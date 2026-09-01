# boot.S - Simple 16-bit real-mode bootloader
# Prints "Hello World" and halts.

.code16
.global _start

_start:
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp

halt:
    hlt
    jmp halt

