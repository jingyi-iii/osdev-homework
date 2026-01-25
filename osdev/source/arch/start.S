.global         arch_start
.global         stack_top

.section        .bss
.skip           16384
stack_top:

.equ            SYS_CODE,       1
.equ            SYS_DATA,       2
.equ            USER_CODE,      3
.equ            USER_DATA,      4

.section        .text
arch_start:
        movl    $stack_top,     %esp
        call    arch_om_switch_pm

        # get syscode selector
        movl    $SYS_CODE,      %eax
        pushl   %eax
        call    arch_om_get_sel
        addl    $4,             %esp

        # jump to code32
        pushl   %eax
        pushl   $knl32_entry
        ljmp    *(%esp)
        addl    $8,             %esp
        hlt

.section        .text
knl32_entry:
        movl    $SYS_DATA,      %eax
        pushl   %eax
        call    arch_om_get_sel
        addl    $4,             %esp

        movw    %ax,            %ds
        movw    %ax,            %es
        movw    %ax,            %fs
        movw    %ax,            %gs
        movw    %ax,            %ss

        movl    $stack_top,     %esp
        call    arch_init_idt
        call    arch_init_8259a
        call    arch_init_serial
        call    kernel_start

        movl    proc_run,       %eax
        movl    (%eax),         %esp
        popl    %gs
        popl    %fs
        popl    %es
        popl    %ds
        popa
        iret
        hlt

