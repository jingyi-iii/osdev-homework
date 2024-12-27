.set ALIGN,		1 << 0
.set MEMINFO,	1 << 1
.set FLAGS,		ALIGN | MEMINFO
.set MAGIC,		0x1BADB002
.set CHECKSUM,	-(MAGIC + FLAGS)

.section .multiboot
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section	.text
.global		_start
.type _start, @function
_start:
        jmp     kernel_start
	    hlt

.size	_start, . - _start
