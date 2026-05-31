/* bootloader/linker.ld - Linker script for flat binary bootloader */
OUTPUT_FORMAT("binary")
SECTIONS
{
    . = 0x7c00;
    .text : {
        *(.text)
        . = 510;
        SHORT(0xAA55);
    }
}
