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
	unsigned long *SHM_BASE = (unsigned long *)_shared_memory;
	SHM_BASE[14] = 0x88889999;

	SHM_BASE[15] = esr;
	SHM_BASE[16] = read_sysreg(far_el1);

	while (1)
		;
}

extern void cntv_timer_init_1ms(void);


#if 0

/* ---- basic sysregs ---- */
static inline u64 read_vbar_el1(void)
{
    u64 v;
    __asm__ volatile("mrs %0, vbar_el1" : "=r"(v));
    return v;
}

static inline u64 read_daif(void)
{
    u64 v;
    __asm__ volatile("mrs %0, daif" : "=r"(v));
    return v;
}

static inline u64 read_spsel(void)
{
    u64 v;
    __asm__ volatile("mrs %0, spsel" : "=r"(v));
    return v;
}

//static inline u64 read_sctlr_el1(void)
//{
//    u64 v;
//    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
//    return v;
//}

/* ---- Generic Timer (CNTV) ---- */
static inline u64 read_cntfrq_el0(void)
{
    u64 v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline u64 read_cntvct_el0(void)
{
    u64 v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static inline u32 read_cntv_ctl_el0(void)
{
    u64 v;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(v));
    return (u32)v;
}

static inline u32 read_cntv_tval_el0(void)
{
    u64 v;
    __asm__ volatile("mrs %0, cntv_tval_el0" : "=r"(v));
    return (u32)v;
}

/* ---- GICv3 CPU interface (sysreg encoding form, toolchain-safe) ----
   ICC_SRE_EL1   S3_0_C12_C12_5
   ICC_PMR_EL1   S3_0_C4_C6_0
   ICC_IGRPEN1   S3_0_C12_C12_7
*/
static inline u64 read_icc_sre_el1(void)
{
    u64 v;
    __asm__ volatile("mrs %0, S3_0_C12_C12_5" : "=r"(v));
    return v;
}

static inline u64 read_icc_pmr_el1(void)
{
    u64 v;
    __asm__ volatile("mrs %0, S3_0_C4_C6_0" : "=r"(v));
    return v;
}

static inline u64 read_icc_igrpen1_el1(void)
{
    u64 v;
    __asm__ volatile("mrs %0, S3_0_C12_C12_7" : "=r"(v));
    return v;
}

/*
 * Dump important interrupt/timer state to shared memory.
 * shm layout (u64 slots):
 *  [0] magic
 *  [1] SCTLR_EL1
 *  [2] VBAR_EL1
 *  [3] DAIF
 *  [4] SPSel
 *  [5] CNTFRQ_EL0
 *  [6] CNTVCT_EL0
 *  [7] CNTV_CTL_EL0 (low 32 bits)
 *  [8] CNTV_TVAL_EL0 (low 32 bits)
 *  [9] ICC_SRE_EL1
 * [10] ICC_PMR_EL1
 * [11] ICC_IGRPEN1_EL1
 */
void dump_irq_state(u64 magic)
{
    volatile u64 *shm = (volatile u64 *)_shared_memory;

    shm[0]  = magic;
    shm[1]  = read_sctlr_el1();
    shm[2]  = read_vbar_el1();
    shm[3]  = read_daif();
    shm[4]  = read_spsel();
    shm[5]  = read_cntfrq_el0();
    shm[6]  = read_cntvct_el0();
    shm[7]  = (u64)read_cntv_ctl_el0();
    shm[8]  = (u64)read_cntv_tval_el0();
    shm[9]  = read_icc_sre_el1();
    shm[10] = read_icc_pmr_el1();
    shm[11] = read_icc_igrpen1_el1();
}

#endif


//static inline void isb(void){ __asm__ volatile("isb" ::: "memory"); }

static inline void write_icc_sre_el1(u64 v){ __asm__ volatile("msr S3_0_C12_C12_5, %0"::"r"(v)); }
static inline u64 read_icc_sre_el1(void){ u64 v; __asm__ volatile("mrs %0, S3_0_C12_C12_5":"=r"(v)); return v; }

static inline void write_icc_pmr_el1(u64 v){ __asm__ volatile("msr S3_0_C4_C6_0, %0"::"r"(v)); }
static inline u64 read_icc_pmr_el1(void){ u64 v; __asm__ volatile("mrs %0, S3_0_C4_C6_0":"=r"(v)); return v; }

static inline void write_icc_igrpen0_el1(u64 v){ __asm__ volatile("msr S3_0_C12_C12_6, %0"::"r"(v)); }
static inline u64 read_icc_igrpen0_el1(void){ u64 v; __asm__ volatile("mrs %0, S3_0_C12_C12_6":"=r"(v)); return v; }

static inline void write_icc_igrpen1_el1(u64 v){ __asm__ volatile("msr S3_0_C12_C12_7, %0"::"r"(v)); }
static inline u64 read_icc_igrpen1_el1(void){ u64 v; __asm__ volatile("mrs %0, S3_0_C12_C12_7":"=r"(v)); return v; }

void probe_gic_cpuif(void)
{
    volatile u64 *shm = (volatile u64 *)_shared_memory;

    write_icc_sre_el1(1);
    isb();

    write_icc_pmr_el1(0xFF);
    isb();

    write_icc_igrpen0_el1(1);
    isb();

    write_icc_igrpen1_el1(1);
    isb();

	shm[1] = 0x30303030;
    shm[2] = read_icc_sre_el1();
    shm[3] = read_icc_pmr_el1();
    shm[4] = read_icc_igrpen0_el1();
    shm[5] = read_icc_igrpen1_el1();
}

static inline u64 read_pfr0(void){ u64 v; asm volatile("mrs %0, ID_AA64PFR0_EL1":"=r"(v)); return v; }
void kernel_main(void)
{
	unsigned long *SHM_BASE = (unsigned long *)_shared_memory;

	mem_init(0, 0);

	paging_init();

	// *SHM_BASE = 0x10101010;

	// SHM_BASE[10] = read_sctlr_el1();
	// SHM_BASE[11] = read_currentel();

	gpio_pinmux_set(14, mux_gpio);
	gpio_direction_output(14, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

	// SHM_BASE[12] = read_mpidr_el1();
//	cntv_timer_init_1ms();
	for (volatile u64 i = 0; i < 1000000; i++) ;
	//dump_irq_state(0x20202020);

	probe_gic_cpuif();
	SHM_BASE[6] = read_currentel();
	SHM_BASE[7] = read_pfr0();
	 *SHM_BASE = 0x20202020;


	return;
}
