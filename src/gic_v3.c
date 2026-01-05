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
#include <asm/gic_v3.h>

static inline u32 mmio_read32(u64 addr) { return *(volatile u32 *)addr; }
static inline void mmio_write32(u64 addr, u32 v) { *(volatile u32 *)addr = v; }
static inline u64 mmio_read64(u64 addr) { return *(volatile u64 *)addr; }
static inline void mmio_write64(u64 addr, u64 v) { *(volatile u64 *)addr = v; }

static inline u64 read_mpidr_el1(void) {
	u64 v;
	__asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v));
	return v;
}

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
	u64 rd = gicr_base_this_cpu();

	/* Wake redistributor */
	u64 waker = rd + GICR_WAKER;
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

static inline void mmio_write8(u64 a, u8 v){ *(volatile u8 *)a = v; }

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

void enable_ppi(u64 gicr_base, u8 ppi_num, u8 prio)
{
	/* group1 for that bit */
	u32 grp = mmio_read32(gicr_base + GICR_IGROUPR0);
	grp |= (1U << ppi_num);
	mmio_write32(gicr_base + GICR_IGROUPR0, grp);

	/* set priority */
	mmio_write8(gicr_base + GICR_IPRIORITYR + ppi_num, prio);

	/* enable */
	grp = mmio_read32(gicr_base + GICR_ISENABLER0);
	grp |= (1U << ppi_num);
	mmio_write32(gicr_base + GICR_ISENABLER0, grp);
	dsb(sy); isb();
}

void disable_all_ppis(u64 gicr_base)
{
	u64 sgi = gicr_base + 0x10000;
	mmio_write32(sgi + 0x0180, 0xFFFFFFFF); /* ICENABLER0 */
	dsb(sy); isb();
}

void write_cntp_ctl_el0(u64 v)
{
	__asm__ volatile("msr cntp_ctl_el0, %0"::"r"(v));
}

void write_cntv_ctl_el0(u64 v)
{
	__asm__ volatile ("msr cntv_ctl_el0, %0" :: "r"(v));
}
