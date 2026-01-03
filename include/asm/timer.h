#ifndef _TIMER_H_
#define _TIMER_H_

void timer_start_hz(u32 hz);
void timer_reload_hz(u32 hz);

void arch_mem_timer_start_hz(u32 hz);
void arch_mem_timer_ack_and_rearm_hz(u32 hz);

#endif // _TIMER_H_
