SECTIONS
{
	. = 0xd0000000,

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

	_stack_top = 0xd1000000;
}
