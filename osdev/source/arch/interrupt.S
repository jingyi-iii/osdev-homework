.globl  sys_isr_tbl

.macro SAVE_REGS
    pushal
    pushl       %ds
    pushl       %es
    pushl       %fs
    pushl       %gs
    mov         $0x10,          %ax
    mov         %ax,            %ds
    mov         %ax,            %es
.endm

.macro RESTORE_REGS
    popl        %gs
    popl        %fs
    popl        %es
    popl        %ds
    popal
.endm

.macro DECLARE_EXCEPTION irq_no
.align  64,     0x90
        SAVE_REGS
        movl    \irq_no,        %esi
        call    esr
        RESTORE_REGS
        iret
.endm

.macro DECLARE_INTERRUPT irq_no
.align  64,     0x90
        SAVE_REGS
        movl    \irq_no,        %esi
        call    isr
        RESTORE_REGS
        iret
.endm

.code32
.section        .text
.align 32
sys_isr_tbl:
        DECLARE_EXCEPTION       $0x00
        DECLARE_EXCEPTION       $0x01
        DECLARE_EXCEPTION       $0x02
        DECLARE_EXCEPTION       $0x03
        DECLARE_EXCEPTION       $0x04
        DECLARE_EXCEPTION       $0x05
        DECLARE_EXCEPTION       $0x06
        DECLARE_EXCEPTION       $0x07
        DECLARE_EXCEPTION       $0x08
        DECLARE_EXCEPTION       $0x09
        DECLARE_EXCEPTION       $0x0a
        DECLARE_EXCEPTION       $0x0b
        DECLARE_EXCEPTION       $0x0c
        DECLARE_EXCEPTION       $0x0d
        DECLARE_EXCEPTION       $0x0e
        DECLARE_EXCEPTION       $0x0f
        DECLARE_EXCEPTION       $0x10
        DECLARE_EXCEPTION       $0x11
        DECLARE_EXCEPTION       $0x12
        DECLARE_EXCEPTION       $0x13
        DECLARE_EXCEPTION       $0x14
        DECLARE_EXCEPTION       $0x15
        DECLARE_EXCEPTION       $0x16
        DECLARE_EXCEPTION       $0x17
        DECLARE_EXCEPTION       $0x18
        DECLARE_EXCEPTION       $0x19
        DECLARE_EXCEPTION       $0x1a
        DECLARE_EXCEPTION       $0x1b
        DECLARE_EXCEPTION       $0x1c
        DECLARE_EXCEPTION       $0x1d
        DECLARE_EXCEPTION       $0x1e
        DECLARE_EXCEPTION       $0x1f
        DECLARE_INTERRUPT       $0x20
        DECLARE_INTERRUPT       $0x21
        DECLARE_INTERRUPT       $0x22
        DECLARE_INTERRUPT       $0x23
        DECLARE_INTERRUPT       $0x24
        DECLARE_INTERRUPT       $0x25
        DECLARE_INTERRUPT       $0x26
        DECLARE_INTERRUPT       $0x27
        DECLARE_INTERRUPT       $0x28
        DECLARE_INTERRUPT       $0x29
        DECLARE_INTERRUPT       $0x2a
        DECLARE_INTERRUPT       $0x2b
        DECLARE_INTERRUPT       $0x2c
        DECLARE_INTERRUPT       $0x2d
        DECLARE_INTERRUPT       $0x2e
        DECLARE_INTERRUPT       $0x2f

#
# Handle function for exceptions(IRQ0~IRQ31).
#
# Input  : None
# Output : None
#
.align  32
esr:
        call    *user_isrs(, %esi, 4)
        ret

#
# Handle function for interrupts(IRQ32~IRQ47).
#
# Input  : None
# Output : None
#
.align  32
isr:
        call    *user_isrs(, %esi, 4)

        cmpl    $0x28,          %esi
        jl      .master
        ## [+] eof signal to slave
        movb    $0x20,          %al
        outb    %al,            $0xA0
        ## [-] eof signal to slave
.master:
        ## [+] eof signal to master
        movb    $0x20,          %al
        outb    %al,            $0x20
        ## [-] eof signal to master

        ret

