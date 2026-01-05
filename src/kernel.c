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

/* Global variable to store discovered CNTV PPI and configured Hz */
static u32 g_cntv_ppi = 0;
static u32 g_cntv_hz = 100;

/* GICR offsets for debug logging */
#define GICR_SGI_BASE   0x10000
#define GICR_ISPENDR0   (GICR_SGI_BASE + 0x0200)
#define GICR_ISACTIVER0 (GICR_SGI_BASE + 0x0300)

static inline u32 mmio_read32(u64 a) { return *(volatile u32 *)a; }

void arch_mem_timer_ack_and_rearm_hz(u32 hz);
void irq_handler(void) {
	u32 iar = gicv3_iar1();
	u32 intid = iar & 0x3ff;
	
	if (intid == 1023) return; /* spurious */
	
	/* Enhanced debug logging - record IRQ state to shared memory
	 * Layout:
	 *  shm[0]  = magic marker
	 *  shm[1]  = total IRQ count
	 *  shm[2]  = last intid
	 *  shm[3]  = timer tick count
	 *  shm[4]  = wfi count (set in main loop)
	 *  shm[10] = last GICR_ISPENDR0 value
	 *  shm[11] = last GICR_ISACTIVER0 value
	 */
	u64 gicr_base = 0x17b40000ULL; /* CPU7 GICR base */
	
	shm[0] = 0x4952514841444C52ULL; /* "IRQHANDLR" */
	shm[1]++;           /* total irq count */
	shm[2] = intid;     /* last intid */
	shm[10] = mmio_read32(gicr_base + GICR_ISPENDR0);
	shm[11] = mmio_read32(gicr_base + GICR_ISACTIVER0);
	
	/* Handle CNTV timer interrupt if it matches discovered PPI */
	if (g_cntv_ppi && intid == g_cntv_ppi) {
		shm[3]++;       /* timer tick count */
		timer_cntv_reload_hz(g_cntv_hz);
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
	u64 gicr_base = 0x17b40000ULL; /* CPU7 GICR */

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
	
	/* 
	 * IMPORTANT FIX: Do NOT enable all PPIs blindly!
	 * This was causing interrupt storms. Instead:
	 * 1. Disable all PPIs first
	 * 2. Discover which PPI corresponds to CNTV
	 * 3. Enable only that specific PPI
	 */
	// OLD CODE (caused interrupt storm):
	// enable_all_ppis(0x17b40000ULL);
	
	/* Ensure all timers are stopped */
	write_cntp_ctl_el0(0);
	write_cntv_ctl_el0(0);
	
	/* Disable all PPIs to start clean */
	disable_all_ppis(gicr_base);
	
	/* Ensure ICC settings are correct */
	__asm__ volatile("msr S3_0_C4_C6_0, %0" :: "r"(0xffUL));  /* PMR */
	__asm__ volatile("msr S3_0_C12_C12_7, %0"::"r"(1UL));     /* IGRPEN1 */

	for (volatile u64 i = 0; i < 1000000; i++) ;

	/* Phase 1: Discover which PPI corresponds to CNTV timer */
	shm[50] = 0x5048415345310000ULL; /* "PHASE1" marker */
	g_cntv_ppi = cntv_ppi_discover(gicr_base);
	shm[51] = g_cntv_ppi; /* Save discovered PPI for visibility */
	
	/* Phase 2: Configure timer frequency (default 100Hz, range 100-10000) */
	g_cntv_hz = 100; /* Can be changed to any value in range 100-10000 */
	shm[52] = g_cntv_hz;
	
	/* Phase 3: Enable only the discovered CNTV PPI */
	if (g_cntv_ppi) {
		shm[53] = 0x454E41424C450000ULL; /* "ENABLE" marker */
		enable_only_ppi(gicr_base, g_cntv_ppi, 0x80);
	} else {
		shm[53] = 0x4641494C45440000ULL; /* "FAILED" marker */
	}

	arch_local_irq_enable();

	/* Optional: Run diagnostic dump for debugging */
	for (volatile u64 i=0;i<5000000;i++);
	arch_ppi27_diag_dump();

	/* Phase 4: Start CNTV timer if PPI was discovered */
	if (g_cntv_ppi) {
		shm[54] = 0x535441525400ULL; /* "START" marker */
		timer_cntv_start_hz(g_cntv_hz);
		test[20] = 0x2020208;
		shm[21] = read_cntv_ctl_el0();
	}

	while (1) {
		__asm__ volatile ("wfi");
		shm[4] = shm[4] + 1;
	}


	return;
}
