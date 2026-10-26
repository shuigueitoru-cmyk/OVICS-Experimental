#ifndef OVICS_MOUSE_H
#define OVICS_MOUSE_H
#include "memory.h"
#define MOUSE_LEFT 1u
#define MOUSE_RIGHT 2u
#define MOUSE_MIDDLE 4u
#define MOUSE_EVENTS 64u
enum mouse_event_type { MOUSE_MOVE=1,MOUSE_LEFT_DOWN,MOUSE_LEFT_UP,MOUSE_RIGHT_DOWN,MOUSE_RIGHT_UP,MOUSE_MIDDLE_DOWN,MOUSE_MIDDLE_UP };
struct mouse_event { pm_u32 type,x,y,buttons,sequence; };
struct mouse_device {
 pm_u32 detected,ready,x,y,buttons,packets,errors,overflow,sync_errors,timeouts;
 pm_u32 sequence,head,tail,queued,dropped,bytes,partial,last_byte_tick;
 unsigned char packet[3];
};
extern struct mouse_device mouse_state;
int mouse_init(void);
void mouse_get_position(pm_u32 *x,pm_u32 *y);
pm_u32 mouse_get_buttons(void);
int mouse_is_pressed(pm_u32 mask);
int mouse_next_event(struct mouse_event *event);
#endif
