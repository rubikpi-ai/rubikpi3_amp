#include <type.h>
#include <asm/irq.h>

/* Adjust to your environment */
#define SHM_BASE 0xD7C00000ULL

/* GICR physical range from /proc/iomem */
#define GICR_BASE_PA 0x17A60000ULL
#define GICR_END_PA  0x17B5FFFFULL

#define GICR_STRIDE  0x20000ULL

/* GICR offsets */
#define GICR_CTLR      0x0000
#define GICR_TYPER     0x0008
#define GICR_WAKER     0x0014

#define GICR_SGI_BASE  0x10000
#define GICR_IGROUPR0  (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_BASE + 0x0100)
#define GICR_IPRIORITYR (GICR_SGI_BASE + 0x0400) /* byte array */
#define GICR_ICFGR1    (GICR_SGI_BASE + 0x0C04) /* PPIs 16..31 */

#define PPI27 27

static inline u32 mmio_read32(u64 a){ return *(volatile u32 *)a; }
static inline u64 mmio_read64(u64 a){ return *(volatile u64 *)a; }
static inline u8  mmio_read8(u64 a){ return *(volatile u8 *)a; }

static inline void dsb_sy(void){ __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb_(void){ __asm__ volatile("isb" ::: "memory"); }

static inline u64 read_mpidr_el1(void)
{
	u64 v; __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v)); return v;
}

static inline u64 read_sysreg64(const char *dummy) { (void)dummy; return 0; }

/* ICC sysregs (GICv3 CPU interface) via S3 encodings */
#define READ_SYSREG_S3_0(cn, cm, op2) ({                          \
	u64 __v;                                                  \
	__asm__ volatile("mrs %0, S3_0_C" #cn "_C" #cm "_" #op2   \
			 : "=r"(__v));                             \
	__v;                                                      \
})
static inline u64 read_icc_pmr_el1(void)     { return READ_SYSREG_S3_0(4,  6, 0); }
static inline u64 read_icc_igrpen1_el1(void) { return READ_SYSREG_S3_0(12, 12, 7); }
//static inline u64 read_icc_sre_el1(void)     { return READ_SYSREG_S3_0(12, 12, 5); }

/* Arch timer (physical) */
static inline u64 read_cntp_ctl_el0(void)
{
	u64 v; __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(v)); return v;
}
static inline u64 read_cntp_tval_el0(void)
{
	u64 v; __asm__ volatile("mrs %0, cntp_tval_el0" : "=r"(v)); return v;
}
static inline u64 read_cntp_cval_el0(void)
{
	u64 v; __asm__ volatile("mrs %0, cntp_cval_el0" : "=r"(v)); return v;
}

/*
 * GICR_TYPER AffinityValue is bits [63:32].
 * It encodes Aff3:Aff2:Aff1:Aff0 (8 bits each).
 * MPIDR_EL1 encodes affinity too; we compare Aff[0..3].
 */
static inline u32 mpidr_affinity(u64 mpidr)
{
	u32 aff0 = (mpidr >> 0) & 0xff;
	u32 aff1 = (mpidr >> 8) & 0xff;
	u32 aff2 = (mpidr >> 16) & 0xff;
	u32 aff3 = (mpidr >> 32) & 0xff;
	return (aff3 << 24) | (aff2 << 16) | (aff1 << 8) | aff0;
}

static u64 find_gicr_base_for_self(u64 mpidr)
{
	u32 want = mpidr_affinity(mpidr);

	for (u64 pa = GICR_BASE_PA; pa + GICR_STRIDE - 1 <= GICR_END_PA; pa += GICR_STRIDE) {
		u64 typer = mmio_read64(pa + GICR_TYPER);
		u32 aff = (u32)(typer >> 32); /* AffinityValue */
		if (aff == want)
			return pa;
	}
	return 0;
}
#define GICR_ISPENDR0   (GICR_SGI_BASE + 0x0200)
static inline u64 read_icc_hppir1_el1(void) { return READ_SYSREG_S3_0(12, 12, 2); }

static inline u64 read_cntkctl_el1(void){ u64 v; __asm__ volatile("mrs %0, cntkctl_el1":"=r"(v)); return v; }
/*
 * Write results into shm[]:
 *  shm[0] = magic
 *  shm[1] = mpidr
 *  shm[2] = gicr_base_found
 *  shm[3] = ICC_PMR_EL1
 *  shm[4] = ICC_IGRPEN1_EL1
 *  shm[5] = ICC_SRE_EL1
 *  shm[6] = GICR_IGROUPR0
 *  shm[7] = GICR_ISENABLER0
 *  shm[8] = GICR_IPRIORITYR[PPI27] (byte)
 *  shm[9] = GICR_ICFGR1
 *  shm[10]= CNTP_CTL_EL0
 *  shm[11]= CNTP_TVAL_EL0
 *  shm[12]= CNTP_CVAL_EL0
 *  shm[13]= GICR_WAKER
 *  shm[14]= GICR_CTLR
 *  shm[15]= raw GICR_TYPER
 */
void arch_ppi27_diag_dump(void)
{
	volatile u64 *shm = (volatile u64 *)SHM_BASE;

	u64 mpidr = read_mpidr_el1();
	u64 gicr = find_gicr_base_for_self(mpidr);

	shm[0] = 0x4449414750504937ULL; /* "DIAGPPI7" */
	shm[1] = mpidr;
	shm[2] = gicr;

	shm[3] = read_icc_pmr_el1();
	shm[4] = read_icc_igrpen1_el1();
	shm[5] = read_icc_sre_el1();

	if (gicr) {
		shm[6]  = mmio_read32(gicr + GICR_IGROUPR0);
		shm[7]  = mmio_read32(gicr + GICR_ISENABLER0);
		shm[8]  = (u64)mmio_read8(gicr + GICR_IPRIORITYR + PPI27);
		shm[9]  = mmio_read32(gicr + GICR_ICFGR1);
		shm[13] = mmio_read32(gicr + GICR_WAKER);
		shm[14] = mmio_read32(gicr + GICR_CTLR);
		shm[15] = mmio_read64(gicr + GICR_TYPER);
	} else {
		shm[6] = shm[7] = shm[8] = shm[9] = 0;
		shm[13] = shm[14] = shm[15] = 0;
	}

	shm[10] = read_cntp_ctl_el0();
	shm[11] = read_cntp_tval_el0();
	shm[12] = read_cntp_cval_el0();

	shm[16] = mmio_read32(gicr + GICR_ISPENDR0);
	shm[17] = read_icc_hppir1_el1();
	shm[18] = read_cntkctl_el1();

	dsb_sy();
	isb_();
}
