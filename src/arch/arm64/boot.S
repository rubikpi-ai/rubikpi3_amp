#include <mm.h>
#include <sysregs.h>
#include <pgtable_prot.h>

.section ".text.boot"

/* ---- GICv3 sysreg encodings (toolchain-safe) ----
 * ICC_SRE_EL2        S3_4_C12_C9_5
 * ICC_SRE_EL1        S3_0_C12_C12_5
 * ICC_PMR_EL1        S3_0_C4_C6_0
 * ICC_BPR1_EL1       S3_0_C12_C12_3
 * ICC_CTLR_EL1       S3_0_C12_C12_4
 * ICC_IGRPEN1_EL1    S3_0_C12_C12_7
 * (Optional) ICC_IGRPEN0_EL1 S3_0_C12_C12_6
 */

	.macro gicv3_cpuif_init_el2
/* Enable system register interface at EL2 */
	mrs x0, S3_4_C12_C9_5          /* ICC_SRE_EL2 */
	orr x0, x0, #1                 /* set SRE bit */
	msr S3_4_C12_C9_5, x0
	isb

	/* Ensure EL1 sees SRE enabled too (usually optional if EL1 sets it, but harmless) */
	mrs x0, S3_0_C12_C12_5         /* ICC_SRE_EL1 */
	orr x0, x0, #1
	msr S3_0_C12_C12_5, x0
	isb

	/* Allow all priorities */
	mov x0, #0xFF
	msr S3_0_C4_C6_0, x0           /* ICC_PMR_EL1 */
	isb

	/* No priority grouping */
	mov x0, #0
	msr S3_0_C12_C12_3, x0         /* ICC_BPR1_EL1 */
	isb

	/* Default control (keep simple) */
	mov x0, #0
	msr S3_0_C12_C12_4, x0         /* ICC_CTLR_EL1 */
	isb

	/* Enable Group1 interrupts at EL1 */
	mov x0, #1
	msr S3_0_C12_C12_7, x0         /* ICC_IGRPEN1_EL1 */
	isb
	.endm

.globl _start
_start:
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

	ldr x9, =_shared_memory
	mov x10, #0xE2E2
	str x10, [x9, #240]     // mark: in el2 before gic init

	gicv3_cpuif_init_el2

	ldr x0, =SCTLR_EL1_VALUE_MMU_DISABLED
	msr sctlr_el1, x0

	ldr x0, =SPSR_EL1
	msr spsr_el2, x0

	adr x0, el1_entry
	msr elr_el2, x0

	eret

el1_entry:
	adr x0, _bss
	adr x1, _ebss
	sub x1, x1, x0
	bl memzero

	ldr x0, =_heap_start
	ldr x1, =_heap_end
	sub x1, x1, x0
	bl memzero

	ldr x0, =_shared_memory
	ldr x1, =_stack_bottom
	sub x1, x1, x0
	bl memzero

	ldr     x0, =vectors
	msr     vbar_el1, x0
	isb

	ldr x0, =_stack_top
	mov	sp, x0
	bl	kernel_main
	b proc_hang

#if 0
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

create_page_table:
	mov x25, x30

	/*1. create identical mapping

	  创建恒等映射，即虚拟地址等于物理地址
	  这里把内核image都建立恒等映射
	 */
	adrp  x0, idmap_pg_dir
	adrp  x1, idmap_pg_end
	sub x1, x1, x0
	bl memzero

	/* create idmap mapping */
	adrp x1, _text_boot
	and x1, x1, SECTION_MASK
	mov x2, x1
	adrp x3,  _end
	add x3, x3, #SECTION_SIZE
	sub x3, x3, #1
	and x3, x3, SECTION_MASK
	mov x4, SWAPPER_MM_MMUFLAGS
	adrp  x0, idmap_pg_dir

	bl __create_section_mapping

	/*2. map to link addr

	  把内核image映射到链接地址
	  链接地址为0xffff000010080000
	  在跳转到C语言之前需要把重定位到链接地址
	 */
	adrp  x0, init_pg_dir
	adrp  x1, init_pg_end
	sub x1, x1, x0
	bl memzero

	/* adrp得到了_text_boot的物理地址*/
	adrp x1, _text_boot
	and x1, x1, SECTION_MASK
	/* ldr加载了_text_boot的链接地址*/
	ldr x2, =_text_boot
	ldr x3, =_end
	add x3, x3, #SECTION_SIZE
	sub x3, x3, #1
	and x3, x3, SECTION_MASK
	mov x4, SWAPPER_MM_MMUFLAGS
	adrp  x0, init_pg_dir

	bl __create_section_mapping

	mov x30, x25
	ret

/*
   创建段映射的页表（2MB大小的section）

   x0: page table base address
   x1: phys address
   x2: virt start address
   x3: virt end address
   x4: prot
 */
__create_section_mapping:
	mov x12, x30

	/*
	   第一步： 根据vaddr来找pgd索引，vaddr >> 39
	   结果保存到x8寄存器
	 */
	lsr x8, x2, #PGDIR_SHIFT
	and x8, x8, #PTRS_PER_PGD - 1

	/*
	   第二步： 把pud页表基地址来做成一个pgd页表项
	   填充到刚才找到的pgd表项中

	   x0指向： pgd页表的基地址
	   x0+PAGE_SIZE： 指向PUD页表的基地址
	   x0+2*PAGE_SIZE: 指向PMD页表的基地址
	 */
	add x9, x0, #PAGE_SIZE
	orr x9, x9, #PUD_TYPE_TABLE
	str x9, [x0, x8, lsl #3] // fill pgd entry

	add x0, x0, #PAGE_SIZE

	/*
	   第三步： 根据需要vaddr来找pud索引，vaddr >> 30
	 */
	lsr x8, x2, #PUD_SHIFT
	and x8, x8, #PTRS_PER_PUD - 1

	/*
	   第4步： 把pud页表基地址来做成一个pud页表项
	   填充到刚才找到的pud表项中
	 */
	add x9, x0, #PAGE_SIZE
	orr x9, x9, #PUD_TYPE_TABLE
	str x9, [x0, x8, lsl #3] // fill pud entry
	add x0, x0, #PAGE_SIZE

	/*
	   第5步：
	   根据vaddr来找到pmd索引，然后找到pmd表项
	   由物理地址来构建一个pmd页表项
	   然后填充到pmd表项里
	 */
1:
	lsr x8, x2, #PMD_SHIFT
	and x8, x8, #PTRS_PER_PMD - 1
	lsr x10, x1, #SECTION_SHIFT
	mov x9, x4
	orr x10, x9, x10, lsl #SECTION_SHIFT
	str x10, [x0, x8, lsl #3]

	add x2, x2, #PMD_SIZE  //next
	add x1, x1, #PMD_SIZE  //phy

	cmp x3, x2
	b.hi 1b

	mov x30, x12
	ret

.globl idmap_replace_ttbr1
idmap_replace_ttbr1:
	msr     ttbr1_el1, x0
	isb
	ret

enable_mmu:
	tlbi	vmalle1	// Invalidate local TLB
	dsb	nsh

	/*
	  设置MAIR寄存器，用来配置内存属性
	  在页表项中用来设置内存属性
	 */
	ldr	x5, =MAIR(0x00, MT_DEVICE_nGnRnE) | \
		     MAIR(0x04, MT_DEVICE_nGnRE) | \
		     MAIR(0x0c, MT_DEVICE_GRE) | \
		     MAIR(0x44, MT_NORMAL_NC) | \
		     MAIR(0xff, MT_NORMAL) | \
		     MAIR(0xbb, MT_NORMAL_WT)
	msr	mair_el1, x5

	/*
	   这种TCR寄存器，主要设置
	   1. T0SZ： TTBR0页表的管辖大小
	   2. T1SZ： TTBR1页表管辖大小
	   3. TCR_TG0_4K： TTBR0的页表粒度为4KB
	   4. TCR_TG1_4K： TTBR1的页表粒度为4KB
	   5. TCR_SH0_INNER/TCR_SH1_INNER: 设置为inner share类型
	   6. TCR_IRGN_WBWA: 设置cache为Write-Back
	   	Write-Allocate Cacheable
	 */
	ldr	x10, =TCR_TxSZ(VA_BITS) | TCR_TG_FLAGS | \
				TCR_SHARED | TCR_CACHE_FLAGS
	msr	tcr_el1, x10

	/*
	   SCTLR_ELx_M: 打开MMU
	   SCTLR_ELx_C: 打开全局的data cache功能
	   SCTLR_ELx_I: 打开全局的指令cache功能

	   不打开cache，ldxr指令访问noncache的地址会触发
	   Dabort异常
	 */
	ldr x3, =SCTLR_ELx_M | SCTLR_ELx_C | SCTLR_ELx_I

	/*
	   加载两个页表的物理地址
	   恒等映射页表： idmap_pg_dir
	   内核空间映射页表： init_pg_dir
	 */
	adrp x0, idmap_pg_dir
	adrp x1, init_pg_dir
	msr ttbr0_el1, x0
	msr ttbr1_el1, x1
	isb

	/*
	   打开MMU
	 */
	msr sctlr_el1, x3
	isb

	/*
	   无效所有的指令高速缓存
	 */
	ic iallu
	dsb nsh
	isb
	ret
