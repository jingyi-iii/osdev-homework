.globl  isr_table

.macro DECLARE_EXCEPTION irq_no
.align  32,     0x90
        movl    \irq_no,    %esi
        jmp     exception_handler
.align  32,     0x90
.endm

.macro DECLARE_INTERRUPT irq_no
.align  32,     0x90
        movl    \irq_no,    %esi
        jmp     interrupt_handler
.align  32,     0x90
.endm

.code32
.section        .text
.align 32
isr_table:
        DECLARE_EXCEPTION  $0x00
        DECLARE_EXCEPTION  $0x01
        DECLARE_EXCEPTION  $0x02
        DECLARE_EXCEPTION  $0x03
        DECLARE_EXCEPTION  $0x04
        DECLARE_EXCEPTION  $0x05
        DECLARE_EXCEPTION  $0x06
        DECLARE_EXCEPTION  $0x07
        DECLARE_EXCEPTION  $0x08
        DECLARE_EXCEPTION  $0x09
        DECLARE_EXCEPTION  $0x0a
        DECLARE_EXCEPTION  $0x0b
        DECLARE_EXCEPTION  $0x0c
        DECLARE_EXCEPTION  $0x0d
        DECLARE_EXCEPTION  $0x0e
        DECLARE_EXCEPTION  $0x0f
        DECLARE_EXCEPTION  $0x10
        DECLARE_EXCEPTION  $0x11
        DECLARE_EXCEPTION  $0x12
        DECLARE_EXCEPTION  $0x13
        DECLARE_EXCEPTION  $0x14
        DECLARE_EXCEPTION  $0x15
        DECLARE_EXCEPTION  $0x16
        DECLARE_EXCEPTION  $0x17
        DECLARE_EXCEPTION  $0x18
        DECLARE_EXCEPTION  $0x19
        DECLARE_EXCEPTION  $0x1a
        DECLARE_EXCEPTION  $0x1b
        DECLARE_EXCEPTION  $0x1c
        DECLARE_EXCEPTION  $0x1d
        DECLARE_EXCEPTION  $0x1e
        DECLARE_EXCEPTION  $0x1f
        DECLARE_INTERRUPT  $0x20
        DECLARE_INTERRUPT  $0x21
        DECLARE_INTERRUPT  $0x22
        DECLARE_INTERRUPT  $0x23
        DECLARE_INTERRUPT  $0x24
        DECLARE_INTERRUPT  $0x25
        DECLARE_INTERRUPT  $0x26
        DECLARE_INTERRUPT  $0x27
        DECLARE_INTERRUPT  $0x28
        DECLARE_INTERRUPT  $0x29
        DECLARE_INTERRUPT  $0x2a
        DECLARE_INTERRUPT  $0x2b
        DECLARE_INTERRUPT  $0x2c
        DECLARE_INTERRUPT  $0x2d
        DECLARE_INTERRUPT  $0x2e
        DECLARE_INTERRUPT  $0x2f

#
# Handle function for exceptions(IRQ0~IRQ31).
#
# Input  : None
# Output : None
#
.align  32
exception_handler:
        call    *isr_handlers(, %esi, 4)

        ## [+] eof signal
        movb    $0x20,      %al
        outb    %al,        $0x20
        ## [-] eof signal

        iret

#
# Handle function for interrupts(IRQ32~IRQ47).
#
# Input  : None
# Output : None
#
.align  32
interrupt_handler:
        call    *isr_handlers(, %esi, 4)

        ## [+] eof signal
        movb    $0x20,      %al
        outb    %al,        $0x20
        ## [-] eof signal

        iret

