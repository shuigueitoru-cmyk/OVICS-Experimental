#include "window_manager.h"
#include "desktop.h"
static int wm_work_height(void){return desktop_active?448:480;}
/* No heap or per-window pixel buffers. Model/input/render are separate APIs.
   IDs monotonically advance across resets and are never recycled after deletion. */
struct wm_context {
 struct wm_window windows[WM_MAX_WINDOWS];
 pm_u32 order[WM_MAX_WINDOWS]; /* bottom -> top, includes hidden live windows */
 pm_u32 ready,count,active,next_id,drag_id,close_id;
 int drag_dx,drag_dy;
 pm_u32 dirty,frames,errors,last_sequence,queue_dropped,recoveries;
 pm_u32 last_key_window,last_key,key_events;
};
static struct wm_context wm_state;
static struct wm_context *wm=&wm_state;
static pm_u32 wm_session_active;
static int wm_error(void){wm->errors++;return 0;}
static struct wm_window *wm_find(pm_u32 id) {
    if(!id)return 0;
    for(pm_u32 i=0;i<WM_MAX_WINDOWS;i++)if(wm->windows[i].id==id)return &wm->windows[i];
    return 0;
}
static void wm_cancel_capture(void){wm->drag_id=wm->close_id=0;}
static void wm_refresh(void) {
    for(pm_u32 i=0;i<wm->count;i++) {
        struct wm_window *w=wm_find(wm->order[i]);
        if(w){w->z=i;w->active=w->id==wm->active;}
    }
    wm->dirty=1;
}
static void wm_focus_top(void) {
    wm->active=0;
    for(pm_u32 i=wm->count;i>0;i--) {
        struct wm_window *w=wm_find(wm->order[i-1]);
        if(w && w->visible){wm->active=w->id;break;}
    }
}
void wm_init(void) {
    pm_u32 serial=wm->ready?wm->next_id:1;
    memset(wm,0,sizeof(*wm));wm->next_id=serial;wm->ready=1;wm->dirty=1;
}
pm_u32 wm_create_window(const char *title,int x,int y,int width,int height,pm_u32 background) {
    if(!wm->ready || !title || wm->count>=WM_MAX_WINDOWS || !wm->next_id ||
       width<96 || width>640 || height<64 || height>wm_work_height()){wm_error();return 0;}
    pm_u32 n=0;
    while(n<WM_TITLE_BYTES && title[n]) {
        if((unsigned char)title[n]<32 || (unsigned char)title[n]>126){wm_error();return 0;}
        n++;
    }
    if(!n || n==WM_TITLE_BYTES){wm_error();return 0;}
    struct wm_window *w=0;
    for(pm_u32 i=0;i<WM_MAX_WINDOWS;i++)if(!wm->windows[i].id){w=&wm->windows[i];break;}
    if(!w){wm_error();return 0;}
    memset(w,0,sizeof(*w));w->id=wm->next_id++; /* zero after UINT_MAX permanently refuses new IDs */
    for(pm_u32 i=0;i<=n;i++)w->title[i]=title[i];
    w->width=width;w->height=height;
    w->x=x<0?0:x>640-width?640-width:x;w->y=y<0?0:y>wm_work_height()-height?wm_work_height()-height:y;
    w->background=background&0xffffffu;w->visible=1;
    wm->order[wm->count++]=w->id;wm->active=w->id;wm_refresh();return w->id;
}
int wm_destroy_window(pm_u32 id) {
    struct wm_window *w=wm_find(id);if(!wm->ready || !w)return wm_error();
    pm_u32 at=wm->count;
    for(pm_u32 i=0;i<wm->count;i++)if(wm->order[i]==id){at=i;break;}
    if(at==wm->count)return wm_error();
    for(pm_u32 i=at;i+1<wm->count;i++)wm->order[i]=wm->order[i+1];
    wm->order[--wm->count]=0;memset(w,0,sizeof(*w));
    if(wm->drag_id==id || wm->close_id==id)wm_cancel_capture();
    if(wm->active==id)wm_focus_top();wm_refresh();return 1;
}
int wm_move_window(pm_u32 id,int x,int y) {
    struct wm_window *w=wm_find(id);if(!wm->ready || !w)return wm_error();
    int nx=x<0?0:x>640-w->width?640-w->width:x;
    int ny=y<0?0:y>wm_work_height()-w->height?wm_work_height()-w->height:y;
    if(w->x!=nx || w->y!=ny){w->x=nx;w->y=ny;wm->dirty=1;}return 1;
}
int wm_focus_window(pm_u32 id) {
    struct wm_window *w=wm_find(id);if(!wm->ready || !w || !w->visible)return wm_error();
    pm_u32 at=wm->count;
    for(pm_u32 i=0;i<wm->count;i++)if(wm->order[i]==id){at=i;break;}
    if(at==wm->count)return wm_error();
    for(pm_u32 i=at;i+1<wm->count;i++)wm->order[i]=wm->order[i+1];
    wm->order[wm->count-1]=id;wm->active=id;wm_refresh();return 1;
}
int wm_show_window(pm_u32 id) {
    struct wm_window *w=wm_find(id);if(!wm->ready || !w)return wm_error();
    w->visible=1;return wm_focus_window(id);
}
int wm_hide_window(pm_u32 id) {
    struct wm_window *w=wm_find(id);if(!wm->ready || !w)return wm_error();
    w->visible=0;if(wm->drag_id==id || wm->close_id==id)wm_cancel_capture();
    if(wm->active==id)wm_focus_top();wm_refresh();return 1;
}
int wm_get_window(pm_u32 id,struct wm_window *out) {
    struct wm_window *w=wm_find(id);if(!wm->ready || !w || !out)return wm_error();*out=*w;return 1;
}
static struct wm_window *wm_hit(int x,int y) {
    for(pm_u32 i=wm->count;i>0;i--) {
        struct wm_window *w=wm_find(wm->order[i-1]);
        if(w && w->visible && x>=w->x && x<w->x+w->width && y>=w->y && y<w->y+w->height)return w;
    }
    return 0;
}
static int wm_close_hit(const struct wm_window *w,int x,int y) {
    return x>=w->x+w->width-21 && x<w->x+w->width-5 && y>=w->y+4 && y<w->y+20;
}
int wm_handle_mouse_event(const struct mouse_event *e) {
    if(!wm->ready || !e || e->type<MOUSE_MOVE || e->type>MOUSE_MIDDLE_UP || e->x>639 || e->y>479 || e->buttons&~7u)return wm_error();
    if(e->type>=2) {
        pm_u32 bit=1u<<((e->type-2)/2),down=!(e->type&1);
        if(!!(e->buttons&bit)!=down)return wm_error();
    }
    if(wm->last_sequence && e->sequence!=wm->last_sequence+1){wm_cancel_capture();wm->recoveries++;}
    wm->last_sequence=e->sequence;
    if(desktop_active && desktop_mouse(e))return 1;
    int x=(int)e->x,y=(int)e->y;
    if(e->type==MOUSE_LEFT_DOWN) {
        wm_cancel_capture();struct wm_window *w=wm_hit(x,y);
        if(!w){wm->active=0;wm_refresh();return 1;}
        wm_focus_window(w->id);
        if(wm_close_hit(w,x,y))wm->close_id=w->id;
        else if(y<w->y+WM_TITLE_HEIGHT){wm->drag_id=w->id;wm->drag_dx=x-w->x;wm->drag_dy=y-w->y;}
    } else if(e->type==MOUSE_MOVE) {
        if(!(e->buttons&MOUSE_LEFT))wm_cancel_capture();
        else if(wm->drag_id)wm_move_window(wm->drag_id,x-wm->drag_dx,y-wm->drag_dy);
    } else if(e->type==MOUSE_LEFT_UP) {
        struct wm_window *w=wm_find(wm->close_id);
        pm_u32 close=w && w->visible && wm_hit(x,y)==w && wm_close_hit(w,x,y)?w->id:0;
        wm_cancel_capture();if(close)wm_destroy_window(close);
    }
    return 1;
}
static void wm_draw_window(const struct wm_window *w) {
    gfx_fill_rect(w->x,w->y,w->width,w->height,w->background);
    gfx_fill_rect(w->x+1,w->y+1,w->width-2,WM_TITLE_HEIGHT-1,w->active?0x2e79c4:0x50637a);
    gfx_rect(w->x,w->y,w->width,w->height,0x90a4ba);
    char title[WM_TITLE_BYTES];pm_u32 fit=(pm_u32)(w->width-34)/8,i=0;
    while(i<fit && i<WM_TITLE_BYTES-1 && w->title[i]){title[i]=w->title[i];i++;}title[i]=0;
    gfx_text(w->x+6,w->y+4,title,0xffffff);
    int bx=w->x+w->width-21,by=w->y+4;
    gfx_fill_rect(bx,by,16,16,0xb83b47);gfx_rect(bx,by,16,16,0xffffff);
    gfx_line(bx+4,by+4,bx+11,by+11,0xffffff);gfx_line(bx+11,by+4,bx+4,by+11,0xffffff);
    if(desktop_active)desktop_content(w);
    else gfx_text(w->x+8,w->y+36,"CONTENT",w->background==0x122035?0xffffff:0x152536);
}
void wm_draw(void) {
    if(!wm->ready || !wm_session_active || !gfx_stats.active)return;
    cursor_graphics_begin();
    if(desktop_active)desktop_background();
    else {gfx_clear(0x1d3040);
    gfx_text(12,10,"OVICS Window Manager",0xffffff);
    gfx_text(12,454,"DRAG TITLE / CLOSE X / ESC: CUI",0xc1d5e7);}
    for(pm_u32 i=0;i<wm->count;i++) {
        struct wm_window *w=wm_find(wm->order[i]);if(w && w->visible)wm_draw_window(w);
    }
    if(desktop_active)desktop_overlay();
    cursor_graphics_end();wm->dirty=0;wm->frames++;
}
static void wm_mode_leave(void){desktop_mode_leave();wm_session_active=0;wm_cancel_capture();}
static void wm_tick(void) {
    if(!wm_session_active)return;
    if(!gfx_stats.active){wm_mode_leave();return;}
    struct mouse_event e;
    if(mouse_state.dropped!=wm->queue_dropped) {
        wm_cancel_capture();wm->recoveries++;wm->queue_dropped=mouse_state.dropped;desktop_input_reset();
        for(pm_u32 n=0;n<MOUSE_EVENTS && mouse_next_event(&e);n++){}
        wm->last_sequence=mouse_state.sequence;
    } else for(pm_u32 n=0;n<MOUSE_EVENTS && mouse_next_event(&e);n++)wm_handle_mouse_event(&e);
    if(wm->drag_id && !(mouse_get_buttons()&MOUSE_LEFT))wm_cancel_capture();
    if(desktop_active)desktop_sync();
    if(wm->dirty)wm_draw();cursor_update();
}
#if OVICS_WM_API_TEST
#include "window_fixture.c"
#endif
static void wm_handle_key(char c,int control) {
#if OVICS_WM_API_TEST
    if(wm_session_active && !control)wm_fixture_key(c);
#endif
    /* Extension point only; no GUI text widget or process is claimed. */
    if(wm_session_active && wm_find(wm->active)) {
        wm->last_key_window=wm->active;wm->last_key=(unsigned char)c|((pm_u32)!!control<<8);wm->key_events++;
        if(desktop_active)desktop_key(c,control);
    }
}
static void wm_demo_start(void) {
    if(!mouse_state.ready){print("Mouse unavailable: ");print(mouse_error);print("\n");return;}
    if(!gfx_enter()){print("Graphics unavailable: ");print(gfx_error);print("\n");return;}
    wm_init();mouse_demo_active=0;
    struct mouse_event e;while(mouse_next_event(&e)){}
    wm->last_sequence=mouse_state.sequence;wm->queue_dropped=mouse_state.dropped;
    wm_create_window("OVICS Explorer",32,50,280,210,0xe6edf3);
    wm_create_window("OVICS Terminal",196,140,300,220,0x122035);
    wm_create_window("OVICS About",365,78,230,160,0xf2ead7);
#if OVICS_WM_API_TEST
    wm_fixture_start();
#endif
    wm_session_active=1;wm_draw();cursor_state.enabled=1;cursor_draw();
}
static void wm_print_info(void) {
    print("Window Manager Status: ");print(wm->ready?"Ready\n":"Not initialized\n");
    print("Window Count: ");print_number(wm->count);print("\nActive Window: ");print_number(wm->active);
    print("\nMaximum Windows: 8\nScreen Resolution: 640 x 480\n");
    print("Redraw Frames: ");print_number(wm->frames);print("\nInput Recoveries: ");print_number(wm->recoveries);
    print("\nInvalid Requests: ");print_number(wm->errors);print("\n");
}
