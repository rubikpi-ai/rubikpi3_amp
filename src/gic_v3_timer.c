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

/* From your DT + dmesg stride */
#define GICR_BASE_CPU7   0x17B40000UL
#define GICR_RD_OFF      0x00000UL
#define GICR_SGI_OFF     0x10000UL

#define GICR_WAKER       (GICR_RD_OFF  + 0x0014)

#define GICR_IGROUPR0    (GICR_SGI_OFF + 0x0080)
#define GICR_ISENABLER0  (GICR_SGI_OFF + 0x0100)
#define GICR_IPRIORITYR  (GICR_SGI_OFF + 0x0400)
#define GICR_ICFGR1      (GICR_SGI_OFF + 0x0C04) /* INTID 16..31 */

#define PPI_CNTV         11U
#define INTID_CNTV       (16U + PPI_CNTV) /* 27 */

static inline void mmio_w32(uintptr_t addr, u32 v) { *(volatile u32 *)addr = v; }
static inline u32 mmio_r32(uintptr_t addr)         { return *(volatile u32 *)addr; }

/* GICv3 CPU interface sysregs */
//static inline void write_icc_pmr_el1(u64 v)     { __asm__ volatile("msr icc_pmr_el1, %0" :: "r"(v)); }
//static inline void write_icc_igrpen1_el1(u64 v) { __asm__ volatile("msr icc_igrpen1_el1, %0" :: "r"(v)); }
//static inline u64 read_icc_iar1_el1(void)       { u64 v; __asm__ volatile("mrs %0, icc_iar1_el1" : "=r"(v)); return v; }
//static inline void write_icc_eoir1_el1(u64 v)   { __asm__ volatile("msr icc_eoir1_el1, %0" :: "r"(v)); }

static inline void enable_irq_daif(void)             { __asm__ volatile("msr daifclr, #2" ::: "memory"); }

static inline void write_icc_pmr_el1(u64 v)
{
	__asm__ volatile("msr S3_0_C4_C6_0, %0" :: "r"(v));
}

static inline void write_icc_igrpen1_el1(u64 v)
{
	__asm__ volatile("msr S3_0_C12_C12_7, %0" :: "r"(v));
}

static inline u64 read_icc_iar1_el1(void)
{
	u64 v;
	__asm__ volatile("mrs %0, S3_0_C12_C12_0" : "=r"(v));
	return v;
}

static inline void write_icc_eoir1_el1(u64 v)
{
	__asm__ volatile("msr S3_0_C12_C12_1, %0" :: "r"(v));
}

/* Generic Timer (CNTV) */
static inline u64 read_cntfrq_el0(void)         { u64 v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
static inline void write_cntv_tval_el0(u32 v)   { __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(v)); }
static inline void write_cntv_ctl_el0(u32 v)    { __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(v)); }

#define CNTV_CTL_ENABLE  (1U << 0)
#define CNTV_CTL_IMASK   (1U << 1)
#define CNTV_CTL_ISTATUS (1U << 2)

static void gicr_wait_awake(uintptr_t gicr_base)
{
    u32 w = mmio_r32(gicr_base + GICR_WAKER);
    w &= ~(1U << 1); /* ProcessorSleep=0 */
    mmio_w32(gicr_base + GICR_WAKER, w);
    dsb(sy);
    while (mmio_r32(gicr_base + GICR_WAKER) & (1U << 2)) {
        /* wait ChildrenAsleep==0 */
    }
}

static void gicr_enable_intid27_cntv(uintptr_t gicr_base)
{
    /* Put INTID27 into Group1 (bit=1). Usually already, but set it. */
    u32 ig = mmio_r32(gicr_base + GICR_IGROUPR0);
    ig |= (u32)BIT(INTID_CNTV);
    mmio_w32(gicr_base + GICR_IGROUPR0, ig);

    /* Force level-sensitive for INTID 16..31 via ICFGR1 */
    u32 icfgr1 = mmio_r32(gicr_base + GICR_ICFGR1);
    unsigned int n = (INTID_CNTV - 16U);        /* 0..15 */
    icfgr1 &= ~(2U << (n * 2));                 /* 0b00 = level */
    mmio_w32(gicr_base + GICR_ICFGR1, icfgr1);

    /* Priority (0x80 mid) */
    volatile u8 *prio = (volatile u8 *)(gicr_base + GICR_IPRIORITYR);
    prio[INTID_CNTV] = 0x80;

    /* Enable INTID27 */
    mmio_w32(gicr_base + GICR_ISENABLER0, (u32)BIT(INTID_CNTV));
    dsb(sy);
}

extern char _shared_memory[];
static inline u64 read_icc_igrpen1_el1(void)
{
    u64 v;
    __asm__ volatile("mrs %0, S3_0_C12_C12_7" : "=r"(v));
    return v;
}
static void gic_cpuif_enable_group1(void)
{
	unsigned long *SHM_BASE = (unsigned long *)_shared_memory;
    write_icc_pmr_el1(0xFF);      /* allow all priorities */
    isb();
    write_icc_igrpen1_el1(1);     /* enable Group1 */
    isb();

	SHM_BASE[20] = read_icc_igrpen1_el1();


}

void cntv_timer_init_1ms(void)
{
    uintptr_t gicr = (uintptr_t)GICR_BASE_CPU7;

    gicr_wait_awake(gicr);
    gicr_enable_intid27_cntv(gicr);

    gic_cpuif_enable_group1();

    u64 freq = read_cntfrq_el0();
    u32 tval = (u32)(freq / 1000ULL);

    write_cntv_tval_el0(tval);
    write_cntv_ctl_el0(CNTV_CTL_ENABLE); /* IMASK=0 */

    enable_irq_daif();
}

/* Called from IRQ vector */
void irq_handle(void)
{
	u32 iar = (u32)read_icc_iar1_el1();
	u32 intid = iar & 0x3FFU;
	unsigned long *SHM_BASE = (unsigned long *)_shared_memory;
	SHM_BASE[13] = 0x77889911;

    if (intid == INTID_CNTV) {
        /* re-arm */
        u64 freq = read_cntfrq_el0();
        write_cntv_tval_el0((u32)(freq / 1000ULL));
        write_icc_eoir1_el1(iar);
        return;
    }

    /* EOI other/spurious */
    write_icc_eoir1_el1(iar);
}
