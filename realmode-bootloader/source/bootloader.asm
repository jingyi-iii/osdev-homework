        org 7c00h
        jmp start
        nop

boot_msg:
        db      "[INTEL] Welcome to booting stage...",  0

start:
        mov     ax,             cs
        mov     ds,             ax
        mov     es,             ax
        mov     ss,             ax
        mov     sp,             7c00h
        
        call    disp_msg

        hlt
        jmp     $


disp_msg:
        push    ax
        push    si

        mov     ah,             0eh
        mov     si,             boot_msg
.loop:
        lodsb
        or      al,             al
        jz      .done

        int     10h
        jmp     .loop 

.done:
        pop     si
        pop     ax
        ret


times (510 - ($-$$)) db 0
Finish_Word     dw      0xaa55
