/* Native Horizon adapter. Host/libcurl frontend is never linked into OVICS. */
#include "horizon.h"
static HzDocument browser_document;
static HzCell browser_cells[72*HZ_LINES_MAX];
static HzLayout browser_layout;
static HzView browser_view;
struct browser_context {pm_u32 ready,valid,pending,loading,cancelled,loads,errors,painted;char url[248];char message[160];};
static struct browser_context browser_state;
static const char browser_home[]="<html><head><title>OVICS Horizon</title></head><body><h1>OVICS Horizon</h1><p>Native HTML browser on the OVICS custom i386 kernel.</p><h2>Navigation</h2><ul><li>Enter an http:// URL and press Enter or Go.</li><li>Ctrl+U clears the address. Up and Down scroll.</li><li>Click a link to inspect its address.</li></ul><h2>Wikipedia</h2><p>Wiki uses a local PC HTTPS relay. Start Start-Wikipedia-Relay.cmd on the host PC first. This is not native TLS.</p><p><a href='https://en.wikipedia.org/wiki/Operating_system'>Wikipedia: Operating system</a></p><h2>Local files</h2><p>Use ram:filename to read a real RAM FS file.</p><h2>Limits</h2><p>HTTP documents up to 8192 bytes. No CSS, images, JavaScript or HTTPS. Non-ASCII glyphs appear as question marks.</p></body></html>";
static void browser_copy(char *out,const char *in,pm_u32 cap){pm_u32 n=0;while(n+1<cap && in[n]){out[n]=in[n];n++;}out[n]=0;}
static void browser_status(const char *s){browser_copy(browser_state.message,s,sizeof(browser_state.message));wm->dirty=1;}
static void browser_reset(void){memset(&browser_state,0,sizeof(browser_state));browser_state.ready=1;browser_copy(browser_state.url,"home:",sizeof(browser_state.url));browser_view=(HzView){0,15,0};browser_layout=(HzLayout){browser_cells,72*HZ_LINES_MAX,0,0,0,HZ_OK};}
static void browser_prepare(void){browser_state.pending=1;}
static int browser_parse(const char *html,pm_u32 n) {
 HzResult r=hz_parse(&browser_document,html,n);
 if(r==HZ_OK)r=hz_layout(&browser_document,&browser_layout,72);
 browser_state.valid=r==HZ_OK;browser_view.offset=browser_view.selected=0;
 if(r!=HZ_OK){browser_state.errors++;browser_status(hz_error(r));return 0;}
 browser_state.loads++;browser_status(browser_document.title[0]?browser_document.title:"Document loaded");return 1;
}
static void browser_pump(void) {
 static int inside;static pm_u64 last;
 if(inside)return;pm_u64 now=timer_get_uptime_ms();if(now-last<10)return;last=now;inside=1;
 if(desktop_active){ps2_poll();kernel_poll_keyboard();wm_tick();}
 if(!desktop_active || !wm_find(gui->windows[3])){
  browser_state.cancelled=1;if(dns_waiting)dns_status=2;
  if(tcp_state!=TCP_CLOSED)tcp_fail("Browser cancelled.");
 }
 inside=0;
}
static void browser_tick(void) {
 if(!desktop_active || !gui->windows[3] || !wm_find(gui->windows[3]) || !browser_state.pending || browser_state.loading)return;
 browser_state.pending=0;browser_state.loading=1;browser_state.cancelled=0;browser_state.valid=0;browser_view.selected=browser_view.offset=0;browser_status("Loading (Esc or X cancels)");wm_draw();
 char address[248];browser_copy(address,browser_state.url,sizeof(address));
 if(!strcmp(address,"home:"))browser_parse(browser_home,sizeof(browser_home)-1);
 else if(starts_with(address,"ram:")) {
  int slot=find_ram_file(address+4);
  if(slot<0){browser_state.errors++;browser_status("RAM file not found");}
  else browser_parse(ram_files[slot].content,http_length(ram_files[slot].content));
 } else if(starts_with(address,"https://")) {browser_state.errors++;browser_status("HTTPS unavailable. Wiki button requires PC relay.");}
 else {
  void (*old_service)(void)=wait_service;struct http_response response;
  wait_service=browser_pump;http_get_native(address,&response);wait_service=old_service;
  if(browser_state.cancelled)browser_status("Cancelled");
  else if(response.error){browser_state.errors++;browser_status(response.error);}
  else if(!response.complete || !response.text){browser_state.errors++;browser_status("Incomplete or non-text HTTP response");}
  else {browser_parse((const char *)response.body,response.length);if(response.status>=400){browser_state.errors++;browser_status("HTTP error response (see page)");}}
 }
 browser_state.loading=0;
 if(!desktop_active)print("Graphics closed. VGA CUI restored.\nOVICS> ");
}
static void browser_paint(void *context,unsigned col,unsigned row,const HzCell *cell,int selected) {
 const struct wm_window *w=context;int x=w->x+8+(int)col*8,y=w->y+88+(int)row*16;
 if(selected)gfx_fill_rect(x,y,8,16,0x284f79);
 char c[2]={(cell->codepoint>=32 && cell->codepoint<=126)?(char)cell->codepoint:cell->codepoint?'?':' ',0};
 gfx_text(x,y,c,selected?0xffffff:cell->link?0x004db3:(cell->style&HZ_HEADING)?0x813d00:0x182333);browser_state.painted++;
}
static void browser_button(int x,int y,int width,const char *s){gfx_fill_rect(x,y,width,22,0xcedbe7);gfx_rect(x,y,width,22,0x50637a);gfx_text(x+4,y+3,s,0x152536);}
static void browser_draw(const struct wm_window *w) {
 int x=w->x,y=w->y;
 gfx_fill_rect(x+8,y+28,w->width-64,22,0xffffff);gfx_rect(x+8,y+28,w->width-64,22,0x50637a);
 pm_u32 len=http_length(browser_state.url);gui_text_line(x+12,y+31,browser_state.url+(len>64?len-64:0),64,0x152536);
 browser_button(x+w->width-48,y+28,40,"Go");browser_button(x+8,y+56,56,"Home");browser_button(x+72,y+56,88,"Wiki relay");browser_button(x+w->width-116,y+56,48,"Up");browser_button(x+w->width-60,y+56,52,"Down");
 browser_view.height=(pm_u32)(w->height-128)/16;
 if(browser_state.valid)hz_paint(&browser_layout,&browser_view,browser_paint,(void *)w);
 gui_text_line(x+8,y+w->height-36,browser_state.message,72,0x152536);
 if(browser_view.selected && browser_view.selected<=browser_document.link_count)gui_text_line(x+8,y+w->height-20,browser_document.arena+browser_document.links[browser_view.selected-1].href,72,0x004db3);
 else gfx_text(x+8,y+w->height-20,"HTTP only | Wiki = host HTTPS relay | Ctrl+U: clear",0x50637a);
}
static void browser_key(char c,int control) {
 if(control){if(c=='u'){browser_state.url[0]=0;wm->dirty=1;}return;}
 pm_u32 n=http_length(browser_state.url);
 if(c=='\n'){if(!browser_state.loading)browser_state.pending=1;}
 else if(c=='\b'){if(n)browser_state.url[n-1]=0;}
 else if(c>=32 && c<=126 && n<247){browser_state.url[n]=c;browser_state.url[n+1]=0;}
 wm->dirty=1;
}
static void browser_mouse(const struct wm_window *w,int x,int y) {
 if(gui_box(x,y,w->x+8,w->y+28,w->width-64,22)){browser_state.url[0]=0;wm->dirty=1;return;}
 if(gui_box(x,y,w->x+w->width-48,w->y+28,40,22)){if(!browser_state.loading)browser_state.pending=1;return;}
 if(gui_box(x,y,w->x+8,w->y+56,56,22)){if(!browser_state.loading){browser_copy(browser_state.url,"home:",248);browser_state.pending=1;}return;}
 if(gui_box(x,y,w->x+72,w->y+56,88,22)){if(!browser_state.loading){browser_copy(browser_state.url,"http://10.0.2.2:8088/wiki/Operating_system",248);browser_state.pending=1;}return;}
 if(gui_box(x,y,w->x+w->width-116,w->y+56,48,22))hz_scroll(&browser_view,&browser_layout,-8);
 else if(gui_box(x,y,w->x+w->width-60,w->y+56,52,22))hz_scroll(&browser_view,&browser_layout,8);
 else if(browser_state.valid && gui_box(x,y,w->x+8,w->y+88,576,(int)browser_view.height*16))hz_select(&browser_view,&browser_document,hz_hit(&browser_layout,&browser_view,(x-w->x-8)/8,(y-w->y-88)/16));
 wm->dirty=1;
}
static void browser_command(const char *url) {
 desktop_start();if(!desktop_active)return;gui_launch(4);browser_copy(browser_state.url,url,248);browser_state.pending=1;
}
