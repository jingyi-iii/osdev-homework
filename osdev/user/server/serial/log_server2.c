/*
 * user/server/serial/log_server2.c — user-mode log server (ring-3 ELF).
 *
 * Decoupled from platform_bus: this is a plain user server process, not a
 * platform driver.  On entry (_start) it claims the fixed SYSCALL_LOG
 * number via the SYSCALL_SYSCTL registry; from then on, whenever any user
 * program calls SYSCALL_LOG, syscall_dispatch() switches CR3 to this
 * process's page directory and runs log_syscall_isr() at ring 0.  The
 * handler writes the payload straight to COM1 (direct port I/O at CPL0).
 */
#include "userlib.h"          /* user_syscall() / user_yield() */
#include "kernel/uapi.h"      /* SYSCALL_SYSCTL / SYSCALL_LOG + configs */
#include "kernel/io.h"        /* ioread8() / iowrite8() */
#include "kernel/errno.h"     /* E_INVAL */
#include "user/uspinlock.h"
#include <stddef.h>           /* size_t */

#define SERIAL_COM1_BASE   0x3F8
#define SERIAL_LSR_OFF     5      /* Line Status Register offset  */
#define LSR_THR_EMPTY      0x20   /* Transmitter Holding Register empty */

/*
 * COM1 write with a user-mode spinlock for exclusion between the server
 * thread and concurrent syscall-handler invocations.
 *
 * COM1 is owned and initialised by the kernel's klog.  When this function
 * runs from the ring-3 server loop, ioread8()/iowrite8() go through the io
 * syscall gate (CAP_ACCESS_IO); when it runs inside log_syscall_isr()
 * (ring 0), they do direct in/out.
 */
static uspinlock ulog_lock = USPINLOCK_INIT;

static void ulog_write_direct(const char* buf, size_t size)
{
    uspin_lock(&ulog_lock);
    for (size_t i = 0; i < size; i++) {
        int timeout = 0;
        while ((ioread8(SERIAL_COM1_BASE + SERIAL_LSR_OFF) & LSR_THR_EMPTY) == 0) {
            /* If the transmitter never becomes ready (no UART / broken
             * status line), drop the rest instead of hanging forever. */
            if (++timeout > 1000000) {
                uspin_unlock(&ulog_lock);
                return;
            }
            __asm__ __volatile__("pause" ::: "memory");
        }
        iowrite8(SERIAL_COM1_BASE, (u8)buf[i]);
    }
    uspin_unlock(&ulog_lock);
}

/*
 * SYSCALL_LOG handler.
 *
 * Runs at ring 0 with THIS process's page tables (the kernel switched CR3
 * to the log server's directory), so this function's code and globals are
 * reachable.  The payload comes from the kernel's copy of the caller's
 * user_log_config (the kernel heap is in the low identity map shared by
 * every directory), so no cross-address-space dereference is needed.
 */
static int log_syscall_isr(void* data)
{
    user_log_config* cfg = (user_log_config*)data;
    if (!cfg || cfg->size > sizeof(cfg->data))
        return E_INVAL;

    ulog_write_direct(cfg->data, cfg->size);
    cfg->ret = 0;
    return 0;
}

/*
 * User-mode log server entry.
 *
 * 1. Claim SYSCALL_LOG with log_syscall_isr() as its handler.
 * 2. Idle forever — the actual logging happens in the syscall handler.
 */
void _start(void)
{
    user_sysctl_config sc = {0};
    sc.cmd            = U_SYSCTL_REGISTER;
    sc.num            = SYSCALL_LOG;
    sc.fn             = (void*)(uptr)log_syscall_isr;
    sc.max_param_size = sizeof(user_log_config);
    user_syscall(SYSCALL_SYSCTL, &sc, sizeof(sc));

    for (;;)
        user_yield();
}
