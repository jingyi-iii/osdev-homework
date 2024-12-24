.globl  _start

.section    .text
_start:
        jmp     main

# GDT & LDT ------------------------------------------------
# [15,  0]  segment limit1
# [39, 16]  segment base address1
# [55, 40]  G | D/B | 0 | AVL | segment limit2(4 bits) | P | DPL(2 bits) | S | TYPE(4 bits)
# [63, 56]  segment base address2
# ----------------------------------------------------------
.section    .text
gdesc_null:
        .word	0b0000000000000000
        .word	0b0000000000000000
        .word	0b0000000000000000
        .word	0b0000000000000000
gdesc_syscode:
        .word	0b1111111111111111
        .word	0b0000000000000000
        .word	0b1001101000000000
        .word	0b0000000011001111
gdesc_sysdata:
        .word	0b1111111111111111
	    .word	0b0000000000000000
	    .word	0b1111001000000000
	    .word	0b0000000011001111
gdesc_ptr:
        .word   . - gdesc_null - 1
        .long   gdesc_null

sel_syscode = gdesc_syscode - gdesc_null
sel_sysdata = gdesc_sysdata - gdesc_null


.code16
.section    .text
main:
        movw    %cs,            %ax
        movw    %ax,            %es
        movw    %ax,            %ss
        movw    %ax,            %fs
        movw    $0,             %sp

        ## [+] show boot message in real mode stage
        movw    %cs,            %ax
        movw    %ax,            %ds
        movw    $bootmsg16,     %si
        movw    $0xb800,        %ax
        movw    %ax,            %es
        movl    $0,             %edi

        movb    $0xe,           %ah
1:
        lodsb
        orb     %al,            %al
        jz      2f

        movw    %ax,            %es:(%edi)
        addl    $2,             %edi
        jmp     1b
2:
        ## [-] show boot message in real mode stage

        ## [+] load gdt
        movl    $gdesc_ptr,     %eax
        xor     %ebx,           %ebx
        movw    %ds,            %bx
        shl     $4,             %ebx
        subl    %ebx,           %eax
        cli
        lgdt    %ds:(%eax)
        ## [-] load gdt

        ## [+] enable protected mode
        movl    %cr0,           %eax
        orl     $0x1,           %eax
        movl    %eax,           %cr0
        ## [-] enable protected mode

        ljmp    $sel_syscode,    $code32

        hlt
1:
        jmp     1b

bootmsg16:
        .ascii  "[Bootloader] Welcome to real mode stage...\0"


.code32
.section        .text
code32:
        ## [+] show boot message in protected mode stage
        movw    $sel_sysdata,   %ax
        movw    %ax,            %ds
        movw    %ax,            %es

        movl    $bootmsg32,     %esi
        movl    $160,           %edi
        addl    $0xb8000,       %edi

        movb    $0xe,           %ah
1:
        lodsb
        orb     %al,            %al
        jz      2f

        movw    %ax,            %es:(%edi)
        addl    $2,             %edi
        jmp     1b
2:
        ## [-] show boot message in protected mode stage

        hlt
3:
        jmp     3b

bootmsg32:
        .ascii  "[Bootloader] Welcome to protected mode stage...\0"

        .org    510
        .word   0xAA55
