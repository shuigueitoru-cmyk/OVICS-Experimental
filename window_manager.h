#ifndef OVICS_WM_H
#define OVICS_WM_H
#include "mouse.h"
#define WM_MAX_WINDOWS 8u
#define WM_TITLE_BYTES 32u
#define WM_TITLE_HEIGHT 24
struct wm_window {
 pm_u32 id;
 char title[WM_TITLE_BYTES];
 int x,y,width,height;
 pm_u32 visible,active,z,background;
};
void wm_init(void);
pm_u32 wm_create_window(const char *title,int x,int y,int width,int height,pm_u32 background);
int wm_destroy_window(pm_u32 id);
int wm_move_window(pm_u32 id,int x,int y);
int wm_show_window(pm_u32 id);
int wm_hide_window(pm_u32 id);
int wm_focus_window(pm_u32 id);
int wm_get_window(pm_u32 id,struct wm_window *out);
void wm_draw(void);
int wm_handle_mouse_event(const struct mouse_event *event);
#endif
