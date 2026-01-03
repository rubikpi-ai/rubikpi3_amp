SECTIONS
{
	. = 0xd0800000;

	_text_boot = .;
	.text.boot : { *(.text.boot) }
	_etext_boot = .;

	_text = .;
	.text :
	{
		*(.text)
	}
	_etext = .;

	_rodata = .;
	.rodata :
	{
		*(.rodata)
	}
	_erodata = .;

	_data = .;
	.data :
	{
		*(.data)
	}
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
	.bss : { *(.bss*) }
	_ebss = .;
	_end = .;

	_heap_start = .;
	. = 0xD7C00000;
	_heap_end = .;

	_shared_memory = .;
	. = 0xD8000000;
	_stack_bottom = .;
	. = 0xD87FF000;   /* <== in carveout, 4KB below top, 16B align ok */
	_stack_top = .;
}
