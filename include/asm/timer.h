#ifndef _TIMER_H_
#define _TIMER_H_

void timer_cntp_start_hz(u32 hz);
void timer_cntp_reload_hz(u32 hz);
void timer_cntv_start_hz(u32 hz);
void timer_cntv_reload_hz(u32 hz);

#endif // _TIMER_H
