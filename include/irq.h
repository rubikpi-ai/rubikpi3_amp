#ifndef __ASM_ARCH_IRQ_H
#define __ASM_ARCH_IRQ_H
#include <type.h>

static inline void arch_local_irq_enable(void)
{
	asm volatile("msr daifclr, #3" ::: "memory"); /* clear F and I */
	asm volatile("isb" ::: "memory");
}

static inline void arch_local_irq_disable(void)
{
	asm volatile(
		"msr	daifset, #2"
		:
		:
		: "memory");
}

void arch_irq_init(void);


/* From your DT */
#define GICD_BASE   0x17A00000ULL
#define GICR_BASE   0x17A60000ULL
#define GICR_STRIDE 0x20000ULL      /* 128KB per CPU RD+SGI frame */

#define TIMER_PPI_ID 27             /* from /proc/interrupts */

#define SHM_BASE 0xD7C00000ULL      /* your shared debug memory */

/*
 * AArch64 system register access using encodings.
 * This avoids toolchain assembler not recognizing ICC_* aliases.
 *
 * Encodings from Arm ARM:
 *   ICC_SRE_EL1      S3_0_C12_C12_5
 *   ICC_PMR_EL1      S3_0_C4_C6_0
 *   ICC_BPR1_EL1     S3_0_C12_C12_3
 *   ICC_CTLR_EL1     S3_0_C12_C12_4
 *   ICC_IGRPEN1_EL1  S3_0_C12_C12_7
 *   ICC_IAR1_EL1     S3_0_C12_C12_0
 *   ICC_EOIR1_EL1    S3_0_C12_C12_1
 *   ICC_DIR_EL1      S3_0_C12_C11_1
 */

#define SYSREG_READ64(reg, out)  __asm__ volatile("mrs %0, " reg : "=r"(out))
#define SYSREG_WRITE64(reg, in)  __asm__ volatile("msr " reg ", %0" :: "r"(in))

static inline u64 read_icc_sre_el1(void)      { u64 v; SYSREG_READ64("S3_0_C12_C12_5", v); return v; }
static inline void     write_icc_sre_el1(u64 v){ SYSREG_WRITE64("S3_0_C12_C12_5", v); }

static inline void write_icc_pmr_el1(u64 v)   { SYSREG_WRITE64("S3_0_C4_C6_0", v); }
static inline void write_icc_bpr1_el1(u64 v)  { SYSREG_WRITE64("S3_0_C12_C12_3", v); }
static inline void write_icc_ctlr_el1(u64 v)  { SYSREG_WRITE64("S3_0_C12_C12_4", v); }
static inline void write_icc_igrpen1_el1(u64 v){ SYSREG_WRITE64("S3_0_C12_C12_7", v); }
static inline void write_icc_igrpen0_el1(u64 v){ SYSREG_WRITE64("S3_0_C12_C12_6", v); }

static inline u32 read_icc_iar1_el1(void) {
    u64 v;
    SYSREG_READ64("S3_0_C12_C12_0", v);
    return (u32)v;
}
static inline void write_icc_eoir1_el1(u32 v) { SYSREG_WRITE64("S3_0_C12_C12_1", (u64)v); }
static inline void write_icc_dir_el1(u32 v)  { SYSREG_WRITE64("S3_0_C12_C11_1", (u64)v); }



#endif /* __ASM_ARCH_IRQ_H */
