.globl  timer_isr
.globl  keyboard_isr

.macro SAVE_REGS_ADD_SWITCH_KERNEL_STACK
        pushal
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs

        movl    %esp,           %esi
        movl    $save_regs,     %edi
        movl    $17,            %ecx
        cld
        rep     movsl

        mov     $0x10,          %ax
        mov     %ax,            %ds
        mov     %ax,            %es
        movl    $stack_top,     %esp
.endm

.macro RESTORE_REGS_AND_SWITCH_USER_STACK
        movl    proc_run,       %eax
        movl    (%eax),         %esp

        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popal
.endm

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


.macro TIMER_INTERRUPR irq_no
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
timer_isr:
.align  64,     0x90
        SAVE_REGS_ADD_SWITCH_KERNEL_STACK
        movl    $0x20,        %esi
        call    *user_isrs(, %esi, 4)
        call    eoi
        RESTORE_REGS_AND_SWITCH_USER_STACK
        iret

keyboard_isr:
.align  64,     0x90
        SAVE_REGS
        movl    $0x21,        %esi
        call    *user_isrs(, %esi, 4)
        call    eoi
        RESTORE_REGS
        iret


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
eoi:
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

