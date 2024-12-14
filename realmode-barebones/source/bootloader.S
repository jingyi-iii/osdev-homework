.code16
.section    .text
.globl _start
_start:
        movw    %cs,            %ax
        movw    %ax,            %ds
        movw    %ax,            %es
        movw    %ax,            %ss
        call    disp_msp

        hlt
1:
        jmp     1b

disp_msp:
        push    %ax
        push    %si

        movb    $0xe,           %ah
        movw    $boot_msg,      %si
.loop:
        lodsb
        orb     %al,            %al
        jz      .done

        int     $0x10
        jmp     .loop
.done:
        pop     %si
        pop     %ax
        ret


boot_msg:
        .ascii "[AT&T] Welcome to booting stage...\n"
        .org 510
        .word 0xAA55
