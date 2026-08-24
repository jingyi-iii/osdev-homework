.globl  arch_isr_tbl
.globl  arch_syscall_entry

.section        .bss
.skip           16384
irq_stack_top:

.macro SAVE_REGS_ADD_SWITCH_KERNEL_STACK
        pushal
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs

        testl   $3,             52(%esp)
        jnz     1f

        # from kernel
        movl    curr_task_ctx,   %esi
        testl   %esi,           %esi
        jz      1f
        movl    %esp,           (%esi)

1:
        # from user, or current_context is null
        mov     $0x10,          %si
        mov     %si,            %ds
        mov     %si,            %es
        movl    $irq_stack_top, %esp
.endm

.macro RESTORE_REGS_AND_SWITCH_USER_STACK
        movl    curr_task_ctx,   %esi
        movl    (%esi),         %esp

        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popal
.endm

/*
 * Syscall-only restore: pops every register EXCEPT EAX.  pushal saves
 * EAX first, so it sits at the deepest stack slot and would be restored
 * by popal — which would clobber syscall_dispatch's return value that
 * lives in EAX.  Skipping the saved EAX slot lets iret hand the return
 * value straight back to the caller, with no magic offset writes.
 * Layout matches the C `regs` struct (arch_task.h): gs/fs/es/ds then
 * edi/esi/ebp/kesp/ebx/edx/ecx/eax then the CPU frame.
 */
.macro RESTORE_REGS_KEEP_EAX
        movl    curr_task_ctx,   %esi
        movl    (%esi),         %esp

        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popl    %edi
        popl    %esi
        popl    %ebp
        addl    $4,             %esp       /* skip saved kesp */
        popl    %ebx
        popl    %edx
        popl    %ecx
        addl    $4,             %esp       /* skip saved eax — EAX keeps the return value */
.endm


.macro DECLARE_EXCEPTION_NOERR irq_no
.align  256,     0x90
        pushl   $0
        pushal
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs

        movw    $0x10,          %ax
        movw    %ax,            %ds
        movw    %ax,            %es

        pushl   %esp
        pushl   \irq_no
        call    exception_handler
        addl    $8,             %esp

        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popal
        addl    $4,             %esp
        iret
.endm

.macro DECLARE_EXCEPTION_ERR irq_no
.align  256,     0x90
        pushal
        pushl   %ds
        pushl   %es
        pushl   %fs
        pushl   %gs

        movw    $0x10,          %ax
        movw    %ax,            %ds
        movw    %ax,            %es

        pushl   %esp
        pushl   \irq_no
        call    exception_handler
        addl    $8,             %esp

        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popal
        addl    $4,             %esp
        iret
.endm

.macro DECLARE_INTERRUPT irq_no
.align  256,     0x90
        SAVE_REGS_ADD_SWITCH_KERNEL_STACK

        pushl   \irq_no
        call    arch_mask_irq
        addl    $4,             %esp

        incl    irq_reenter_cnt
        movl    irq_reenter_cnt,%esi
        cmp     $0,             %esi
        jne     1f

        pushl   $0
        pushl   $0
        pushl   \irq_no
        call    irqline_handler
        addl    $12,            %esp

        movl    \irq_no,       %eax
        cmpl    $0x28,          %eax
        jl      2f
        movb    $0x20,          %al
        outb    %al,            $0xA0
2:
        movb    $0x20,          %al
        outb    %al,            $0x20

        decl    irq_reenter_cnt

        /*
         * Threaded irq (irqline_handler set irq_defer_unmask): keep the
         * PIC line masked — the handler thread unmask it after it runs —
         * and kick the scheduler so the woken thread runs promptly.
         * Synchronous irq: unmask here, as before.
         */
        movl    irq_defer_unmask, %eax
        testl   %eax, %eax
        jnz     3f

        pushl   \irq_no
        call    arch_unmask_irq
        addl    $4,             %esp
        jmp     4f
3:
        call    schedule_from_isr
        jmp     4f

1:
        decl    irq_reenter_cnt

4:
        RESTORE_REGS_AND_SWITCH_USER_STACK
        iret
.endm

.code32
.section        .text
.align  256,     0x90
arch_isr_tbl:
        DECLARE_EXCEPTION_NOERR  $0x00
        DECLARE_EXCEPTION_NOERR  $0x01
        DECLARE_EXCEPTION_NOERR  $0x02
        DECLARE_EXCEPTION_NOERR  $0x03
        DECLARE_EXCEPTION_NOERR  $0x04
        DECLARE_EXCEPTION_NOERR  $0x05
        DECLARE_EXCEPTION_NOERR  $0x06
        DECLARE_EXCEPTION_NOERR  $0x07
        DECLARE_EXCEPTION_ERR    $0x08
        DECLARE_EXCEPTION_NOERR  $0x09
        DECLARE_EXCEPTION_ERR    $0x0A
        DECLARE_EXCEPTION_ERR    $0x0B
        DECLARE_EXCEPTION_ERR    $0x0C
        DECLARE_EXCEPTION_ERR    $0x0D
        DECLARE_EXCEPTION_ERR    $0x0E
        DECLARE_EXCEPTION_NOERR  $0x0F
        DECLARE_EXCEPTION_NOERR  $0x10
        DECLARE_EXCEPTION_ERR    $0x11
        DECLARE_EXCEPTION_NOERR  $0x12
        DECLARE_EXCEPTION_NOERR  $0x13
        DECLARE_EXCEPTION_NOERR  $0x14
        DECLARE_EXCEPTION_NOERR  $0x15
        DECLARE_EXCEPTION_NOERR  $0x16
        DECLARE_EXCEPTION_NOERR  $0x17
        DECLARE_EXCEPTION_NOERR  $0x18
        DECLARE_EXCEPTION_NOERR  $0x19
        DECLARE_EXCEPTION_NOERR  $0x1A
        DECLARE_EXCEPTION_NOERR  $0x1B
        DECLARE_EXCEPTION_NOERR  $0x1C
        DECLARE_EXCEPTION_NOERR  $0x1D
        DECLARE_EXCEPTION_ERR    $0x1E
        DECLARE_EXCEPTION_NOERR  $0x1F
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

.align  256,     0x90
arch_syscall_entry:
        SAVE_REGS_ADD_SWITCH_KERNEL_STACK

        incl    irq_reenter_cnt
        movl    irq_reenter_cnt,%esi
        cmp     $0,             %esi
        jne     1f

        /* syscall dispatch: ebx = minor, ecx = arg, edx = size */
        pushl   %edx
        pushl   %ecx
        pushl   %ebx
        call    syscall_dispatch
        addl    $12,            %esp

        /*
         * EAX now holds syscall_dispatch's return value.
         * RESTORE_REGS_KEEP_EAX restores every register except EAX, so
         * iret returns the syscall result in EAX to the caller.  The
         * reentrant path (jne above) keeps the original EAX untouched.
         */
1:
        decl    irq_reenter_cnt

        RESTORE_REGS_KEEP_EAX
        iret
