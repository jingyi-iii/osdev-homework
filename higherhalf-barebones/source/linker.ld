ENTRY (_start)

SECTIONS
{
    . = 0x00000000;
	_kernel_start = .;
        .multiboot.data : {
            *(.multiboot.data)
        }

       .multiboot.text : {
           *(.multiboot.text)
       }


	. += 4M;
	.text ALIGN (4K) : AT (ADDR (.text) - 4M)
	{
		*(.text)
	}
	.rodata ALIGN (4K) : AT (ADDR (.rodata) - 4M)
	{
		*(.rodata)
	}
	.data ALIGN (4K) : AT (ADDR (.data) - 4M)
	{
		*(.data)
	}
	.bss ALIGN (4K) : AT (ADDR (.bss) - 4M)
	{
		*(COMMON)
		*(.bss)
		*(.stack)
	}

	_kernel_end = .;
}
