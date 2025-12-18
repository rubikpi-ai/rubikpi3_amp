	.arch armv8-a
	.file	"kernel.c"
	.text
	.section	.rodata
	.align	3
.LC0:
	.string	"Welcome BenOS!\r\n"
	.text
	.align	2
	.global	kernel_main
	.type	kernel_main, %function
kernel_main:
.LFB0:
	.cfi_startproc
	stp	x29, x30, [sp, -16]!
	.cfi_def_cfa_offset 16
	.cfi_offset 29, -16
	.cfi_offset 30, -8
	mov	x29, sp
	bl	uart_init
	adrp	x0, .LC0
	add	x0, x0, :lo12:.LC0
	bl	uart_send_string
.L2:
	bl	uart_recv
	and	w0, w0, 255
	bl	uart_send
	b	.L2
	.cfi_endproc
.LFE0:
	.size	kernel_main, .-kernel_main
	.ident	"GCC: (Ubuntu 9.4.0-1ubuntu1~20.04.2) 9.4.0"
	.section	.note.GNU-stack,"",@progbits
