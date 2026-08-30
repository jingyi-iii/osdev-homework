/*
 * user/user.ld — link script for user-space programs.
 *
 * User programs are linked at a high user address (well above the
 * kernel's low identity map and below USER_SPACE_TOP = 0xC0000000).
 * The kernel loads the resulting ELF segments at these exact VAs.
 */
ENTRY(_start)

SECTIONS
{
    . = 0xA0000000;

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(COMMON) *(.bss*) }
}
