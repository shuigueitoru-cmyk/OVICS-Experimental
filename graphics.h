#ifndef OVICS_GRAPHICS_H
#define OVICS_GRAPHICS_H
#include "memory.h"
#define GFX_WIDTH 640
#define GFX_HEIGHT 480
#define GFX_VIRTUAL 0xE1000000u
struct gfx_status {
 pm_u32 ready,active,width,height,pitch,bpp,physical,virtual_address,bytes,pages;
 pm_u32 rejected,tests_passed,transitions,demo_frames;
};
extern struct gfx_status gfx_stats;
extern const char *gfx_error;
int gfx_init(void);
int gfx_pixel(int x,int y,pm_u32 rgb);
int gfx_line(int x0,int y0,int x1,int y1,pm_u32 rgb);
int gfx_rect(int x,int y,int width,int height,pm_u32 rgb);
int gfx_fill_rect(int x,int y,int width,int height,pm_u32 rgb);
void gfx_clear(pm_u32 rgb);
int gfx_text(int x,int y,const char *text,pm_u32 rgb);
void gfx_size(pm_u32 *width,pm_u32 *height);
#endif
