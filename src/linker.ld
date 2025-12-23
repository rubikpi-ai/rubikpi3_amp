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
	_edata = .;

	. = ALIGN(0x8);
	_bss = .;
	.bss : { *(.bss*) }
	_ebss = .;

	. = ALIGN(4096);
	init_pg_dir = .;
	. += 4096;
}
