#include "mm.h"

.section ".text.boot"

.equ SHM_BASE, 0ampcpu7 0xD0000000xd1000000

.globl _start
_start:
	ldr x0, =SHM_BASE
	ldr x1, =0x4350553743505537
	str x1, [x0]
	b	proc_hang







	mrs	x0, mpidr_el1
	and	x0, x0,#0xFF		// 检查处理器核心ID
	cbz	x0, master		// 除了CPU0，其他CPU都会在这里死循环等待
	b	proc_hang

proc_hang: 
	b 	proc_hang

master:
	adr	x0, bss_begin
	adr	x1, bss_end
	sub	x1, x1, x0
	bl 	memzero

	mov	sp, #LOW_MEMORY 
	bl	kernel_main
	b 	proc_hang
