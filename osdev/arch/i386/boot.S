.set ALIGN,     1 << 0
.set MEMINFO,   1 << 1
.set FLAGS,     ALIGN | MEMINFO
.set MAGIC,     0x1BADB002
.set CHECKSUM,  -(MAGIC + FLAGS)

.section        .multiboot
.align          4
.long           MAGIC
.long           FLAGS
.long           CHECKSUM

.section        .data
.align          4
.globl          mboot_info
mboot_info:
        .long   0

.section	.text
.global		_start
.type _start,   @function
_start:
        cli
        /* GRUB (multiboot v1) passes the physical address of the
         * multiboot_info in EBX.  Save it before arch_start clobbers the
         * registers; kernel/init.c later reads the GRUB module list from
         * it (low memory is identity-mapped after arch_paging_init). */
        movl    %ebx, mboot_info
        jmp     arch_start
1:
        hlt
        jmp     1b

.size	_start, . - _start
