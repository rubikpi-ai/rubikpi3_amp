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

void arch_mem_timer_ack_and_rearm_hz(u32 hz);
void irq_handler(void) {
	u32 iar = gicv3_iar1();
	u32 intid = iar & 0x3ff;
	//u32 t = arch_mem_timer_active_spi();

	if (intid == 1023) return; /* spurious */

	shm[0] = 0x1111222233334444ULL;
	shm[1]++;           /* total irq count */
	shm[2] = intid;     /* last intid */

	if (intid == 0x26) {
		shm[3]++;       /* timer tick count */
	//	arch_mem_timer_ack_and_rearm_hz(10);
		timer_cntv_reload_hz(10);
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

void arch_mem_timer_cntacr_enable_all(volatile u64 *shm);
void arch_ppi27_diag_dump(void);
void enable_ppi27(u64 gicr_base);
void enable_all_ppis(u64 gicr_base);

static inline u64 read_cntv_ctl_el0(void)
{
    u64 v; __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(v)); return v;
}

static inline void write_cntp_ctl_el0(u64 v){ __asm__ volatile("msr cntp_ctl_el0, %0"::"r"(v)); }
static inline void write_cntv_ctl_el0(u64 v) {
    __asm__ volatile ("msr cntv_ctl_el0, %0" :: "r"(v));
}
void timer_cntv_start_hz(u32 hz);
void timer_start_hz(u32 hz);
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
//	enable_ppi27(0x17b40000ULL);
	enable_all_ppis(0x17b40000ULL);
	__asm__ volatile("msr S3_0_C4_C6_0, %0" :: "r"(0xffUL));
	__asm__ volatile("msr S3_0_C12_C12_7, %0"::"r"(1UL));
	// el1_full_gic_init();
//	el1_gicd_spi_init();

	for (volatile u64 i = 0; i < 1000000; i++) ;

	//arch_mem_timer_start_hz(10);
	//arch_mem_timer_cntacr_enable_all(shm);
	//arch_mem_timer_probe_frames(10, shm, 0);
	//arch_mem_timer_find_and_start(10, shm);


	arch_local_irq_enable();

	//write_cntp_ctl_el0(0);
	//timer_start_hz(10);
	//for (volatile u64 i=0;i<5000000;i++);
	//arch_ppi_diag_dump();
	//shm[21] = read_cntv_ctl_el0();

	write_cntp_ctl_el0(0);
	write_cntv_ctl_el0(0);
	timer_cntv_start_hz(10);
	for (volatile u64 i=0;i<5000000;i++);
	arch_ppi27_diag_dump();

	test[20] = 0x2020208;
	shm[21] = read_cntv_ctl_el0();

	while (1) {
		__asm__ volatile ("wfi");
		shm[4] = shm[4] + 1;
	}


	return;
}
