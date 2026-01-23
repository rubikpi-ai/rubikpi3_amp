/* Memory layout for RubikPi3 AMP */
MEMORY
{
	/* CODE+HEAP: 0xD0800000 - 0xD7C00000 (116MB) */
	CODE   (rwx) : ORIGIN = 0xD0800000, LENGTH = 116M
	/* SHARED: 0xD7C00000 - 0xD8000000 (4MB) */
	SHARED (rw)  : ORIGIN = 0xD7C00000, LENGTH = 4M
	/* STACK: 0xD8000000 - 0xD87FF000 (~8MB) */
	STACK  (rw)  : ORIGIN = 0xD8000000, LENGTH = 8188K
}

SECTIONS
{
	. = 0xD0800000;

	_text_boot = .;
	.text.boot : { *(.text.boot) } > CODE
	_etext_boot = .;

	_text = .;
	.text : { *(.text) } > CODE
	_etext = .;

	_rodata = .;
	.rodata : { *(.rodata) } > CODE
	_erodata = .;

	_data = .;
	.data : { *(.data) } > CODE

	. = ALIGN(4096);
	idmap_pg_dir = .;
	. += 3*4096;
	idmap_pg_end = .;
	init_pg_dir = .;
	. += 3*4096;
	init_pg_end = .;
	swapper_pg_dir = .;
	. += 4096;
	_edata = .;

	. = ALIGN(0x8);
	_bss = .;
	.bss : { *(.bss*) } > CODE
	_ebss = .;
	_end = .;

	/* Heap: from _end to 0xD7C00000 */
	_heap_start = .;
	_heap_end = ORIGIN(SHARED);

	/* Shared memory: 0xD7C00000 - 0xD8000000 */
	_shared_memory = ORIGIN(SHARED);

	/* Stack: 0xD8000000 - 0xD87FF000 */
	_stack_bottom = ORIGIN(STACK);
	_stack_top = ORIGIN(STACK) + LENGTH(STACK);
}
