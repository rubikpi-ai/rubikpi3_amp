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

static inline u32 mmio_read32(u64 addr) { return *(volatile u32 *)addr; }
static inline void mmio_write32(u64 addr, u32 v) { *(volatile u32 *)addr = v; }
static inline u64 mmio_read64(u64 addr) { return *(volatile u64 *)addr; }
static inline void mmio_write64(u64 addr, u64 v) { *(volatile u64 *)addr = v; }

static inline u64 read_mpidr_el1(void) {
    u64 v;
    __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v));
    return v;
}

#if 0
/* Aff0 is core index inside cluster in typical Qualcomm topology.
 * Your U-Boot uses mpidr=0x700 for CPU7 => aff0=7.
 */
static inline u32 mpidr_aff0(u64 mpidr) {
    return (u32)(mpidr & 0xffU);
}

static u64 gicr_base_this_cpu(void) {
    u64 mpidr = read_mpidr_el1();
    u32 aff0 = mpidr_aff0(mpidr);
    return GICR_BASE + ((u64)aff0) * GICR_STRIDE;
}
#endif

/* ... keep your includes ... */

#define GICR_TYPER              0x0008
#define GICR_WAKER              0x0014

/* QCS6490 DTS: GICR region size = 0x100000 for 8 CPUs */
#define GICR_REGION_SIZE        0x100000UL
#define GICR_RD_STRIDE          0x20000UL
#define GICR_RD_FRAMES          (GICR_REGION_SIZE / GICR_RD_STRIDE)

/*
 * On many GICv3 implementations:
 *  - GICR_TYPER.Affinity is in bits [32:63]
 *  - bit[4] is "Last"
 *
 * Your dump shows low values (0x0,0x100,...,0x710), which suggests at least
 * some affinity/last encoding is visible in low bits on this platform.
 *
 * To be robust, we try both:
 *  A) affinity in upper bits (common)
 *  B) affinity-like pattern in lower bits (matches your dump)
 */
static inline u64 mpidr_to_aff(u64 mpidr)
{
    u64 aff0 = (mpidr >> 0) & 0xff;
    u64 aff1 = (mpidr >> 8) & 0xff;
    u64 aff2 = (mpidr >> 16) & 0xff;
    u64 aff3 = (mpidr >> 32) & 0xff;
    return (aff3 << 24) | (aff2 << 16) | (aff1 << 8) | aff0;
}

#if 0
static u64 gicr_base_this_cpu(void)
{
    u64 mpidr = read_mpidr_el1();
    u64 want_aff = mpidr_to_aff(mpidr);

    for (u32 i = 0; i < GICR_RD_FRAMES; i++) {
        u64 rd = GICR_BASE + (u64)i * GICR_RD_STRIDE;
        u64 typer = mmio_read64(rd + GICR_TYPER);

        /* Variant A (architected, common): affinity in [63:32] */
        u64 aff_a = (typer >> 32) & 0xffffffffULL;

        /* Variant B (your dump indicates something like i*0x100 in low bits) */
        u64 aff_b = (typer & 0xff00ULL) >> 8;  /* yields 0..7 for 0x000,0x100,...,0x700 */
        u64 want_b = want_aff & 0xff;          /* want Aff0 */

        if (aff_a == want_aff || aff_b == want_b) {
            return rd;
        }
    }

    /* Fallback: if not found, default to frame0 (better than wild write) */
    return GICR_BASE;
}
#endif

static u64 gicr_base_this_cpu(void) {
    return 0x17B40000ULL; /* GICR_BASE + 7*0x20000 */
}

/*
 * NOTE:
 * ICC_* system register helpers are provided by include/sysreg.h using
 * encoded names (S3_0_C*_C*_*) to avoid toolchains that don't recognize
 * the ICC_* aliases.
 *
 * Used functions/macros from sysreg.h:
 *  - read_icc_sre_el1 / write_icc_sre_el1
 *  - write_icc_pmr_el1 / write_icc_bpr1_el1 / write_icc_ctlr_el1 / write_icc_igrpen1_el1
 *  - read_icc_iar1_el1
 *  - write_icc_eoir1_el1 / write_icc_dir_el1
 */

u32 gicv3_iar1(void) {
    return read_icc_iar1_el1();
}

void gicv3_eoi1(u32 iar) {
    write_icc_eoir1_el1(iar);
    write_icc_dir_el1(iar);
    isb();
}

/* Public init */
void gicv3_init_for_cpu(void) {
	volatile u64 *shm = (volatile u64 *)0xD7C00000;
    u64 rd = gicr_base_this_cpu();

    /* Wake redistributor */
    u64 waker = rd + 0x0014; /* GICR_WAKER */
    u32 v = mmio_read32(waker);
    v &= ~(1U << 1); /* ProcessorSleep=0 */
    mmio_write32(waker, v);



    while (mmio_read32(waker) & (1U << 2)) { /* ChildrenAsleep */ }

    /* Enable System Register Interface */
    u64 sre = read_icc_sre_el1();
    /* SRE=1, DIB=0, DFB=0; keep other bits */
    sre |= 1U;
    write_icc_sre_el1(sre);
    isb();

    /* CPU interface config */
    write_icc_pmr_el1(0xFF); /* allow all priorities */
    write_icc_bpr1_el1(0);
    write_icc_ctlr_el1(0);
    isb();
    write_icc_igrpen1_el1(1); /* enable Group1 */
	write_icc_igrpen0_el1(1);
    isb();
}

/* Enable a PPI/SGI (0..31) on this CPU */
void gicv3_enable_ppi(u32 intid, u8 prio) {
    u64 rd = gicr_base_this_cpu();
    u64 sgi = rd + 0x10000; /* SGI frame */

    /* 1) Group1 */
    u64 igroupr0 = sgi + 0x0080; /* GICR_IGROUPR0 */
    u32 g = mmio_read32(igroupr0);
    g |= (1U << intid);
    mmio_write32(igroupr0, g);
	asm volatile("dsb sy" ::: "memory");

    /*
     * Make sure this PPI is Group1 Non-secure (G1NS), not Group1 Secure.
     * If IGRPMODR0 bit is 1 => Group1S (won't be visible via ICC_IAR1_EL1 in NS EL1).
     */
    volatile u64 *shm = (volatile u64 *)0xD7C00000;
    u64 igrpmodr0 = sgi + 0x0D00; /* GICR_IGRPMODR0 (needs validation) */
    shm[88] = igrpmodr0;
    u32 m = mmio_read32(igrpmodr0);
    shm[89] = m;
    m &= ~(1U << intid);          /* 0 => Group1NS */
    mmio_write32(igrpmodr0, m);
    asm volatile("dsb sy" ::: "memory");
	shm[90] = mmio_read32(igrpmodr0);

    /* 2) Priority */
    u64 ipriorityr0 = sgi + 0x0400; /* GICR_IPRIORITYR0 */
    *(volatile u8 *)(ipriorityr0 + intid) = prio;
	asm volatile("dsb sy" ::: "memory");

    /* 3) Enable */
    u64 isenabler0 = sgi + 0x0100; /* GICR_ISENABLER0 */
    mmio_write32(isenabler0, (1U << intid));

	asm volatile("dsb sy" ::: "memory");

    /* debug: read back after programming */
    {
	volatile u64 *shm = (volatile u64 *)0xD7C00000;
        u64 igroupr0 = sgi + 0x0080;
		u64 icenabler0 = sgi + 0x0180;
        shm[73] = mmio_read32(igroupr0);
        shm[74] = mmio_read32(isenabler0);
        shm[84] = mmio_read32(icenabler0);
        shm[85] = *(volatile u8 *)(sgi + 0x0400 + intid);

        /* try flip enable bit to verify we're touching the right reg */
        mmio_write32(icenabler0, (1U << intid));
        dsb(sy);
        shm[86] = mmio_read32(isenabler0);
        mmio_write32(isenabler0, (1U << intid));
        dsb(sy);
        shm[87] = mmio_read32(isenabler0);
    }
}


/* GICD offsets */
#define GICD_CTLR        0x0000
#define GICD_IGROUPR(n)  (0x0080 + 4*(n))
#define GICD_ISENABLER(n) (0x0100 + 4*(n))
#define GICD_IPRIORITYR(n) (0x0400 + (n))
#define GICD_IROUTER(n)  (0x6100 + 8*(n))

void gicv3_route_spi_to_self(u32 intid, u64 mpidr)
{
    /* Set SPI routing to this CPU (ARE must be enabled by firmware/Linux; usually is) */
    u64 aff = mpidr_to_aff(mpidr);
    mmio_write64(GICD_BASE + GICD_IROUTER(intid), aff);
}

void gicv3_enable_spi(u32 intid, u8 prio, u64 mpidr)
{
    (void)mpidr;

    /* Group1NS: set IGROUPR bit = 1 */
    u32 n = intid / 32;
    u32 bit = intid % 32;

    u32 g = mmio_read32(GICD_BASE + GICD_IGROUPR(n));
    g |= (1U << bit);
    mmio_write32(GICD_BASE + GICD_IGROUPR(n), g);

    /* priority */
    *(volatile u8 *)(GICD_BASE + GICD_IPRIORITYR(intid)) = prio;

    /* enable */
    mmio_write32(GICD_BASE + GICD_ISENABLER(n), (1U << bit));
}

void el1_full_gic_init(void)
{
    // 1. Enable ICC_SRE_EL1 (System Register Interface)
    u64 sre = read_icc_sre_el1();
    sre |= 1U;      // SRE=1
    sre &= ~(1U << 2); // DIB=0
    sre &= ~(1U << 3); // DFB=0
    write_icc_sre_el1(sre);
    asm volatile("isb" ::: "memory");

    // 2. Set Priority Mask, Allow all priorities (default Linux is PMR=0xff)
    write_icc_pmr_el1(0xFF);

    // 3. Set Binary Point (BPR1), Group1, Linux默认0
    write_icc_bpr1_el1(0);

    // 4. Enable/disallow any interrupt routing trapping; ICC_CTLR_EL1
    write_icc_ctlr_el1(0); // Usually EOImode=0, APM=0 (EOI model可改，但建议和Linux一致)

    asm volatile("isb" ::: "memory");

    // 5. Group enable: ICC_IGRPEN1_EL1 for Group 1 (NS interrupts)
    write_icc_igrpen1_el1(1);
    //   Group 0 (usually unused for NS EL1,但可直接enable)
    write_icc_igrpen0_el1(1);

    asm volatile("isb" ::: "memory");
}

void el1_gicd_spi_init(u32 spi, u8 prio, u64 mpidr)
{
    // Route SPI to this core
    gicv3_route_spi_to_self(spi, mpidr);
    // Enable SPI
    gicv3_enable_spi(spi, prio, mpidr);
    // 其它: affinity, group, priority, enable全部搞定
}


#define GICR_SGI_BASE   0x10000
#define GICR_IGROUPR0   (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_BASE + 0x0100)
#define GICR_ICENABLER0 (GICR_SGI_BASE + 0x0180)
#define GICR_ISPENDR0   (GICR_SGI_BASE + 0x0200)
#define GICR_ICPENDR0   (GICR_SGI_BASE + 0x0280)
#define GICR_ISACTIVER0 (GICR_SGI_BASE + 0x0300)
#define GICR_ICACTIVER0 (GICR_SGI_BASE + 0x0380)
#define GICR_IPRIORITYR (GICR_SGI_BASE + 0x0400)
#define GICR_ICFGR1     (GICR_SGI_BASE + 0x0C04)

#define PPI27 27

static inline void mmio_write8(u64 a, u8 v){ *(volatile u8 *)a = v; }

static inline void write_cntp_tval_el0(u64 v){ __asm__ volatile("msr cntp_tval_el0, %0"::"r"(v)); }
static inline void write_cntp_ctl_el0(u64 v){ __asm__ volatile("msr cntp_ctl_el0, %0"::"r"(v)); }
static inline u64 read_cntfrq_el0(void){ u64 v; __asm__ volatile("mrs %0, cntfrq_el0":"=r"(v)); return v; }

void enable_ppi27(u64 gicr_base)
{
    /* group1 */
    u32 grp = mmio_read32(gicr_base + GICR_IGROUPR0);
    grp |= (1U << PPI27);
    mmio_write32(gicr_base + GICR_IGROUPR0, grp);

    /* priority: pick high priority for debug (smaller = higher). use 0x40 */
    mmio_write8(gicr_base + GICR_IPRIORITYR + PPI27, 0x40);

    /* config: usually level for timer PPIs; leave as 0 unless你要强制 edge */
    /* enable */
    mmio_write32(gicr_base + GICR_ISENABLER0, (1U << PPI27));
    dsb(sy); isb();
}

void enable_all_ppis(u64 gicr_base)
{
    /* group1 all */
    mmio_write32(gicr_base + GICR_IGROUPR0, 0xFFFFFFFF);

    /* priority all to 0x40 */
    for (int i = 0; i < 32; i++)
        mmio_write8(gicr_base + GICR_IPRIORITYR + i, 0x40);

    /* enable all SGI/PPI */
    mmio_write32(gicr_base + GICR_ISENABLER0, 0xFFFFFFFF);
    dsb(sy); isb();
}

/*
 * Disable all PPIs to prevent interrupt storms.
 * This clears all enabled SGI/PPI interrupts (0..31).
 */
void disable_all_ppis(u64 gicr_base)
{
    /* Disable all SGI/PPI (write 1 to clear) */
    mmio_write32(gicr_base + GICR_ICENABLER0, 0xFFFFFFFF);
    dsb(sy); isb();
}

/*
 * Enable specific PPIs by mask.
 * @gicr_base: GICR base address for this CPU
 * @mask: bitmask of PPIs to enable (bit N = PPI N)
 * @prio: priority to assign (0x00=highest, 0xFF=lowest)
 */
void enable_ppi_mask(u64 gicr_base, u32 mask, u8 prio)
{
    /* Set Group1 for requested PPIs */
    u32 grp = mmio_read32(gicr_base + GICR_IGROUPR0);
    grp |= mask;
    mmio_write32(gicr_base + GICR_IGROUPR0, grp);
    
    /* Set priority for each enabled PPI */
    for (int i = 0; i < 32; i++) {
        if (mask & (1U << i)) {
            mmio_write8(gicr_base + GICR_IPRIORITYR + i, prio);
        }
    }
    
    /* Enable the PPIs */
    mmio_write32(gicr_base + GICR_ISENABLER0, mask);
    dsb(sy); isb();
}

/*
 * Enable a single PPI interrupt.
 * @gicr_base: GICR base address for this CPU
 * @intid: PPI interrupt ID (16..31)
 * @prio: priority to assign
 */
void enable_only_ppi(u64 gicr_base, u32 intid, u8 prio)
{
    if (intid > 31) return; /* out of range */
    
    /* Disable all PPIs first */
    disable_all_ppis(gicr_base);
    
    /* Enable just this one */
    enable_ppi_mask(gicr_base, (1U << intid), prio);
}
