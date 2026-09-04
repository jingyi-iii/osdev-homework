/*
 * user/server/serial/log_server2.c — user-mode log server (ring-3 ELF).
 *
 * A plain portal service (the user syscall registry is gone): on entry
 * (_start) it publishes a DYNAMIC portal, registers it in the namespace
 * under "log", then serves it forever — every portal_call() payload it
 * receives is written straight to COM1.
 *
 * The kernel only mediates the RPC (kernel/ipc/portal.c): portal_call()
 * shm-maps the caller's buffer into this process's address space, so the
 * payload is read here with no copy.  COM1 writes go through
 * ioread8()/iowrite8() (provided by user_service.c) → SYSCALL_IO at ring
 * 3, so the process needs a CAP_ACCESS_IO grant for {0x3F8, 8} plus
 * CAP_IPC for the portal gate (grant_log_caps in kernel/init.c).
 */
#include "userlib.h"          /* user_syscall() / portal ABI          */
#include "kernel/uapi.h"      /* PORTAL_ID_NAMESPACE              */
#include "kernel/io.h"        /* ioread8()/iowrite8() (user_service.c) */
#include "user/uspinlock.h"   /* user-mode spinlock                    */
#include <stddef.h>           /* size_t */

#define SERIAL_COM1_BASE   0x3F8
#define SERIAL_LSR_OFF     5      /* Line Status Register offset  */
#define LSR_THR_EMPTY      0x20   /* Transmitter Holding Register empty */

/* Serialises COM1 writes between portal-loop invocations. */
static uspinlock ulog_lock = USPINLOCK_INIT;

static void ulog_write(const char* buf, size_t size)
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
 * User-mode log server entry.
 *
 * 1. Publish a DYNAMIC portal and register it in the namespace under
 *    "log" (same pattern as the terminal server's "console").
 * 2. Serve it forever: each request carries a raw byte buffer shm-mapped
 *    into this address space; write it to COM1 and reply 0.
 */
void _start(void)
{
    user_portal_ctrl cfg = {0};
    int tries;

    cfg.cmd       = U_PORTAL_CTRL_INIT;
    cfg.server_id = 0;                     /* dynamic id, registered below */
    if (user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg)) != 0) {
        for (;;)
            user_yield();
    }

    /* Publish under the namespace name "log" so clients can resolve us. */
    tries = 0;
    while (tries++ < 10000 &&
           ns_register(NS_NAME_LOG, (u32)(uptr)cfg.out, 0, 0) != 0)
        user_yield();

    for (;;) {
        cfg.cmd = U_PORTAL_CTRL_WAIT;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

        cfg.cmd = U_PORTAL_CTRL_GET_REQ;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
        if (!cfg.req)
            continue;

        if (cfg.va && cfg.va_size)
            ulog_write((const char*)cfg.va, cfg.va_size);

        cfg.cmd = U_PORTAL_CTRL_REPLY;
        cfg.ret = 0;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    }
}

