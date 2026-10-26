#ifndef OVICS_TIMER_H
#define OVICS_TIMER_H
#include "memory.h"
/* Approximately 1ms (1000Hz) or 10ms (100Hz) samples, never microsecond precision.
   Uptime starts when timer_init enables IRQ0 during kernel bootstrap.
   Saturates at UINT64_MAX. Reads preserve the caller's original IF state. */
void timer_init(void);
pm_u64 timer_get_ticks(void);
pm_u64 timer_get_uptime_ms(void);
#endif
