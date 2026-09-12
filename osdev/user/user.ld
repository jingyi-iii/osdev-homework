/*
 * user/user.ld — link script for user-space programs.
 *
 * User programs are linked at 0xC1000000 (USER_ELF_BASE in arch/i386/
 * paging.h): the 16MB band [0xC0000000, 0xC1000000) above it is reserved
 * as the user heap, and everything stays below USER_SPACE_TOP.  The
 * kernel loads the resulting ELF segments at these exact VAs.
 */
ENTRY(_start)

SECTIONS
{
    . = 0xC1000000;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(COMMON) *(.bss*) }
}
