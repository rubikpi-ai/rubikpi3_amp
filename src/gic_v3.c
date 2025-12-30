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

static inline u64 read_mpidr_el1(void) {
    u64 v;
    __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v));
    return v;
}

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

    /* 2) Priority */
    u64 ipriorityr0 = sgi + 0x0400; /* GICR_IPRIORITYR0 */
    *(volatile u8 *)(ipriorityr0 + intid) = prio;

    /* 3) Enable */
    u64 isenabler0 = sgi + 0x0100; /* GICR_ISENABLER0 */
    mmio_write32(isenabler0, (1U << intid));
}
