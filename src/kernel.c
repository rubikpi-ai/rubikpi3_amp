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
#include <asm/ptregs.h>
#include <asm/irq.h>
#include <asm/gic_v3.h>
#include <asm/timer.h>

extern char _shared_memory[];
extern char _stack_bottom[];

static inline unsigned long read_sctlr_el1(void)
{
	unsigned long v;
	asm volatile("mrs %0, sctlr_el1" : "=r"(v));
	return v;
}

static inline unsigned long read_currentel(void)
{
	unsigned long v;
	asm volatile("mrs %0, CurrentEL" : "=r"(v));
	return v;
}

static inline unsigned long read_sp(void) {
	unsigned long v;
	asm volatile("mov %0, sp" : "=r"(v));
	return v;
}

static inline unsigned long read_mpidr_el1(void)
{
	unsigned long v;
	__asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v));
	return v;
}

void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	unsigned long *test = SHM_BASE;
	test[14] = 0x88889999;

	test[15] = esr;
	test[16] = read_sysreg(far_el1);

	while (1)
		;
}

static volatile u64 * const shm = (volatile u64 *)SHM_BASE;

void irq_handler(void) {
	u32 iar = gicv3_iar1();
	u32 intid = iar & 0x3ff;

	if (intid == 1023) return; /* spurious */

	shm[0] = 0x1111222233334444ULL;
	shm[1]++;           /* total irq count */
	shm[2] = intid;     /* last intid */

	if (intid == 6) {
		shm[3]++;       /* timer tick count */
		arch_mem_timer_ack_and_rearm_hz(10);
	}

	gicv3_eoi1(iar);
}

static inline void psci_cpu_off(void)
{
	register unsigned long x0 asm("x0") = 0x84000002; // CPU_OFF
	asm volatile("smc #0" : : "r"(x0) : "memory");
	while (1) asm volatile("wfi");
}
#define AMP_CMD_IDX  32
#define AMP_CMD_RESET 0x52534554ULL /* 'RSET' */

extern char vectors[];
static inline void write_vbar_el1(u64 v)
{
	asm volatile("msr vbar_el1, %0" :: "r"(v));
	asm volatile("isb" ::: "memory");
}

void kernel_main(void)
{
	volatile u64 *shm = (volatile u64 *)0xD7C00000;
	unsigned long *test = SHM_BASE;

	write_vbar_el1((u64)vectors);
	arch_local_irq_disable();
	shm[1] = read_mpidr_el1();

	mem_init(0, 0);

	paging_init();

	gpio_pinmux_set(14, mux_gpio);
	gpio_direction_output(14, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

	gicv3_init_for_cpu();

	for (volatile u64 i = 0; i < 1000000; i++) ;

	arch_mem_timer_start_hz(10);
	//arch_mem_timer_find_and_start(10, shm);

	for (volatile u64 i=0;i<5000000;i++);

	arch_local_irq_enable();

	*test = 0x2020207;

	while (1) {
		__asm__ volatile ("wfi");
		shm[4] = shm[4] + 1;
	}


	return;
}
