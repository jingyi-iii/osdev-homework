# boot.S - Simple 16-bit real-mode bootloader
# Prints "Hello World" and halts.

.code16
.global _start

_start:
    # Set up segments
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp

    # Print "Hello World"
    movw $msg, %si
    call print_string

    # Halt the CPU
halt:
    hlt
    jmp halt

# print_string - Print a null-terminated string via BIOS interrupt
# Input: SI = pointer to string
print_string:
    pusha
.print_loop:
    lodsb               # Load byte from [SI] into AL, increment SI
    testb %al, %al      # Check for null terminator
    jz .done
    movb $0x0e, %ah     # BIOS teletype function
    movb $0x00, %bh     # Page number
    int $0x10           # Video BIOS interrupt
    jmp .print_loop
.done:
    popa
    ret

msg:
    .asciz "Hello World"
