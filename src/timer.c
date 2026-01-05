#include <asm/pgtable.h>
#include <asm/pgtable_prot.h>
#include <asm/pgtable_hwdef.h>
#include <asm/sysregs.h>
#include <asm/barrier.h>
#include <string.h>
#include <mm.h>
#include <mmu.h>
#include <page_alloc.h>
#include <asm/gpio.h>
#include <type.h>
#include <asm/irq.h>

/* Virtual timer CNTV */
static inline void write_cntv_tval_el0(u64 v) {
	__asm__ volatile ("msr cntv_tval_el0, %0" :: "r"(v));
}
static inline void write_cntv_ctl_el0(u64 v) {
	__asm__ volatile ("msr cntv_ctl_el0, %0" :: "r"(v));
}
static inline u64 read_cntv_ctl_el0(void) {
	u64 v; __asm__ volatile ("mrs %0, cntv_ctl_el0" : "=r"(v)); return v;
}
static inline u64 read_cntv_tval_el0(void) {
	u64 v; __asm__ volatile ("mrs %0, cntv_tval_el0" : "=r"(v)); return v;
}
static inline u64 read_cntv_cval_el0(void) {
	u64 v; __asm__ volatile ("mrs %0, cntv_cval_el0" : "=r"(v)); return v;
}

/* CNTP for EL1 Non-secure physical timer interrupt (your arch_timer uses it) */
static inline u64 read_cntfrq_el0(void) {
	u64 v; __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(v)); return v;
}
static inline void write_cntp_tval_el0(u64 v) {
	__asm__ volatile ("msr cntp_tval_el0, %0" :: "r"(v));
}
static inline void write_cntp_ctl_el0(u64 v) {
	__asm__ volatile ("msr cntp_ctl_el0, %0" :: "r"(v));
}

/* bit0 ENABLE, bit1 IMASK (1=mask), bit2 ISTATUS (RO) */
#define CNTP_CTL_ENABLE (1U << 0)
#define CNTP_CTL_IMASK  (1U << 1)

void timer_cntp_start_hz(u32 hz)
{
	u64 frq = read_cntfrq_el0();
	u64 ticks = frq / hz;

	/* program timer first */
	write_cntp_tval_el0(ticks);
	/* enable + unmask */
	write_cntp_ctl_el0(CNTP_CTL_ENABLE); /* IMASK=0 */
}

void timer_cntp_reload_hz(u32 hz)
{
	u64 frq = read_cntfrq_el0();
	u64 ticks = frq / hz;
	write_cntp_tval_el0(ticks);
}

void timer_cntv_start_hz(u32 hz)
{
	u64 frq = read_cntfrq_el0();
	u64 ticks = frq / hz;
	write_cntv_tval_el0(ticks);
	write_cntv_ctl_el0(CNTP_CTL_ENABLE);
}

void timer_cntv_reload_hz(u32 hz)
{
	u64 frq = read_cntfrq_el0();
	u64 ticks = frq / hz;
	write_cntv_tval_el0(ticks);
}
