#include "mm.h"
#include <asm/base.h>
#include <asm/sysregs.h>

.section ".text.boot"

.equ SHM_BASE, 0xD1000000

.globl _start
_start:
	ldr x0, =SHM_BASE
	ldr x1, =0x4350553743505537
	str x1, [x0]


	ldr x2, =0x0F12C000
	ldr w1, [x2]
	ldr x0, =0xD1000010
	str w1, [x0]

	/* out put */
	ldr x2, =0x0F12C004
	ldr w3, [x2]
	mov w4, #1
	orr w3, w3, w4, lsl #1
	str w3, [x2]

	ldr x2, =0x0F12C000
	ldr w3, [x2]
	mov w4, #1
	mov x3, xzr
	orr w3, w3, w4, lsl #9
	str w3, [x2]



	b proc_hang
#if 0
	b master

master:
	mrs x5, CurrentEL
	cmp x5, #CurrentEL_EL3
	b.eq el3_entry
	cmp x5, #CurrentEL_EL2
	b.eq el2_entry
	b el1_entry

el3_entry:
	ldr x0, =SCTLR_EL2_VALUE_MMU_DISABLED
	msr sctlr_el2, x0

	ldr x0, =HCR_HOST_NVHE_FLAGS
	msr hcr_el2, x0

	ldr x0, =SCR_VALUE
	msr scr_el3, x0

	ldr x0, =SPSR_EL2
	msr spsr_el3, x0

	adr x0, el2_entry
	msr elr_el3, x0

	eret

el2_entry:
	ldr x0, =SCTLR_EL2_VALUE_MMU_DISABLED
	msr sctlr_el2, x0

	/* The Execution state for EL1 is AArch64 */
	ldr x0, =HCR_HOST_NVHE_FLAGS
	msr hcr_el2, x0

	/* Generic timers. */
	mrs     x0, cnthctl_el2
	orr     x0, x0, #3                      // Enable EL1 physical timers
	msr     cnthctl_el2, x0
	msr     cntvoff_el2, xzr                // Clear virtual offset

	ldr x0, =SCTLR_EL1_VALUE_MMU_DISABLED
	msr sctlr_el1, x0

	ldr x0, =SPSR_EL1
	msr spsr_el2, x0

	adr x0, el1_entry
	msr elr_el2, x0

	eret

el1_entry:
	/* 设置异常向量表基地址到vbar寄存器 */
	ldr     x5, =vectors
	msr     vbar_el1, x5
	isb

	adr x0, bss_begin
	adr x1, bss_end
	sub x1, x1, x0
	bl memzero

	/* 创建页表 */
	bl create_page_table
	/* 打开MMU*/
	bl enable_mmu

	/* set sp to top of init_task_union */
	/*
	   这里要用adrp来获取init_task_union的物理地址，
	   不能使用ldr来获取init_task_union的链接地址
	   因为这时 CPU还运行在 VA=PA
	 */
	adrp x2, init_task_union
#endif

proc_hang:
	b 	proc_hang
