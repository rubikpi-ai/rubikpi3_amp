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

void gicv3_init_for_cpu(void);
void gicv3_enable_ppi(u32 intid, u8 prio);
u32 gicv3_iar1(void);
void gicv3_eoi1(u32 iar);

void timer_start_hz(u32 hz);
void timer_reload_hz(u32 hz);

static volatile u64 * const shm = (volatile u64 *)SHM_BASE;

void irq_handler(void) {
    u32 iar = gicv3_iar1();
    u32 intid = iar & 0x3ff;

    if (intid == 1023) return; /* spurious */

    shm[0] = 0x1111222233334444ULL;
    shm[1]++;           /* total irq count */
    shm[2] = intid;     /* last intid */

    if (intid == TIMER_PPI_ID) {
        shm[3]++;       /* timer tick count */
        timer_reload_hz(10);
    }

    gicv3_eoi1(iar);
}

void kernel_main(void)
{
	arch_local_irq_disable();

	unsigned long *test = SHM_BASE;

	mem_init(0, 0);

	paging_init();

	// *SHM_BASE = 0x10101010;

	// SHM_BASE[10] = read_sctlr_el1();
	// SHM_BASE[11] = read_currentel();

	gpio_pinmux_set(14, mux_gpio);
	gpio_direction_output(14, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

	gicv3_init_for_cpu();

	/* enable timer PPI on this CPU */
	gicv3_enable_ppi(TIMER_PPI_ID, 0x80);

	/* start timer */
	timer_start_hz(10);

	// SHM_BASE[12] = read_mpidr_el1();
	for (volatile u64 i = 0; i < 1000000; i++) ;
	//dump_irq_state(0x20202020);


	arch_local_irq_enable();

	 *test = 0x20202020;

	while (1) {
		__asm__ volatile ("wfi");
	}


	return;
}
