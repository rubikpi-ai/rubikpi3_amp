#ifndef _TIMER_H_
#define _TIMER_H_

int arch_mem_timer_select_frame(u32 frame_no);
u32 arch_mem_timer_active_spi(void);
u64 arch_mem_timer_active_base(void);
void arch_mem_timer_start_hz(u32 hz);
void arch_mem_timer_ack_and_rearm(void);
int arch_mem_timer_probe_frames(u32 hz, volatile u64 *shm, int include_frame0);

#endif // _TIMER_H_
