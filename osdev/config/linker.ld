ENTRY(_start)

SECTIONS
{
	. = 1M;
	__kernel_start = .;

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
	 * Paging-structures pool — 8 MB reserved for page directories and
	 * page tables (each 4 KB; 8 MB => 2048 slots, one per process and
	 * one per 4 MB chunk mapped).  Both are accessed through their
	 * identity VA, so they must live in a fixed range that the PMM
	 * never hands out; otherwise a user VA (also identity-mapped)
	 * could numerically collide with one of these PAs.  It sits BEFORE
	 * the PMM bitmap, so pmm_init() protects it automatically: the PMM
	 * only frees pages after the bitmap, keeping everything below it
	 * (kernel, pool, bitmap) reserved.
	 */
	.page_tables BLOCK(4K) : ALIGN(4K)
	{
		__page_table_base = .;
		. = . + 8M;
		__page_table_end = .;
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

SECTIONS
{
	. = 0xC0000000;
	__user_heap_start = .;
	. = . + 0x1000000;
	__user_heap_end = .;
}

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* 5 (RX) */
    rodata PT_LOAD FLAGS(4);   /* 4 (R)  */
    data   PT_LOAD FLAGS(6);   /* 6 (RW) */
	bss    PT_LOAD FLAGS(6);   /* 6 (RW) */
}
