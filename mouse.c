#include "mouse.h"
struct mouse_device mouse_state;
static struct mouse_event mouse_events[MOUSE_EVENTS];
static pm_u64 ps2_current_stamp;
const char *mouse_error="Not initialized";
struct mouse_stamp { pm_u32 sequence;pm_u64 milliseconds; };
static struct mouse_stamp mouse_stamps[MOUSE_EVENTS];
static int mouse_event_time(pm_u32 sequence,pm_u64 *ms) {
 struct mouse_stamp *s=&mouse_stamps[sequence%MOUSE_EVENTS];
 if(s->sequence!=sequence)return 0;*ms=s->milliseconds;return 1;
}
static void mouse_queue(struct mouse_device *s,struct mouse_event *queue,pm_u32 type) {
    struct mouse_event e={type,s->x,s->y,s->buttons,++s->sequence};
    /* Drop oldest, retain latest. Sequence gaps/dropped expose lost events;
       authoritative position/buttons always remain available via the state API. */
    if(s->queued==MOUSE_EVENTS){s->tail=(s->tail+1)%MOUSE_EVENTS;s->queued--;s->dropped++;}
    if(s==&mouse_state){struct mouse_stamp *stamp=&mouse_stamps[e.sequence%MOUSE_EVENTS];stamp->sequence=e.sequence;stamp->milliseconds=ps2_current_stamp;}
    queue[s->head]=e;s->head=(s->head+1)%MOUSE_EVENTS;s->queued++;
}
static void mouse_decode(struct mouse_device *s,struct mouse_event *queue,unsigned char byte,pm_u32 tick) {
    s->bytes++;
    if(s->partial && (pm_u32)(tick-s->last_byte_tick)>=250){s->partial=0;s->errors++;s->timeouts++;}
    s->last_byte_tick=tick;
    if(!s->partial && !(byte&8)){s->errors++;s->sync_errors++;return;}
    s->packet[s->partial++]=byte;
    if(s->partial<3)return;
    s->partial=0;s->packets++;
    unsigned char flags=s->packet[0];
    /* Overflow invalidates motion only, not releases: never strand a button. */
    int dx=(int)s->packet[1]-((flags&16)?256:0);
    int dy=(int)s->packet[2]-((flags&32)?256:0);
    pm_u32 oldx=s->x,oldy=s->y,oldbuttons=s->buttons;
    if(flags&0xc0){s->overflow++;s->errors++;}
    else {
        int x=(int)s->x+dx,y=(int)s->y-dy;
        if(x<0)x=0;if(x>639)x=639;if(y<0)y=0;if(y>479)y=479;
        s->x=(pm_u32)x;s->y=(pm_u32)y;
    }
    s->buttons=flags&7;
    if(s->x!=oldx || s->y!=oldy)mouse_queue(s,queue,MOUSE_MOVE);
    for(pm_u32 bit=1,i=0;i<3;bit<<=1,i++)if((oldbuttons^s->buttons)&bit)
        mouse_queue(s,queue,2+i*2+((s->buttons&bit)?0:1));
}
void mouse_get_position(pm_u32 *x,pm_u32 *y){if(x)*x=mouse_state.x;if(y)*y=mouse_state.y;}
pm_u32 mouse_get_buttons(void){return mouse_state.buttons;}
int mouse_is_pressed(pm_u32 mask){return mask && !(mask&~7u) && (mouse_state.buttons&mask)==mask;}
int mouse_next_event(struct mouse_event *event) {
    if(!event || !mouse_state.queued)return 0;
    *event=mouse_events[mouse_state.tail];mouse_state.tail=(mouse_state.tail+1)%MOUSE_EVENTS;mouse_state.queued--;return 1;
}
/* 8042 has one shared output buffer. This is its only runtime data reader.
   Keyboard bytes and AUX bytes cannot enter each other's parsers. Wake-only
   IRQ1/12 handlers never read data; decoding stays in this polling path. */
static unsigned char ps2_keys[64];
static pm_u32 ps2_key_head,ps2_key_tail,ps2_key_count,ps2_key_dropped,ps2_errors;
static volatile pm_u32 ps2_key_lost;
static void ps2_keyboard_byte(unsigned char byte) {
    if(ps2_key_count==sizeof(ps2_keys)){ps2_key_dropped+=ps2_key_count;ps2_key_count=ps2_key_head=ps2_key_tail=0;ps2_key_lost=1;}
    ps2_keys[ps2_key_head]=byte;ps2_key_head=(ps2_key_head+1)%sizeof(ps2_keys);ps2_key_count++;
}
static void ps2_route(unsigned char status,unsigned char byte) {
    if(status&0xc0){ps2_errors++;if(status&0x20){mouse_state.partial=0;mouse_state.errors++;}return;}
    if(status&0x20){if(mouse_state.ready)mouse_decode(&mouse_state,mouse_events,byte,(pm_u32)ps2_current_stamp);}
    else ps2_keyboard_byte(byte);
}
/* Hardware IRQs retain raw data promptly during slow drawing. Decoding and
   all mouse/key queue mutations stay in the normal consumer. */
struct ps2_raw_byte {unsigned char status,byte;pm_u64 ms;};
static volatile struct ps2_raw_byte ps2_raw[256];
static volatile pm_u32 ps2_raw_head,ps2_raw_tail,ps2_raw_count,ps2_irq_raw_dropped;
void ps2_irq_capture(void) {
 for(pm_u32 n=0;n<32;n++) {
  unsigned char status=inb(0x64);if(!(status&1))break;unsigned char byte=inb(0x60);
  if(ps2_raw_count==256){ps2_raw_tail=(ps2_raw_tail+1)%256;ps2_raw_count--;ps2_irq_raw_dropped++;ps2_key_lost=1;}
  volatile struct ps2_raw_byte *p=&ps2_raw[ps2_raw_head];p->status=status;p->byte=byte;p->ms=system_timer.milliseconds;
  ps2_raw_head=(ps2_raw_head+1)%256;ps2_raw_count++;
 }
}
static void ps2_poll(void) {
 for(pm_u32 budget=0;budget<64;budget++) {
  unsigned char status,byte;pm_u64 ms;pm_u32 f=timer_irq_save();
  if(ps2_raw_count) {
   volatile struct ps2_raw_byte *p=&ps2_raw[ps2_raw_tail];status=p->status;byte=p->byte;ms=p->ms;
   ps2_raw_tail=(ps2_raw_tail+1)%256;ps2_raw_count--;
  } else {
   status=inb(0x64);if(!(status&1)){timer_irq_restore(f);break;}
   byte=inb(0x60);ms=system_timer.milliseconds;
  }
  timer_irq_restore(f);ps2_current_stamp=ms;ps2_route(status,byte);
 }
}
static int ps2_key_read(unsigned char *byte) {
    ps2_poll();if(!ps2_key_count)return 0;
    *byte=ps2_keys[ps2_key_tail];ps2_key_tail=(ps2_key_tail+1)%sizeof(ps2_keys);ps2_key_count--;return 1;
}
static int ps2_write(unsigned short port,unsigned char value) {
    for(pm_u32 n=0;n<100000;n++)if(!(inb(0x64)&2)){outb(port,value);return 1;}
    mouse_error="8042 input timeout";return 0;
}
static int ps2_response(int auxiliary,unsigned char *value) {
    for(pm_u32 n=0;n<100000;n++) {
        unsigned char status=inb(0x64);if(!(status&1))continue;
        unsigned char byte=inb(0x60);
        if(status&0xc0){ps2_errors++;continue;}
        if(!!(status&0x20)==auxiliary){*value=byte;return 1;}
        ps2_route(status,byte);
    }
    mouse_error="8042 response timeout";return 0;
}
static int ps2_aux_command(unsigned char command) {
    for(int retry=0;retry<3;retry++) {
        unsigned char answer;
        if(!ps2_write(0x64,0xd4) || !ps2_write(0x60,command) || !ps2_response(1,&answer))return 0;
        if(answer==0xfa)return 1;
        if(answer!=0xfe){mouse_error="Mouse command not acknowledged";return 0;}
    }
    mouse_error="Mouse resend limit";return 0;
}
int mouse_init(void) {
    if(mouse_state.ready)return 1;
    mouse_state.x=320;mouse_state.y=240;
    unsigned char config=0,answer=0;int have_config=0,ok=0;
    ps2_poll();
    if(!ps2_write(0x64,0xad))goto done; /* freeze keyboard while reading controller replies */
    ps2_poll();
    if(!ps2_write(0x64,0x20) || !ps2_response(0,&config))goto done;
    have_config=1;
    if(!ps2_write(0x64,0xa8) || !ps2_write(0x64,0xa9) || !ps2_response(0,&answer))goto done;
    if(answer){mouse_error="8042 auxiliary port test failed";goto done;}
    /* Preserve translation/system settings; disable IRQ1/12, enable both clocks. */
    if(!ps2_write(0x64,0x60) || !ps2_write(0x60,(unsigned char)(config&~0x33u)))goto done;
    if(!ps2_aux_command(0xf6) || !ps2_aux_command(0xf2) || !ps2_response(1,&answer))goto done;
    mouse_state.detected=1;
    if(answer!=0){mouse_error="Only standard 3-byte PS/2 mouse supported";goto done;}
    if(!ps2_aux_command(0xf4))goto done;
    mouse_state.ready=1;ok=1;mouse_error="None";
done:
    if(!ok) {
        mouse_state.errors++;
        if(have_config){ps2_write(0x64,0x60);ps2_write(0x60,(unsigned char)((config&~0x13u)|0x20));}
    }
    if(!ps2_write(0x64,0xae)){mouse_state.ready=0;return 0;}
    return ok;
}

/* Wake from idle even if IRQ0 fails; IRQ capture never decodes or draws.
   All 8042 reads stay inside this module. NIC polls on normal timer wakes. */
extern void irq1_stub(void),irq12_stub(void);
static pm_u32 ps2_idle_ready;
static void ps2_enable_idle_wake(void) {
 unsigned char config;ps2_poll();
 if(!ps2_write(0x64,0xad))return;
 if(!ps2_write(0x64,0x20) || !ps2_response(0,&config)){ps2_write(0x64,0xae);return;}
 pm_u32 a=(pm_u32)irq1_stub,b=(pm_u32)irq12_stub;
 idt[33].low=a;idt[33].high=a>>16;idt[33].selector=8;idt[33].zero=0;idt[33].flags=0x8e;
 idt[44].low=b;idt[44].high=b>>16;idt[44].selector=8;idt[44].zero=0;idt[44].flags=0x8e;
 if(!ps2_write(0x64,0x60) || !ps2_write(0x60,(unsigned char)((config&~0x10u)|1u|(mouse_state.ready?2u:0u)))){ps2_write(0x64,0xae);return;}
 if(!ps2_write(0x64,0xae))return;
 outb(0xa1,mouse_state.ready?0xef:0xff);outb(0x21,mouse_state.ready?0xf8:0xfc);ps2_idle_ready=1;
}
