.global         kernel_start

# GDT & LDT ------------------------------------------------
# [15,  0]  segment limit1
# [39, 16]  segment base address1
# [55, 40]  G | D/B | 0 | AVL | segment limit2(4 bits) | P | DPL(2 bits) | S | TYPE(4 bits)
# [63, 56]  segment base address2
# ----------------------------------------------------------
.section        .data
gdtmeta:
        .word   2048
        .long   gdesc_null
    
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
        . = . + 2048 - 24

sel_syscode = gdesc_syscode - gdesc_null
sel_sysdata = gdesc_sysdata - gdesc_null

.section        .bss
.skip   16384
stack_top:

.section        .text
kernel_start:
        movl    $stack_top,     %esp

        ## [+] load gdt
        movl    $gdtmeta,       %eax
        lgdt    (%eax)
        ## [-] load gdt

        ## [+] enable protected mode
        movl    %cr0,           %eax
        orl     $0x1,           %eax
        movl    %eax,           %cr0
        ## [-] enable protected mode

        ljmp    $sel_syscode,   $kernel_start32
        hlt

.section        .text
kernel_start32:
        call    kernel_main
        hlt

