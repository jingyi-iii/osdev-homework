ENTRY(_start)

SECTIONS
{
	. = 1M;

	/* Code data. */
	.text BLOCK(4K) : ALIGN(4K)
	{
		*(.multiboot)
		*(.text)
	} :text

	.initcall :
	{
        __start_initcall = .;
        *(.initcall)
        __stop_initcall = .;
    } :text

	/* Read-only data. */
	.rodata BLOCK(4K) : ALIGN(4K)
	{
		*(.rodata)
	} :rodata

	/* Read-write data (initialized) */
	.data BLOCK(4K) : ALIGN(4K)
	{
		*(.data)
	} :data

	/* Read-write data (uninitialized) and stack */
	.bss BLOCK(4K) : ALIGN(4K)
	{
		*(COMMON)
		*(.bss)
	} :bss
}

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* 5 (RX) */
    rodata PT_LOAD FLAGS(4);   /* 4 (R)  */
    data   PT_LOAD FLAGS(6);   /* 6 (RW) */
	bss    PT_LOAD FLAGS(6);   /* 6 (RW) */
}
