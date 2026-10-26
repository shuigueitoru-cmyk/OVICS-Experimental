#include "timer.h"
#ifndef OVICS_TIMER_HZ
#define OVICS_TIMER_HZ 100u
#endif
#if OVICS_TIMER_HZ != 100 && OVICS_TIMER_HZ != 1000
#error Supported PIT rates are 100 and 1000 Hz
#endif
#define PIT_INPUT_HZ 1193182u
#define TIMER_DIVISOR ((PIT_INPUT_HZ+OVICS_TIMER_HZ/2)/OVICS_TIMER_HZ)
struct timer_state {
 pm_u64 ticks, milliseconds;
 pm_u32 phase, irq_count, eoi_count, ready, divisor, frequency, saturated;
};
static volatile struct timer_state system_timer;
static pm_u32 timer_watchdog_fault;
static pm_u32 timer_irq_save(void){pm_u32 f;__asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory");return f;}
static void timer_irq_restore(pm_u32 f){__asm__ volatile("pushl %0; popfl"::"r"(f):"memory","cc");}
static pm_u64 timer_add_sat(pm_u64 a,pm_u64 b){return (~(pm_u64)0)-a<b?~(pm_u64)0:a+b;}
static void timer_advance(volatile struct timer_state *t) {
 if(t->ticks!=~(pm_u64)0)t->ticks++;else t->saturated=1;
 /* Remainder accumulation accounts for the rounded PIT divisor without
    multiplying a long-running 64-bit tick count or requiring libgcc. */
 pm_u32 phase=t->phase+t->divisor*1000u;
 pm_u32 delta=phase/PIT_INPUT_HZ;t->phase=phase%PIT_INPUT_HZ;
 pm_u64 old=t->milliseconds;t->milliseconds=timer_add_sat(old,delta);
 if(t->milliseconds==~(pm_u64)0)t->saturated=1;
}
void timer_irq(void) {
 timer_advance(&system_timer);system_timer.irq_count++;
 outb(0x20,0x20);system_timer.eoi_count++;
}
pm_u64 timer_get_ticks(void){pm_u32 f=timer_irq_save();pm_u64 v=system_timer.ticks;timer_irq_restore(f);return v;}
pm_u64 timer_get_uptime_ms(void){pm_u32 f=timer_irq_save();pm_u64 v=system_timer.milliseconds;timer_irq_restore(f);return v;}
extern void irq0_stub(void);
void timer_init(void) {
 if(system_timer.ready)return;
 __asm__ volatile("cli":::"memory");
 memset((void *)&system_timer,0,sizeof(system_timer));
 pm_u32 a=(pm_u32)irq0_stub;idt[32].low=a;idt[32].high=a>>16;idt[32].selector=8;idt[32].zero=0;idt[32].flags=0x8e;
 /* Remap both 8259s, mask every external source except IRQ0. */
 outb(0x21,0xff);outb(0xa1,0xff);
 outb(0x20,0x11);outb(0xa0,0x11);outb(0x21,0x20);outb(0xa1,0x28);
 outb(0x21,4);outb(0xa1,2);outb(0x21,1);outb(0xa1,1);
 outb(0x21,0xfe);outb(0xa1,0xff);
 system_timer.divisor=TIMER_DIVISOR;system_timer.frequency=OVICS_TIMER_HZ;
#if OVICS_TIMER_ROLLOVER_TEST
 /* Dedicated fixture only: simulated initial age for a real IRQ carry test. */
 system_timer.ticks=4294967040ull;system_timer.milliseconds=42950318326ull;system_timer.phase=426668u;
 system_timer.irq_count=(pm_u32)system_timer.ticks;system_timer.eoi_count=system_timer.irq_count;
#endif
 outb(0x43,0x34);outb(0x40,TIMER_DIVISOR&255);outb(0x40,TIMER_DIVISOR>>8);
 system_timer.ready=1;__asm__ volatile("sti":::"memory");
}
/* Small base-10 formatter: 64/32 division performed explicitly for freestanding
   i386, also used for HH:MM:SS without truncating uptime to 32 bits. */
static pm_u64 timer_divide(pm_u64 n,pm_u32 d,pm_u32 *rem) {
 pm_u64 q=0;pm_u32 r=0;
 for(int i=63;i>=0;i--){r=(r<<1)|(pm_u32)((n>>i)&1);if(r>=d){r-=d;q|=(pm_u64)1<<i;}}
 if(rem)*rem=r;return q;
}
static void print_u64(pm_u64 n){char b[21];pm_u32 at=0;do{pm_u32 r;n=timer_divide(n,10,&r);b[at++]=(char)('0'+r);}while(n);while(at)putchar(b[--at]);}
static void timer_print_info(void) {
 print("OVICS Timer Information\nTimer Source: PIT / IRQ0\nFrequency: ");print_number(system_timer.frequency);
 print(" Hz (requested)\nPIT Input: 1193182 Hz\nDivisor: ");print_number(system_timer.divisor);
 print("\nResolution: ");print(system_timer.frequency==1000?"approximately 1 ms\n":"approximately 10 ms\n");
 print("Ticks: ");print_u64(timer_get_ticks());print("\nUptime: ");print_u64(timer_get_uptime_ms());print(" ms\nIRQ Status: ");
 print(timer_watchdog_fault?"Degraded (IRQ stalled)\n":system_timer.ready?"Active\n":"Inactive\n");print("Saturated: ");print(system_timer.saturated?"Yes\n":"No\n");
}
static void timer_print_uptime(void) {
 pm_u32 seconds,minutes;pm_u64 total=timer_divide(timer_get_uptime_ms(),1000,0),m=timer_divide(total,60,&seconds),h=timer_divide(m,60,&minutes);
 print("OVICS Uptime\n");if(h<10)putchar('0');print_u64(h);putchar(':');if(minutes<10)putchar('0');print_number(minutes);putchar(':');if(seconds<10)putchar('0');print_number(seconds);print("\n");
}
