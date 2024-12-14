# after 0M
.set ALIGN,		1 << 0
.set MEMINFO,	1 << 1
.set FLAGS,		ALIGN | MEMINFO
.set MAGIC,		0x1BADB002
.set CHECKSUM,	-(MAGIC + FLAGS)
.set BL_OFT,	0x00400000	# 4M

.section .multiboot.data, "aw"
	.align	4
	.long	MAGIC
	.long	FLAGS
	.long	CHECKSUM

.section .multiboot.text, "a"
.global		_start
.type _start, @function
_start:
	movl	$(page_tbl - BL_OFT),	%edi
	movl	$0,		%esi
	movl	$1023,	%ecx

1:
	cmpl	$_kernel_start,	%esi
	jl		2f
	cmpl	$(_kernel_end - BL_OFT),	%esi
	jge		3f

	movl	%esi,	%edx
	orl		$0x003,	%edx
	movl	%edx,	(%edi)

2:
	addl	$4096,	%esi
	addl	$4,		%edi
	loop	1b

3:
	movl	$(0x000B8000 | 0x003),	page_tbl - BL_OFT + 1022 * 4

	# entry0: 0 -> 0x3FFFFF
	movl	$(page_tbl - BL_OFT + 0x003),	page_dir - BL_OFT + 0
	# entry1: 0x400000 -> 0x7FFFFF
	movl	$(page_tbl - BL_OFT + 0x003),	page_dir - BL_OFT + 4
	movl	$(page_dir - BL_OFT),			%ecx
	movl	%ecx,	%cr3

	movl	%cr0,	%ecx
	orl		$0x80010000,	%ecx
	movl	%ecx,	%cr0

	lea		4f,		%ecx
	jmp		*%ecx

# after 4M
.section .stack, "aw", @nobits
	.align	16
stack_bottom:
	.skip	16384
stack_top:

.section .bss, "aw", @nobits
	.align	4096
page_dir:
	.skip	4096
page_tbl:
	.skip	4096

.section .text
4:
	movl	%cr3,		%ecx
	movl	%ecx,		%cr3

	mov		$stack_top,	%esp

	call	kernel_main

	cli
1:	hlt
	jmp 	1b


