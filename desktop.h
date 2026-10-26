#ifndef OVICS_DESKTOP_H
#define OVICS_DESKTOP_H
struct wm_window;
struct mouse_event;
static unsigned int desktop_active;
static void desktop_background(void);
static void desktop_overlay(void);
static void desktop_content(const struct wm_window *window);
static int desktop_mouse(const struct mouse_event *event);
static void desktop_key(char c,int control);
static void desktop_sync(void);
static void desktop_mode_leave(void);
static void desktop_input_reset(void);
#endif
