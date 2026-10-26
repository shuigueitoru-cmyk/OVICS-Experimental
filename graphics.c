#include "graphics.h"
/* QEMU PCI standard VGA (1234:1111), legacy Bochs DISPI, 640x480 XRGB8888.
   The driver does not assume a fixed physical BAR or change PCI resource sizes. */
struct gfx_status gfx_stats;
const char *gfx_error="Not initialized";
static volatile pm_u32 *gfx_fb;
static unsigned char gfx_saved_vram[262144];
static unsigned char gfx_font[256][16];
static unsigned char gfx_seq[5],gfx_crtc[25],gfx_gc[9],gfx_attr[21],gfx_dac[768];
static unsigned char gfx_misc,gfx_mask;
static pm_u32 gfx_demo_last;
static int gfx_demo_mode;
static unsigned short gfx_dispi_read(unsigned short index) { outw(0x1ce,index);return inw(0x1cf); }
static void gfx_dispi_write(unsigned short index,unsigned short value) { outw(0x1ce,index);outw(0x1cf,value); }
static unsigned char gfx_index_read(unsigned short port,unsigned char i) { outb(port,i);return inb(port+1); }
static void gfx_index_write(unsigned short port,unsigned char i,unsigned char v) { outb(port,i);outb(port+1,v); }
static int gfx_reject(void) { gfx_stats.rejected++;return 0; }
int gfx_init(void) {
    if(gfx_stats.ready)return 1;
    pm_u32 bus,slot,function,bar=0,found=0;
    for(bus=0;bus<256 && !found;bus++)for(slot=0;slot<32 && !found;slot++) {
        if((pci_read(bus,slot,0,0)&65535)==65535)continue;
        pm_u32 functions=(pci_read(bus,slot,0,12)&0x00800000)?8:1;
        for(function=0;function<functions;function++)if(pci_read(bus,slot,function,0)==0x11111234u &&
            (pci_read(bus,slot,function,8)>>16)==0x0300u) {
            pm_u32 command=pci_read(bus,slot,function,4);
            if((command&3)!=3){gfx_error="VGA PCI I/O or memory decoding disabled";return 0;}
            bar=pci_read(bus,slot,function,16);
            if((bar&15)!=8 && (bar&15)!=0){gfx_error="Unsupported VGA BAR type";return 0;}
            found=1;break;
        }
    }
    if(!found){gfx_error="QEMU PCI standard VGA unavailable (use -vga std)";return 0;}
    bar&=0xfffffff0u;
    unsigned short id=gfx_dispi_read(0),memory=gfx_dispi_read(10);
    if(id<0xb0c4 || id>0xb0c5 || memory<19 || gfx_dispi_read(4)&1) {
        gfx_error="Unsupported DISPI, insufficient VRAM, or preexisting graphics mode";return 0;
    }
    pm_u32 bytes=GFX_WIDTH*GFX_HEIGHT*4;
    if(!vm_framebuffer_map(bar,bytes)){gfx_error=vm_last_error;return 0;}
    gfx_fb=(volatile pm_u32 *)GFX_VIRTUAL;
    gfx_stats.width=GFX_WIDTH;gfx_stats.height=GFX_HEIGHT;gfx_stats.pitch=GFX_WIDTH*4;gfx_stats.bpp=32;
    gfx_stats.physical=bar;gfx_stats.virtual_address=GFX_VIRTUAL;gfx_stats.bytes=bytes;
    gfx_stats.pages=(bytes+4095)>>12;gfx_stats.ready=1;gfx_error="None";return 1;
}
static void gfx_save_text(void) {
    gfx_misc=inb(0x3cc);gfx_mask=inb(0x3c6);
    for(int i=0;i<5;i++)gfx_seq[i]=gfx_index_read(0x3c4,(unsigned char)i);
    for(int i=0;i<25;i++)gfx_crtc[i]=gfx_index_read(0x3d4,(unsigned char)i);
    for(int i=0;i<9;i++)gfx_gc[i]=gfx_index_read(0x3ce,(unsigned char)i);
    for(int i=0;i<21;i++){inb(0x3da);outb(0x3c0,(unsigned char)i);gfx_attr[i]=inb(0x3c1);}
    inb(0x3da);outb(0x3c0,0x20);
    outb(0x3c7,0);for(int i=0;i<768;i++)gfx_dac[i]=inb(0x3c9);
    volatile unsigned char *raw=(volatile unsigned char *)gfx_fb;
    for(pm_u32 i=0;i<sizeof(gfx_saved_vram);i++)gfx_saved_vram[i]=raw[i];
    /* Standard VGA stores four interleaved planes. BIOS default font is plane 2,
       character map zero, 32 bytes per glyph; capture it rather than borrow fonts. */
    for(int c=0;c<256;c++)for(int row=0;row<16;row++)gfx_font[c][row]=gfx_saved_vram[(c*32+row)*4+2];
}
static void gfx_restore_text(void) {
    gfx_dispi_write(4,0);
    volatile unsigned char *raw=(volatile unsigned char *)gfx_fb;
    for(pm_u32 i=0;i<sizeof(gfx_saved_vram);i++)raw[i]=gfx_saved_vram[i];
    outb(0x3c2,gfx_misc);
    gfx_index_write(0x3c4,0,1);
    for(int i=1;i<5;i++)gfx_index_write(0x3c4,(unsigned char)i,gfx_seq[i]);
    gfx_index_write(0x3c4,0,gfx_seq[0]);
    gfx_index_write(0x3d4,0x11,(unsigned char)(gfx_crtc[0x11]&0x7f));
    for(int i=0;i<25;i++)if(i!=0x11)gfx_index_write(0x3d4,(unsigned char)i,gfx_crtc[i]);
    gfx_index_write(0x3d4,0x11,gfx_crtc[0x11]);
    for(int i=0;i<9;i++)gfx_index_write(0x3ce,(unsigned char)i,gfx_gc[i]);
    for(int i=0;i<21;i++){inb(0x3da);outb(0x3c0,(unsigned char)i);outb(0x3c0,gfx_attr[i]);}
    inb(0x3da);outb(0x3c0,0x20);
    outb(0x3c6,gfx_mask);outb(0x3c8,0);for(int i=0;i<768;i++)outb(0x3c9,gfx_dac[i]);
}
static int gfx_enter(void) {
    if(!gfx_stats.ready || gfx_stats.active)return 0;
    gfx_save_text();
    if(!(gfx_misc&1) || gfx_seq[3] || (gfx_crtc[9]&31)!=15) {
        gfx_error="Text restoration requires the default color VGA 8x16 font";return 0;
    }
    gfx_dispi_write(4,0);gfx_dispi_write(1,GFX_WIDTH);gfx_dispi_write(2,GFX_HEIGHT);gfx_dispi_write(3,32);
    gfx_dispi_write(6,GFX_WIDTH);gfx_dispi_write(8,0);gfx_dispi_write(9,0);
    gfx_dispi_write(4,0xc1); /* enabled + linear framebuffer + do not clear VRAM */
    if(gfx_dispi_read(1)!=GFX_WIDTH || gfx_dispi_read(2)!=GFX_HEIGHT || gfx_dispi_read(3)!=32 ||
       gfx_dispi_read(6)!=GFX_WIDTH || (gfx_dispi_read(4)&0x41)!=0x41) {
        gfx_restore_text();gfx_error="DISPI mode readback failed";return 0;
    }
    gfx_stats.active=1;gfx_stats.transitions++;gfx_error="None";return 1;
}
static void gfx_leave(void) {
    if(!gfx_stats.active)return;
    wm_mode_leave();cursor_mode_leave();gfx_restore_text();gfx_stats.active=0;gfx_demo_mode=0;gfx_stats.transitions++;
    update_cursor();
}
static void gfx_fault_console(void) {
    console_sink=0;
    /* No allocation, logging or continuation from an exception. Restore the
       pinned VGA state before the fatal exception handler uses its text output. */
    if(gfx_stats.active){wm_mode_leave();cursor_mode_leave();gfx_restore_text();gfx_stats.active=0;gfx_demo_mode=0;}
}
void gfx_size(pm_u32 *width,pm_u32 *height) {
    if(width)*width=gfx_stats.width;if(height)*height=gfx_stats.height;
}
static int gfx_raw_pixel(int x,int y,pm_u32 rgb) {
    if(!gfx_stats.active || x<0 || y<0 || x>=GFX_WIDTH || y>=GFX_HEIGHT)return gfx_reject();
    gfx_fb[(pm_u32)y*GFX_WIDTH+(pm_u32)x]=rgb&0xffffffu;return 1;
}
static void gfx_raw_clear(pm_u32 rgb) {
    if(!gfx_stats.active){gfx_reject();return;}
    for(pm_u32 i=0;i<GFX_WIDTH*GFX_HEIGHT;i++)gfx_fb[i]=rgb&0xffffffu;
}
/* 64-bit rectangle arithmetic keeps even INT_MIN/INT_MAX inputs safe. */
static int gfx_fill_bounds(long long left,long long top,long long right,long long bottom,pm_u32 rgb) {
    if(!gfx_stats.active || right<=0 || bottom<=0 || left>=GFX_WIDTH || top>=GFX_HEIGHT || right<=left || bottom<=top)return 0;
    if(left<0)left=0;if(top<0)top=0;if(right>GFX_WIDTH)right=GFX_WIDTH;if(bottom>GFX_HEIGHT)bottom=GFX_HEIGHT;
    for(int y=(int)top;y<(int)bottom;y++)for(int x=(int)left;x<(int)right;x++)gfx_fb[y*GFX_WIDTH+x]=rgb&0xffffffu;
    return 1;
}
static int gfx_raw_fill_rect(int x,int y,int width,int height,pm_u32 rgb) {
    if(width<=0 || height<=0 || !gfx_fill_bounds(x,y,(long long)x+width,(long long)y+height,rgb))return gfx_reject();
    return 1;
}
static int gfx_raw_rect(int x,int y,int width,int height,pm_u32 rgb) {
    if(!gfx_stats.active || width<=0 || height<=0)return gfx_reject();
    long long r=(long long)x+width,b=(long long)y+height;int drawn=0;
    drawn|=gfx_fill_bounds(x,y,r,(long long)y+1,rgb);drawn|=gfx_fill_bounds(x,b-1,r,b,rgb);
    drawn|=gfx_fill_bounds(x,y,(long long)x+1,b,rgb);drawn|=gfx_fill_bounds(r-1,y,r,b,rgb);
    return drawn?1:gfx_reject();
}
static int gfx_raw_line(int x0,int y0,int x1,int y1,pm_u32 rgb) {
    /* Reject out-of-screen endpoints. Bounded Bresenham avoids overflow or a
       multi-billion-step loop from hostile coordinates; rectangles/text clip. */
    if(!gfx_stats.active || x0<0 || x1<0 || y0<0 || y1<0 || x0>=GFX_WIDTH || x1>=GFX_WIDTH || y0>=GFX_HEIGHT || y1>=GFX_HEIGHT)return gfx_reject();
    int dx=x1>x0?x1-x0:x0-x1,sx=x0<x1?1:-1,dy=y1>y0?y0-y1:y1-y0,sy=y0<y1?1:-1,error=dx+dy;
    for(;;){gfx_pixel(x0,y0,rgb);if(x0==x1 && y0==y1)break;int e=2*error;if(e>=dy){error+=dy;x0+=sx;}if(e<=dx){error+=dx;y0+=sy;}}
    return 1;
}
static int gfx_raw_text(int x,int y,const char *text,pm_u32 rgb) {
    if(!gfx_stats.active || !text)return gfx_reject();
    long long px=x,py=y;int drawn=0;
    for(pm_u32 n=0;n<256 && text[n];n++) {
        unsigned char c=(unsigned char)text[n];if(c=='\n'){px=x;py+=16;continue;}
        if(px<GFX_WIDTH && px+8>0 && py<GFX_HEIGHT && py+16>0) {
            for(int row=0;row<16;row++)for(int bit=0;bit<8;bit++)if(gfx_font[c][row]&(128>>bit)) {
                long long xx=px+bit,yy=py+row;
                if(xx>=0 && yy>=0 && xx<GFX_WIDTH && yy<GFX_HEIGHT){gfx_fb[(int)yy*GFX_WIDTH+(int)xx]=rgb&0xffffffu;drawn=1;}
            }
        }
        px+=8;
    }
    return drawn?1:gfx_reject();
}
int gfx_pixel(int x,int y,pm_u32 rgb){cursor_graphics_begin();int ok=gfx_raw_pixel(x,y,rgb);cursor_graphics_end();return ok;}
void gfx_clear(pm_u32 rgb){cursor_graphics_begin();gfx_raw_clear(rgb);cursor_graphics_end();}
int gfx_fill_rect(int x,int y,int w,int h,pm_u32 rgb){cursor_graphics_begin();int ok=gfx_raw_fill_rect(x,y,w,h,rgb);cursor_graphics_end();return ok;}
int gfx_rect(int x,int y,int w,int h,pm_u32 rgb){cursor_graphics_begin();int ok=gfx_raw_rect(x,y,w,h,rgb);cursor_graphics_end();return ok;}
int gfx_line(int x0,int y0,int x1,int y1,pm_u32 rgb){cursor_graphics_begin();int ok=gfx_raw_line(x0,y0,x1,y1,rgb);cursor_graphics_end();return ok;}
int gfx_text(int x,int y,const char *s,pm_u32 rgb){cursor_graphics_begin();int ok=gfx_raw_text(x,y,s,rgb);cursor_graphics_end();return ok;}
static pm_u32 gfx_checksum(void) {
    pm_u32 h=2166136261u;for(pm_u32 i=0;i<GFX_WIDTH*GFX_HEIGHT;i++){h^=gfx_fb[i];h*=16777619u;}return h;
}
/* Observable actual VRAM results; independently checked by the QEMU harness. */
struct gfx_test_result {
 pm_u32 checks,passed,failure,background,pixel,line,rectangle,fill,text,edge;
 pm_u32 before_rejection,after_rejection,rejections,width,height,checksum,ownership;
};
volatile struct gfx_test_result gfx_test_record;
static int gfx_selftest(void) {
    struct gfx_test_result *r=(struct gfx_test_result *)&gfx_test_record;
    memset(r,0,sizeof(*r));
#define GFX_CHECK(c) do { r->checks++;if(!(c)){r->failure=r->checks;return 0;} } while(0)
    pm_u32 width,height;gfx_size(&width,&height);r->width=width;r->height=height;
    GFX_CHECK(width==640 && height==480);
    gfx_clear(0x112233);r->background=gfx_fb[300*640+500];GFX_CHECK(r->background==0x112233);
    GFX_CHECK(gfx_pixel(20,60,0x00ff00));r->pixel=gfx_fb[60*640+20];GFX_CHECK(r->pixel==0x00ff00);
    GFX_CHECK(gfx_line(40,80,180,140,0xffaa00));r->line=gfx_fb[110*640+110];GFX_CHECK(r->line==0xffaa00);
    GFX_CHECK(gfx_rect(220,70,130,90,0xffffff));r->rectangle=gfx_fb[70*640+220];GFX_CHECK(r->rectangle==0xffffff && gfx_fb[71*640+221]==0x112233);
    GFX_CHECK(gfx_fill_rect(390,70,160,90,0x2244cc));r->fill=gfx_fb[100*640+430];GFX_CHECK(r->fill==0x2244cc);
    GFX_CHECK(gfx_text(24,200,"OVICS 0.2.2 Experimental\nPIXEL / LINE / RECT / TEXT",0xffffff));
    for(int y=200;y<216;y++)for(int x=24;x<32;x++)if(gfx_fb[y*640+x]==0xffffff)r->text++;
    GFX_CHECK(r->text>0);
    GFX_CHECK(gfx_rect(0,0,640,480,0xff00ff));r->edge=gfx_fb[479*640+639];GFX_CHECK(r->edge==0xff00ff);
    GFX_CHECK(gfx_fill_rect(-4,300,10,10,0x00cccc));GFX_CHECK(gfx_fb[300*640]==0x00cccc && gfx_fb[300*640+6]==0x112233);
    pm_u32 rejected=gfx_stats.rejected;r->before_rejection=gfx_checksum();
    GFX_CHECK(!gfx_pixel(-1,0,1));GFX_CHECK(!gfx_pixel(640,0,1));GFX_CHECK(!gfx_pixel(0,480,1));
    GFX_CHECK(!gfx_line(-1,0,639,479,1));GFX_CHECK(!gfx_line(-2147483647-1,0,2147483647,479,1));
    GFX_CHECK(!gfx_fill_rect(2147483647,0,2147483647,2,1));GFX_CHECK(!gfx_fill_rect(0,0,-1,1,1));
    GFX_CHECK(!gfx_rect(-2147483647-1,-2147483647-1,2147483647,2147483647,1));
    GFX_CHECK(!gfx_text(2147483647,2147483647,"outside",1));
    r->after_rejection=gfx_checksum();r->rejections=gfx_stats.rejected-rejected;
    GFX_CHECK(r->before_rejection==r->after_rejection && r->rejections==9);
    pm_u32 used=pm_stats.used,mapped=vm_stats.mapped_pages,tables=vm_stats.table_count;
    GFX_CHECK(!pm_free(gfx_stats.physical));
    GFX_CHECK(!vm_unmap_page(GFX_VIRTUAL));
    GFX_CHECK(!vm_map_page(0xd4000000u,gfx_stats.physical,VM_WRITE));
    pm_u32 owned=pm_alloc();GFX_CHECK(owned!=0);
    int duplicate=vm_map_page(GFX_VIRTUAL,owned,VM_WRITE);
    int refs=pm_vm_refs[owned>>12];int released=pm_free(owned);
    GFX_CHECK(!duplicate && !refs && released);
    GFX_CHECK(!vm_framebuffer_map(gfx_stats.physical,gfx_stats.bytes));
    GFX_CHECK(pm_stats.used==used && vm_stats.mapped_pages==mapped && vm_stats.table_count==tables);
    r->ownership=1;
    gfx_text(24,260,"DRAWING CHECKS PASS - ESC RETURNS TO CUI",0x00ff88);
    r->checksum=gfx_checksum();r->passed=1;gfx_stats.tests_passed++;return 1;
#undef GFX_CHECK
}
static void gfx_demo_draw(void) {
    gfx_clear(0x101828);
    for(int y=60;y<420;y+=40)for(int x=20;x<620;x+=40)
        gfx_fill_rect(x,y,36,36,((pm_u32)(x*255/640)<<16)|((pm_u32)(y*255/480)<<8)|((gfx_stats.demo_frames*13)&255));
    gfx_rect(0,0,640,480,0x55ccff);
    gfx_line(0,479,639,0,0xffffff);gfx_text(16,16,"OVICS GRAPHICS DEMO - ESC: VGA CUI",0xffffff);
    gfx_fill_rect((int)((gfx_stats.demo_frames*11)%560)+20,430,40,20,0xffaa22);
    gfx_stats.demo_frames++;
}
static void gfx_tick(pm_u32 now) {
#if OVICS_GFX_FAULT_TEST
    if(gfx_stats.active && gfx_test_record.passed) {
        volatile pm_u32 value=*(volatile pm_u32 *)0xe0000000u;(void)value;
    }
#endif
    if(gfx_stats.active && gfx_demo_mode && (pm_u32)(now-gfx_demo_last)>=200) {
        gfx_demo_last=now;gfx_demo_draw();
    }
}
static void gfx_start(int demo) {
    if(!gfx_enter()){print("Graphics unavailable: ");print(gfx_error);print("\n");return;}
    gfx_demo_mode=demo;
    if(demo){gfx_demo_last=clock_now();gfx_demo_draw();}
    else if(!gfx_selftest()) {gfx_leave();print("Graphics test FAIL\n");}
}
static void gfx_print_info(void) {
    print("Graphics: ");print(gfx_stats.ready?"QEMU PCI standard VGA\n":"unavailable\n");
    print("Mode: 640 x 480 x 32 (XRGB8888)\nPitch: ");print_number(gfx_stats.pitch);print(" bytes\n");
    print("Framebuffer physical: 0x");print_hex(gfx_stats.physical,8);print("\nFramebuffer virtual: 0x");print_hex(gfx_stats.virtual_address,8);
    print("\nMapped pages: ");print_number(gfx_stats.pages);print(" (locked, supervisor RW, PCD/PWT)\n");
    print("Drawing checks passed: ");print_number(gfx_stats.tests_passed);print("\nVGA transitions: ");print_number(gfx_stats.transitions);
    print("\nLast graphics error: ");print(gfx_error);print("\n");
}
