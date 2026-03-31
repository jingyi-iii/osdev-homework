.globl  arch_isr_tbl

.macro SAVE_REGS_ADD_SWITCH_KERNEL_STACK
        pushal
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs

        testl   $3,             52(%esp)
        jz      from_kernel

from_user:

        # movl    %esp,           %esi
        # movl    $save_regs,     %edi
        # movl    $17,            %ecx
        # cld
        # rep     movsl

        jmp     switch_heap

from_kernel:

        movl    proc_run,       %eax
        movl    %esp,           (%eax)

switch_heap:
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
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs
        mov     $0x10,          %ax
        mov     %ax,            %ds
        mov     %ax,            %es
.endm

.macro RESTORE_REGS
        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popal
.endm

.macro DEFAULT_EXCEPTION irq_no
.align  256,     0x90
        hlt
.endm

.macro DEFAULT_INTERRUPT irq_no
.align  256,     0x90
        SAVE_REGS
        movl    \irq_no,        %eax
        call    mask_int

        movl    \irq_no,        %esi
        call    isr
        
        movl    \irq_no,        %eax
        call    unmask_int  
        RESTORE_REGS
        iret
.endm

.macro TIMER_INTERRUPT irq_no
.align  256,     0x90
        SAVE_REGS_ADD_SWITCH_KERNEL_STACK
        movl    \irq_no,        %eax
        call    mask_int

        movl    \irq_no,        %esi
        call    isr
        
        movl    \irq_no,        %eax
        call    unmask_int  
        RESTORE_REGS_AND_SWITCH_USER_STACK
        iret
.endm

.code32
.section        .text
.align  256,     0x90
arch_isr_tbl:
        DEFAULT_EXCEPTION       $0x00
        DEFAULT_EXCEPTION       $0x01
        DEFAULT_EXCEPTION       $0x02
        DEFAULT_EXCEPTION       $0x03
        DEFAULT_EXCEPTION       $0x04
        DEFAULT_EXCEPTION       $0x05
        DEFAULT_EXCEPTION       $0x06
        DEFAULT_EXCEPTION       $0x07
        DEFAULT_EXCEPTION       $0x08
        DEFAULT_EXCEPTION       $0x09
        DEFAULT_EXCEPTION       $0x0a
        DEFAULT_EXCEPTION       $0x0b
        DEFAULT_EXCEPTION       $0x0c
        DEFAULT_EXCEPTION       $0x0d
        DEFAULT_EXCEPTION       $0x0e
        DEFAULT_EXCEPTION       $0x0f
        DEFAULT_EXCEPTION       $0x10
        DEFAULT_EXCEPTION       $0x11
        DEFAULT_EXCEPTION       $0x12
        DEFAULT_EXCEPTION       $0x13
        DEFAULT_EXCEPTION       $0x14
        DEFAULT_EXCEPTION       $0x15
        DEFAULT_EXCEPTION       $0x16
        DEFAULT_EXCEPTION       $0x17
        DEFAULT_EXCEPTION       $0x18
        DEFAULT_EXCEPTION       $0x19
        DEFAULT_EXCEPTION       $0x1a
        DEFAULT_EXCEPTION       $0x1b
        DEFAULT_EXCEPTION       $0x1c
        DEFAULT_EXCEPTION       $0x1d
        DEFAULT_EXCEPTION       $0x1e
        DEFAULT_EXCEPTION       $0x1f
        TIMER_INTERRUPT         $0x20
        DEFAULT_INTERRUPT       $0x21
        DEFAULT_INTERRUPT       $0x22
        DEFAULT_INTERRUPT       $0x23
        DEFAULT_INTERRUPT       $0x24
        DEFAULT_INTERRUPT       $0x25
        DEFAULT_INTERRUPT       $0x26
        DEFAULT_INTERRUPT       $0x27
        DEFAULT_INTERRUPT       $0x28
        DEFAULT_INTERRUPT       $0x29
        DEFAULT_INTERRUPT       $0x2a
        DEFAULT_INTERRUPT       $0x2b
        DEFAULT_INTERRUPT       $0x2c
        DEFAULT_INTERRUPT       $0x2d
        DEFAULT_INTERRUPT       $0x2e
        DEFAULT_INTERRUPT       $0x2f

#
# Handle function for exceptions(IRQ0~IRQ31).
#
# Input  : None
# Output : None
#
.align  32
esr:
        # call    *user_isrs(, %esi, 4)
        ret

#
# Handle function for interrupts(IRQ32~IRQ47).
#
# Input  : None
# Output : None
#
.align  32
isr:
        # pushl   %esi
        # call    get_dev_by_irq_nr
        # addl    $4, %esp

        # pushl   %eax
        # call    *user_isrs(, %esi, 4)
        # addl    $4, %esp

        pushl   %esi
        call    irqline_handler
        addl    $4, %esp

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

#
# Diable hardware interrupt based on irq number
#
# Input  : eax, irq number (32~48)
# Output : None
#
.align  32
mask_int:
        pushl   %eax
        pushl   %ebx
        pushl   %ecx
        pushl   %edx

        movl    %eax,           %edx
        cmpl    $40,            %eax
        jnb     2f
1:
        in      $0x21,          %al     # ignore current interrupt (master)
        movb    %dl,            %cl
        subb    $32,            %cl
        movb    $1,             %bl
        shl     %cl,            %bl
        or      %bl,            %al
        out     %al,            $0x21
        jmp     3f
2:
        in      $0xa1,          %al     # ignore current interrupt (slave)
        movb    %dl,            %cl
        subb    $40,            %cl
        movb    $1,             %bl
        shl     %cl,            %bl
        orb     %bl,            %al
        out     %al,            $0xa1
3:
        popl    %edx
        popl    %ecx
        popl    %ebx
        popl    %eax
        ret

#
# Enable hardware interrupt based on irq number
#
# Input  : eax, irq number (32~48)
# Output : None
#
.align  32
unmask_int:
        pushl   %eax
        pushl   %ebx
        pushl   %ecx
        pushl   %edx

        movl    %eax,           %edx
        cmpl    $40,            %eax
        jnb     2f
1:                                      # master
        in      $0x21,          %al
        movb    %dl,            %cl
        subb    $32,            %cl
        movb    $1,             %bl
        shl     %cl,            %bl
        not     %bl
        and     %bl,            %al
        out     %al,            $0x21
        jmp     3f

2:                                      # slave
        in      $0xa1,          %al
        movb    %dl,            %cl
        sub     $40,            %cl
        movb    $1,             %bl
        shl     %cl,            %bl
        not     %bl
        and     %bl,            %al
        out     %al,            $0xa1
3:
        popl    %edx
        popl    %ecx
        popl    %ebx
        popl    %eax
        ret

