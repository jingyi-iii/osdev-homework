/*
 * user/hello.c — first user-mode ELF program.
 *
 * Runs in its own address space (loaded by kernel/elf.c), calls the
 * kernel only through the fixed syscall ABI (console output + yield),
 * and never touches kernel code or data directly.
 */
#include "userlib.h"

void _start(void)
{
    console_putstr("\n[user-elf] Hello from a real user-mode ELF!\n");
    console_putstr("[user-elf] Looping in user space (yield via syscall)...\n");

    for (;;)
        user_yield();
}
