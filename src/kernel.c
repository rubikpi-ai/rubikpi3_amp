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
#include <printk.h>
#include <uart_geni.h>
#include <clock.h>
#include <clock_qcs6490.h>

#define AMP_CMD_IDX  32
#define AMP_CMD_RESET 0x52534554ULL /* 'RSET' */

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

static inline void psci_cpu_off(void)
{
	register unsigned long x0 asm("x0") = 0x84000002; // CPU_OFF
	asm volatile("smc #0" : : "r"(x0) : "memory");
	while (1) asm volatile("wfi");
}

void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	volatile u64 *shm = (volatile u64 *)0xD7C00000;

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

static volatile u64 * const shm = (volatile u64 *)SHM_BASE;

u64 jiffies = 0;

void irq_handler(void)
{
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

//int uart2_init(unsigned int baud, unsigned long src_clk_hz, unsigned int clk_sel);
void uart2_init(void);
void uart2_debug_dump_and_try_tx(volatile u64 *shm, u32 shm_base_idx, const char *msg);
int uart2_set_baud_linux_style(u32 baud);

void kernel_main(void)
{
	volatile u64 *shm = (volatile u64 *)0xD7C00000;
	unsigned long *test = (unsigned long *)SHM_BASE;
	u64 gicr = 0x17b40000ULL;

	arch_local_irq_disable();
	shm[1] = read_mpidr_el1();

	mem_init(0, 0);

	paging_init();

	uart2_init();
//	uart2_init(115200, 19200000, 0);
	uart2_puts("uart2 hello rubikpi 123\n");

	// clk_set_rate(UART2_CLK, 115200 * 16); /* 16x oversampling */

	gpio_pinmux_set(14, mux_gpio);
	gpio_direction_output(14, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

//	uart2_debug_dump_and_try_tx(shm, 200, "0123456789ABCDEF");
	//uart2_puts("hello world\n");

	gicv3_init_for_cpu();
	enable_ppi(gicr, 27, 0x40);
	write_cntv_ctl_el0(0);

	for (volatile u64 i = 0; i < 1000000; i++) ;

	arch_local_irq_enable();
	timer_cntv_start_hz(1000);
	for (volatile u64 i=0;i<5000000;i++);

	test[20] = 0x2020208;

	//printk("kernel_main: CPU %d, SCTLR_EL1=0x%x, CurrentEL=0x%x, SP=0x%x\n",
	//       read_mpidr_el1() & 0xff,
	//       read_sctlr_el1(),
	//       read_currentel(),
	//       read_sp());

	while (1) {
		__asm__ volatile ("wfi");
		shm[4] = shm[4] + 1;

		/* Check for reset command from Linux */
		if (shm[AMP_CMD_IDX] == AMP_CMD_RESET) {
			/* Disable interrupts before CPU_OFF */
			arch_local_irq_disable();

			/* Call PSCI CPU_OFF to power down this CPU */
			psci_cpu_off();

			/* Should never reach here */
			while (1)
				;
		}
	}

	return;
}
