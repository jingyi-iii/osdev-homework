.global         kernel_start

.section        .bss
.skip   16384
stack_top:

.section        .text
kernel_start:
        movl    $stack_top,     %esp

        ## [+] load gdt
        call    init_gdt
        ## [-] load gdt

        ## [+] enable protected mode
        movl    %cr0,           %eax
        orl     $0x1,           %eax
        movl    %eax,           %cr0
        ## [-] enable protected mode

        ljmp    $8,             $kernel_start32
        hlt

.section        .text
kernel_start32:
        movw    $16,            %ax
        movw    %ax,            %ds
        movw    %ax,            %es
        movw    %ax,            %fs
        movw    %ax,            %gs
        movw    %ax,            %ss

        movl    $stack_top,     %esp
        call    init_idt
        call    init_8259a

        call    kernel_main
1:
        jmp     1b
        hlt

