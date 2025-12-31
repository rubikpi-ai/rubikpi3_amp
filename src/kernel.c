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








static inline u64 read_CurrentEL(void)
{
	u64 v;
	__asm__ volatile("mrs %0, CurrentEL" : "=r"(v));
	return v;
}

static inline u64 read_DAIF(void)
{
	u64 v;
	__asm__ volatile("mrs %0, DAIF" : "=r"(v));
	return v;
}

static inline u64 read_MPIDR_EL1(void)
{
	u64 v;
	__asm__ volatile("mrs %0, MPIDR_EL1" : "=r"(v));
	return v;
}

static inline u64 read_SPSel(void)
{
	u64 v;
	__asm__ volatile("mrs %0, SPSel" : "=r"(v));
	return v;
}

static inline u64 read_SP_EL0(void)
{
	u64 v;
	__asm__ volatile("mrs %0, SP_EL0" : "=r"(v));
	return v;
}

static inline u64 read_SP_EL1(void)
{
	u64 v;
	__asm__ volatile("mrs %0, SP_EL1" : "=r"(v));
	return v;
}

/* Exception state (EL1) */
static inline u64 read_ESR_EL1(void)
{
	u64 v;
	__asm__ volatile("mrs %0, ESR_EL1" : "=r"(v));
	return v;
}

static inline u64 read_ELR_EL1(void)
{
	u64 v;
	__asm__ volatile("mrs %0, ELR_EL1" : "=r"(v));
	return v;
}

static inline u64 read_FAR_EL1(void)
{
	u64 v;
	__asm__ volatile("mrs %0, FAR_EL1" : "=r"(v));
	return v;
}

static inline u64 read_SPSR_EL1(void)
{
	u64 v;
	__asm__ volatile("mrs %0, SPSR_EL1" : "=r"(v));
	return v;
}

/* Timer */
static inline u64 read_CNTFRQ_EL0(void)
{
	u64 v;
	__asm__ volatile("mrs %0, CNTFRQ_EL0" : "=r"(v));
	return v;
}

static inline u64 read_CNTPCT_EL0(void)
{
	u64 v;
	__asm__ volatile("mrs %0, CNTPCT_EL0" : "=r"(v));
	return v;
}

static inline u64 read_CNTP_CVAL_EL0(void)
{
	u64 v;
	__asm__ volatile("mrs %0, CNTP_CVAL_EL0" : "=r"(v));
	return v;
}

static inline u64 read_CNTP_TVAL_EL0(void)
{
	u64 v;
	__asm__ volatile("mrs %0, CNTP_TVAL_EL0" : "=r"(v));
	return v;
}

static inline u64 read_CNTP_CTL_EL0(void)
{
	u64 v;
	__asm__ volatile("mrs %0, CNTP_CTL_EL0" : "=r"(v));
	return v;
}

/*
 * Optional: try-read GIC ICC registers. WARNING: may UNDEF/trap on your platform.
 * Use ONLY after you have a sync exception handler that records ESR/ELR/FAR.
 */
static inline u64 read_ICC_SRE_EL1_unsafe(void)
{
	u64 v;
	__asm__ volatile("mrs %0, S3_0_C12_C12_5" : "=r"(v)); /* ICC_SRE_EL1 */
	return v;
}

static inline u64 read_ICC_IGRPEN1_EL1_unsafe(void)
{
	u64 v;
	__asm__ volatile("mrs %0, S3_0_C12_C12_7" : "=r"(v)); /* ICC_IGRPEN1_EL1 */
	return v;
}

/* Dump a basic snapshot to shm */
static inline void dump_basic_regs(volatile u64 *shm, u32 base_index)
{
	shm[base_index + 0]  = 0x524547444d504d50ULL; /* 'REGDMPMP' marker-ish */
	shm[base_index + 1]  = read_CurrentEL();
	shm[base_index + 2]  = read_DAIF();
	shm[base_index + 3]  = read_MPIDR_EL1();
	shm[base_index + 4]  = read_SPSel();
	shm[base_index + 5]  = read_SP_EL0();
	shm[base_index + 6]  = read_SP_EL1();

	/* timer snapshot */
	shm[base_index + 8]  = read_CNTFRQ_EL0();
	shm[base_index + 9]  = read_CNTPCT_EL0();
	shm[base_index + 10] = read_CNTP_TVAL_EL0();
	shm[base_index + 11] = read_CNTP_CVAL_EL0();
	shm[base_index + 12] = read_CNTP_CTL_EL0();

	/* unsafe ICC reads (comment out until sync handler is confirmed) */
	/* shm[base_index + 16] = read_ICC_SRE_EL1_unsafe(); */
	/* shm[base_index + 17] = read_ICC_IGRPEN1_EL1_unsafe(); */
}






static inline void psci_cpu_off(void)
{
    register unsigned long x0 asm("x0") = 0x84000002; // CPU_OFF
    asm volatile("smc #0" : : "r"(x0) : "memory");
    while (1) asm volatile("wfi");
}
#define AMP_CMD_IDX  32
#define AMP_CMD_RESET 0x52534554ULL /* 'RSET' */

void kernel_main(void)
{
	volatile u64 *shm = (volatile u64 *)0xD7C00000;
	unsigned long *test = SHM_BASE;

	dump_basic_regs(shm, 0);
	arch_local_irq_disable();


	mem_init(0, 0);

	paging_init();

	gpio_pinmux_set(14, mux_gpio);
	gpio_direction_output(14, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

	gicv3_init_for_cpu();

	/* enable timer PPI on this CPU */
	gicv3_enable_ppi(TIMER_PPI_ID, 0x80);

	/* start timer */
	timer_start_hz(10);

	for (volatile u64 i = 0; i < 1000000; i++) ;

	arch_local_irq_enable();

	 *test = 0x20202020;

	while (1) {
		if (shm[AMP_CMD_IDX] == AMP_CMD_RESET) {
			shm[AMP_CMD_IDX] = 0;
			psci_cpu_off(); /* 不会返回 */
		}
		__asm__ volatile ("wfi");
	}


	return;
}
