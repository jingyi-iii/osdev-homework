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

        __start_exitcall = .;
        *(.exitcall)
        __stop_exitcall = .;
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

	/*
	 * PMM bitmap — 128 KB reserved for the physical memory manager's
	 * bitmap (one bit per 4 KB page, enough for 4 GB of physical memory).
	 * The bitmap is zero-initialised by the loader (BSS-like section).
	 */
	.pmm_bitmap BLOCK(4K) : ALIGN(4K)
	{
		__pmm_bitmap_start = .;
		. = . + 128K;
		__pmm_bitmap_end = .;
	} :bss

	/* Kernel end marker — everything beyond this is free physical memory */
	__kernel_end = .;
}

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* 5 (RX) */
    rodata PT_LOAD FLAGS(4);   /* 4 (R)  */
    data   PT_LOAD FLAGS(6);   /* 6 (RW) */
	bss    PT_LOAD FLAGS(6);   /* 6 (RW) */
}
