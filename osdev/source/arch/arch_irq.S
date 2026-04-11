.globl  arch_isr_tbl
.globl  syscall

.macro SAVE_REGS_ADD_SWITCH_KERNEL_STACK
        pushal
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs

        testl   $3,             52(%esp)
        jnz     1f

        # from kernel
        movl    curr_context,   %esi
        movl    %esp,           (%esi)

1:
        # from user
        mov     $0x10,          %si
        mov     %si,            %ds
        mov     %si,            %es
        movl    $stack_top,     %esp
.endm

.macro RESTORE_REGS_AND_SWITCH_USER_STACK
        movl    curr_context,   %esi
        movl    (%esi),         %esp

        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popal
.endm


.macro DECLARE_EXCEPTION irq_no
.align  256,     0x90
        hlt
.endm

.macro DECLARE_INTERRUPT irq_no
.align  256,     0x90
        SAVE_REGS_ADD_SWITCH_KERNEL_STACK
        movl    \irq_no,        %esi
        call    mask_int

        incl    k_reenter
        movl    k_reenter,      %esi
        cmp     $0,             %esi
        jne     1f

        movl    \irq_no,        %esi
        call    isr

1:
        decl    k_reenter
        
        movl    \irq_no,        %esi
        call    unmask_int  
        RESTORE_REGS_AND_SWITCH_USER_STACK
        iret
.endm

.code32
.section        .text
.align  256,     0x90
arch_isr_tbl:
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
        hlt
        ret

#
# Handle function for interrupts(IRQ32~IRQ47).
#
# Input  : None
# Output : None
#
.align  32
isr:
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
        pushl   %esi

        movl    %esi,           %edx
        cmpl    $40,            %esi
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
        popl    %esi
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
        pushl   %esi

        movl    %esi,           %edx
        cmpl    $40,            %esi
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
        popl    %esi
        popl    %edx
        popl    %ecx
        popl    %ebx
        popl    %eax
        ret


.align  256,     0x90
syscall:
        SAVE_REGS_ADD_SWITCH_KERNEL_STACK

        incl    k_reenter
        movl    k_reenter,      %esi
        cmp     $0,             %esi
        jne     1f

        call    syscall_handler

1:
        decl    k_reenter

        RESTORE_REGS_AND_SWITCH_USER_STACK
        iret
.endm