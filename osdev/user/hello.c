/*
 * user/hello.c — first user-mode ELF program.
 *
 * Runs in its own address space (loaded by kernel/elf.c), calls the
 * kernel only through the fixed syscall ABI (console output + yield +
 * a direct LOG to the user-mode log server), and never touches kernel
 * code or data directly.
 */
#include "userlib.h"

/*
 * Log a string directly to the user-mode log server via SYSCALL_LOG.
 *
 * The payload is carried INLINE in user_log_config (kernel/uapi.h), so the
 * server's ring-0 handler (running with the log server's page tables) can
 * write it from the kernel's copy without touching this process's memory.
 *
 * SYSCALL_LOG only exists once the log server's _start has claimed it, so
 * retry briefly — the same pattern console_putstr() uses for the portal.
 */
static void user_log(const char* s)
{
    user_log_config cfg = {0};
    u32 len = 0;

    if (!s)
        return;
    while (s[len] && len < sizeof(cfg.data) - 1)
        len++;
    cfg.size = len;
    for (u32 i = 0; i < len; i++)
        cfg.data[i] = s[i];

    for (int tries = 0; tries < 10000; tries++) {
        if (user_syscall(SYSCALL_LOG, &cfg, sizeof(cfg)) == 0)
            return;
        user_yield();
    }
}

void _start(void)
{
    console_putstr("\n[user-elf] Hello from a real user-mode ELF!\n");
    console_putstr("[user-elf] Looping in user space (yield via syscall)...\n");

    /* Direct LOG through the user-mode log server's syscall (SYSCALL_LOG). */
    user_log("[user-elf] Direct LOG via the log server's SYSCALL_LOG!\n");

    for (;;)
        user_yield();
}
