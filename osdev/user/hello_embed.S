/*
 * user/hello_embed.S — embed the user-mode ELF into the kernel image.
 *
 * Explicit alternative to `objcopy -I binary` magic `_binary_*` symbols:
 * the symbols below are named by hand and `.incbin` plainly shows which
 * file is being embedded.  The kernel references them as:
 *
 *   extern u8 user_hello_elf_start[];
 *   extern u8 user_hello_elf_end[];
 *
 * The path is relative to the makefile's working directory (project root),
 * so the assembler sees `output/hello.elf` as built by the $(USER_ELF)
 * rule.  The embed object explicitly depends on that target in the
 * makefile, so the ELF always exists before this file is assembled.
 */
    .section .rodata
    .align 4

    .globl user_hello_elf_start
user_hello_elf_start:
    .incbin "output/hello.elf"

    .globl user_hello_elf_end
user_hello_elf_end:

/* user-mode log server ELF — loaded by kernel/init.c via proc_load_from_elf
 * (log_server_elf_start/_end). */
    .globl log_server_elf_start
log_server_elf_start:
    .incbin "output/log_server.elf"

    .globl log_server_elf_end
log_server_elf_end:
