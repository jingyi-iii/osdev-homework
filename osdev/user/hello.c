/*
 * user/hello.c — first user-mode ELF program.
 *
 * Runs in its own address space (loaded by kernel/elf.c), calls the
 * kernel only through the fixed syscall ABI (console output + yield +
 * a direct LOG to the user-mode log server), and never touches kernel
 * code or data directly.
 */
#include "userlib.h"

void _start(void)
{
    console_putstr("\n[user-elf] Hello from a real user-mode ELF!\n");
    console_putstr("[user-elf] Looping in user space (yield via syscall)...\n");

    /* Direct LOG through the user-mode log server's syscall (SYSCALL_LOG). */
    user_log_str("[user-elf] Direct LOG via the log server's SYSCALL_LOG!\n");

    for (;;)
        user_yield();
}
