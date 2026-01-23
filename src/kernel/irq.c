#include <type.h>
#include <irq.h>
#include <gic_v3.h>
#include <sysregs.h>
#include <timer.h>

static inline void psci_cpu_off(void)
{
	asm volatile(
		"ldr x0, =0x84000002\n"
		"smc #0\n"
		::: "x0", "x1", "x2", "x3"
	);
}

void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	volatile u64 *shm = (volatile u64 *)SHM_BASE;

	/* Record exception information */
	shm[14] = 0x88889999;
	shm[15] = esr;
	shm[16] = read_sysreg(far_el1);
	shm[17] = reason;

	/* Disable interrupts immediately */
	arch_local_irq_disable();

	/* Call PSCI CPU_OFF to gracefully shutdown this CPU */
	/* This prevents the exception from affecting Linux */
	psci_cpu_off();

	/* Should never reach here */
	while (1)
		;
}

u64 jiffies = 0;

void irq_handler(void)
{
	volatile u64 *shm = (volatile u64 *)SHM_BASE;
	u32 iar = gicv3_iar1();
	u32 intid = iar & 0x3ff;

	shm[0] = 0x1111222233334444ULL;
	shm[1]++;           /* total irq count */
	shm[2] = intid;     /* last intid */

	if (intid == 0x1b) {
		shm[3]++;       /* timer tick count */
	//	jiffies++;
		timer_cntv_reload_hz(1000);
	}

	gicv3_eoi1(iar);
}
