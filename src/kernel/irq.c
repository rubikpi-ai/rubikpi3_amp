#include <type.h>
#include <irq.h>
#include <gic_v3.h>
#include <sysregs.h>
#include <timer.h>

/* ESR_EL1 Exception Class (EC) field definitions - bits [31:26] */
#define ESR_ELx_EC_SHIFT	26
#define ESR_ELx_EC_MASK		(0x3FUL << ESR_ELx_EC_SHIFT)
#define ESR_ELx_EC(esr)		(((esr) & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT)

/* ESR_EL1 Instruction Length (IL) field - bit [25] */
#define ESR_ELx_IL_SHIFT	25
#define ESR_ELx_IL		(1UL << ESR_ELx_IL_SHIFT)

/* ESR_EL1 Instruction Specific Syndrome (ISS) field - bits [24:0] */
#define ESR_ELx_ISS_MASK	0x01FFFFFFUL

/* Exception Class values */
#define ESR_ELx_EC_UNKNOWN	0x00	/* Unknown reason */
#define ESR_ELx_EC_WFx		0x01	/* Trapped WFI/WFE */
#define ESR_ELx_EC_CP15_32	0x03	/* Trapped MCR/MRC (32-bit) */
#define ESR_ELx_EC_CP15_64	0x04	/* Trapped MCRR/MRRC (64-bit) */
#define ESR_ELx_EC_CP14_MR	0x05	/* Trapped MCR/MRC (CP14) */
#define ESR_ELx_EC_CP14_LS	0x06	/* Trapped LDC/STC (CP14) */
#define ESR_ELx_EC_FP_ASIMD	0x07	/* Trapped FP/SIMD access */
#define ESR_ELx_EC_CP10_ID	0x08	/* Trapped VMRS access */
#define ESR_ELx_EC_PAC		0x09	/* Trapped PAC instruction */
#define ESR_ELx_EC_CP14_64	0x0C	/* Trapped MRRC (CP14) */
#define ESR_ELx_EC_BTI		0x0D	/* Branch Target Exception */
#define ESR_ELx_EC_ILL		0x0E	/* Illegal Execution state */
#define ESR_ELx_EC_SVC32	0x11	/* SVC instruction (32-bit) */
#define ESR_ELx_EC_HVC32	0x12	/* HVC instruction (32-bit) */
#define ESR_ELx_EC_SMC32	0x13	/* SMC instruction (32-bit) */
#define ESR_ELx_EC_SVC64	0x15	/* SVC instruction (64-bit) */
#define ESR_ELx_EC_HVC64	0x16	/* HVC instruction (64-bit) */
#define ESR_ELx_EC_SMC64	0x17	/* SMC instruction (64-bit) */
#define ESR_ELx_EC_SYS64	0x18	/* MSR/MRS/Sys instruction */
#define ESR_ELx_EC_SVE		0x19	/* SVE access trap */
#define ESR_ELx_EC_ERET		0x1A	/* ERET/ERETAA/ERETAB */
#define ESR_ELx_EC_IMP_DEF	0x1F	/* Implementation defined */
#define ESR_ELx_EC_IABT_LOW	0x20	/* Instruction Abort (lower EL) */
#define ESR_ELx_EC_IABT_CUR	0x21	/* Instruction Abort (current EL) */
#define ESR_ELx_EC_PC_ALIGN	0x22	/* PC alignment fault */
#define ESR_ELx_EC_DABT_LOW	0x24	/* Data Abort (lower EL) */
#define ESR_ELx_EC_DABT_CUR	0x25	/* Data Abort (current EL) */
#define ESR_ELx_EC_SP_ALIGN	0x26	/* SP alignment fault */
#define ESR_ELx_EC_FP_EXC32	0x28	/* FP exception (32-bit) */
#define ESR_ELx_EC_FP_EXC64	0x2C	/* FP exception (64-bit) */
#define ESR_ELx_EC_SERROR	0x2F	/* SError interrupt */
#define ESR_ELx_EC_BREAKPT_LOW	0x30	/* HW Breakpoint (lower EL) */
#define ESR_ELx_EC_BREAKPT_CUR	0x31	/* HW Breakpoint (current EL) */
#define ESR_ELx_EC_SOFTSTP_LOW	0x32	/* Software Step (lower EL) */
#define ESR_ELx_EC_SOFTSTP_CUR	0x33	/* Software Step (current EL) */
#define ESR_ELx_EC_WATCHPT_LOW	0x34	/* Watchpoint (lower EL) */
#define ESR_ELx_EC_WATCHPT_CUR	0x35	/* Watchpoint (current EL) */
#define ESR_ELx_EC_BKPT32	0x38	/* BKPT instruction (32-bit) */
#define ESR_ELx_EC_BRK64	0x3C	/* BRK instruction (64-bit) */

/* Data/Instruction Abort ISS fields */
#define ESR_ELx_ISV_SHIFT	24
#define ESR_ELx_ISV		(1UL << ESR_ELx_ISV_SHIFT)
#define ESR_ELx_SAS_SHIFT	22
#define ESR_ELx_SAS_MASK	(3UL << ESR_ELx_SAS_SHIFT)
#define ESR_ELx_WNR_SHIFT	6
#define ESR_ELx_WNR		(1UL << ESR_ELx_WNR_SHIFT)
#define ESR_ELx_CM_SHIFT	8
#define ESR_ELx_CM		(1UL << ESR_ELx_CM_SHIFT)
#define ESR_ELx_DFSC_MASK	0x3FUL	/* Data Fault Status Code */

/* DFSC (Data Fault Status Code) values */
#define ESR_ELx_FSC_ADDR_L0	0x00	/* Address size fault, level 0 */
#define ESR_ELx_FSC_ADDR_L1	0x01	/* Address size fault, level 1 */
#define ESR_ELx_FSC_ADDR_L2	0x02	/* Address size fault, level 2 */
#define ESR_ELx_FSC_ADDR_L3	0x03	/* Address size fault, level 3 */
#define ESR_ELx_FSC_TRANS_L0	0x04	/* Translation fault, level 0 */
#define ESR_ELx_FSC_TRANS_L1	0x05	/* Translation fault, level 1 */
#define ESR_ELx_FSC_TRANS_L2	0x06	/* Translation fault, level 2 */
#define ESR_ELx_FSC_TRANS_L3	0x07	/* Translation fault, level 3 */
#define ESR_ELx_FSC_ACCESS_L1	0x09	/* Access flag fault, level 1 */
#define ESR_ELx_FSC_ACCESS_L2	0x0A	/* Access flag fault, level 2 */
#define ESR_ELx_FSC_ACCESS_L3	0x0B	/* Access flag fault, level 3 */
#define ESR_ELx_FSC_PERM_L1	0x0D	/* Permission fault, level 1 */
#define ESR_ELx_FSC_PERM_L2	0x0E	/* Permission fault, level 2 */
#define ESR_ELx_FSC_PERM_L3	0x0F	/* Permission fault, level 3 */
#define ESR_ELx_FSC_EXTABT	0x10	/* Synchronous external abort */
#define ESR_ELx_FSC_ALIGN	0x21	/* Alignment fault */

extern int printk(const char *fmt, ...);

static inline void psci_cpu_off(void)
{
	asm volatile(
		"ldr x0, =0x84000002\n"
		"smc #0\n"
		::: "x0", "x1", "x2", "x3"
	);
}

/* Get exception class name string */
static const char *esr_get_class_string(unsigned int ec)
{
	switch (ec) {
	case ESR_ELx_EC_UNKNOWN:	return "Unknown/Uncategorized";
	case ESR_ELx_EC_WFx:		return "Trapped WFI/WFE";
	case ESR_ELx_EC_CP15_32:	return "Trapped MCR/MRC (CP15, 32-bit)";
	case ESR_ELx_EC_CP15_64:	return "Trapped MCRR/MRRC (CP15, 64-bit)";
	case ESR_ELx_EC_CP14_MR:	return "Trapped MCR/MRC (CP14)";
	case ESR_ELx_EC_CP14_LS:	return "Trapped LDC/STC (CP14)";
	case ESR_ELx_EC_FP_ASIMD:	return "Trapped FP/ASIMD access";
	case ESR_ELx_EC_CP10_ID:	return "Trapped VMRS access";
	case ESR_ELx_EC_PAC:		return "Trapped PAC instruction";
	case ESR_ELx_EC_CP14_64:	return "Trapped MRRC (CP14, 64-bit)";
	case ESR_ELx_EC_BTI:		return "Branch Target Exception";
	case ESR_ELx_EC_ILL:		return "Illegal Execution state";
	case ESR_ELx_EC_SVC32:		return "SVC instruction (AArch32)";
	case ESR_ELx_EC_HVC32:		return "HVC instruction (AArch32)";
	case ESR_ELx_EC_SMC32:		return "SMC instruction (AArch32)";
	case ESR_ELx_EC_SVC64:		return "SVC instruction (AArch64)";
	case ESR_ELx_EC_HVC64:		return "HVC instruction (AArch64)";
	case ESR_ELx_EC_SMC64:		return "SMC instruction (AArch64)";
	case ESR_ELx_EC_SYS64:		return "MSR/MRS/Sys instruction";
	case ESR_ELx_EC_SVE:		return "SVE access trap";
	case ESR_ELx_EC_ERET:		return "ERET/ERETAA/ERETAB";
	case ESR_ELx_EC_IMP_DEF:	return "Implementation defined";
	case ESR_ELx_EC_IABT_LOW:	return "Instruction Abort (lower EL)";
	case ESR_ELx_EC_IABT_CUR:	return "Instruction Abort (current EL)";
	case ESR_ELx_EC_PC_ALIGN:	return "PC alignment fault";
	case ESR_ELx_EC_DABT_LOW:	return "Data Abort (lower EL)";
	case ESR_ELx_EC_DABT_CUR:	return "Data Abort (current EL)";
	case ESR_ELx_EC_SP_ALIGN:	return "SP alignment fault";
	case ESR_ELx_EC_FP_EXC32:	return "FP exception (AArch32)";
	case ESR_ELx_EC_FP_EXC64:	return "FP exception (AArch64)";
	case ESR_ELx_EC_SERROR:		return "SError interrupt";
	case ESR_ELx_EC_BREAKPT_LOW:	return "HW Breakpoint (lower EL)";
	case ESR_ELx_EC_BREAKPT_CUR:	return "HW Breakpoint (current EL)";
	case ESR_ELx_EC_SOFTSTP_LOW:	return "Software Step (lower EL)";
	case ESR_ELx_EC_SOFTSTP_CUR:	return "Software Step (current EL)";
	case ESR_ELx_EC_WATCHPT_LOW:	return "Watchpoint (lower EL)";
	case ESR_ELx_EC_WATCHPT_CUR:	return "Watchpoint (current EL)";
	case ESR_ELx_EC_BKPT32:		return "BKPT instruction (AArch32)";
	case ESR_ELx_EC_BRK64:		return "BRK instruction (AArch64)";
	default:			return "Unknown exception class";
	}
}

/* Get fault status code description for Data/Instruction Aborts */
static const char *esr_get_fault_string(unsigned int dfsc)
{
	switch (dfsc) {
	case ESR_ELx_FSC_ADDR_L0:	return "Address size fault, level 0";
	case ESR_ELx_FSC_ADDR_L1:	return "Address size fault, level 1";
	case ESR_ELx_FSC_ADDR_L2:	return "Address size fault, level 2";
	case ESR_ELx_FSC_ADDR_L3:	return "Address size fault, level 3";
	case ESR_ELx_FSC_TRANS_L0:	return "Translation fault, level 0";
	case ESR_ELx_FSC_TRANS_L1:	return "Translation fault, level 1";
	case ESR_ELx_FSC_TRANS_L2:	return "Translation fault, level 2";
	case ESR_ELx_FSC_TRANS_L3:	return "Translation fault, level 3";
	case ESR_ELx_FSC_ACCESS_L1:	return "Access flag fault, level 1";
	case ESR_ELx_FSC_ACCESS_L2:	return "Access flag fault, level 2";
	case ESR_ELx_FSC_ACCESS_L3:	return "Access flag fault, level 3";
	case ESR_ELx_FSC_PERM_L1:	return "Permission fault, level 1";
	case ESR_ELx_FSC_PERM_L2:	return "Permission fault, level 2";
	case ESR_ELx_FSC_PERM_L3:	return "Permission fault, level 3";
	case ESR_ELx_FSC_EXTABT:	return "Synchronous external abort";
	case ESR_ELx_FSC_ALIGN:		return "Alignment fault";
	default:			return "Unknown fault";
	}
}

/* Get bad mode reason string */
static const char *get_bad_mode_string(int reason)
{
	switch (reason) {
	case 0:		return "Synchronous Abort";
	case 1:		return "IRQ";
	case 2:		return "FIQ";
	case 3:		return "SError";
	default:	return "Unknown";
	}
}

/* Print register dump */
static void show_regs(struct pt_regs *regs)
{
	int i;

	printk("\nRegister dump:\n");
	for (i = 0; i < 30; i += 2) {
		printk("  x%d: 0x%016lx  x%d: 0x%016lx\n",
		       i, regs->regs[i],
		       i + 1, regs->regs[i + 1]);
	}
	printk("  x30(lr): 0x%016lx\n", regs->regs[30]);
	printk("  sp: 0x%016lx\n", regs->sp);
	printk("  pc: 0x%016lx\n", regs->pc);
	printk("  pstate: 0x%016lx\n", regs->pstate);
}

/* Decode and print ESR information */
static void decode_esr(unsigned int esr)
{
	unsigned int ec = ESR_ELx_EC(esr);
	unsigned int il = (esr & ESR_ELx_IL) ? 1 : 0;
	unsigned int iss = esr & ESR_ELx_ISS_MASK;

	printk("\nESR_EL1 decode:\n");
	printk("  Exception Class (EC): 0x%x - %s\n", ec, esr_get_class_string(ec));
	printk("  Instruction Length (IL): %d (%s)\n", il, il ? "32-bit" : "16-bit");
	printk("  ISS: 0x%x\n", iss);

	/* Additional decode for Data/Instruction Aborts */
	if (ec == ESR_ELx_EC_DABT_LOW || ec == ESR_ELx_EC_DABT_CUR ||
	    ec == ESR_ELx_EC_IABT_LOW || ec == ESR_ELx_EC_IABT_CUR) {
		unsigned int dfsc = esr & ESR_ELx_DFSC_MASK;
		printk("  Fault Status Code: 0x%x - %s\n", dfsc, esr_get_fault_string(dfsc));

		if (ec == ESR_ELx_EC_DABT_LOW || ec == ESR_ELx_EC_DABT_CUR) {
			printk("  Write not Read (WnR): %s\n",
			       (esr & ESR_ELx_WNR) ? "Write" : "Read");
			printk("  Cache Maintenance (CM): %s\n",
			       (esr & ESR_ELx_CM) ? "Yes" : "No");
		}
	}
}

void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
	u64 far = read_sysreg(far_el1);
	u64 elr = read_sysreg(elr_el1);

	/* Disable interrupts immediately */
	arch_local_irq_disable();

	/* Print exception information via UART */
	printk("\n");
	printk("============================================\n");
	printk("          EXCEPTION OCCURRED!\n");
	printk("============================================\n");
	printk("\nException type: %s (reason=%d)\n", get_bad_mode_string(reason), reason);
	printk("ESR_EL1: 0x%08x\n", esr);
	printk("ELR_EL1 (PC): 0x%016lx\n", elr);
	printk("FAR_EL1 (Fault Address): 0x%016lx\n", far);

	/* Decode ESR */
	decode_esr(esr);

	/* Show register dump if regs is valid */
	if (regs) {
		show_regs(regs);
	}

	printk("\n============================================\n");
	printk("Calling PSCI CPU_OFF...\n");

	/* Call PSCI CPU_OFF to gracefully shutdown this CPU */
	/* This prevents the exception from affecting Linux */
	psci_cpu_off();

	/* Should never reach here */
	while (1)
		;
}

u64 jiffies = 0;

void irq_handler(void)
{
	volatile u64 *shm = (volatile u64 *)SHM_BASE;
	u32 iar = gicv3_iar1();
	u32 intid = iar & 0x3ff;

	shm[0] = 0x1111222233334444ULL;
	shm[1]++;           /* total irq count */
	shm[2] = intid;     /* last intid */

	if (intid == 0x1b) {
		shm[3]++;       /* timer tick count */
	//	jiffies++;
		timer_cntv_reload_hz(1000);
	}

	gicv3_eoi1(iar);
}
