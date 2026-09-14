/*
 * user/server/user_service.c — user-mode service layer for the
 * DRIVER_CLASS_USER servers (standalone ring-3 ELF programs).
 *
 * The *_server drivers are now compiled as separate user ELFs (linked
 * with user/user.ld), so they can no longer call the kernel-image
 * implementations (kernel/io.c, kernel/process.c, drivers/...).  This
 * file provides the user-mode side of the APIs they use:
 *
 *   - real syscall-gate wrappers:
 *       ioread8()/iowrite8()   → SYSCALL_IO          (privileged in/out
 *                                                      executed at ring 0)
 *       thread_yield()         → SYSCALL_PROC_THREAD
 *   - ring-3-safe stubs for ring-0-only entry points that a standalone
 *     user ELF must still link but can never legitimately execute:
 *       klog_*                 (cli/sti guarded → would #GP at CPL3)
 *       timer_is_ready()       → 0, so LOG() skips the timestamp exactly
 *                                like early boot (no timer syscall gate)
 *       timer_read_time_str()  → -1 (never reached while not ready)
 *
 * These have the same signatures as their kernel-side counterparts (see
 * kernel/io.h, kernel/process.h, kernel/klog.h); they are what the
 * standalone ELF links against instead.
 */

#include "userlib.h"              /* user_syscall(), user_proc_ctrl, U_* */
#include "kernel/io.h"            /* io_syscall_data + io_syscall_cmd */
#include "kernel/klog.h"          /* klog_init/klog_write prototypes */
#include <stddef.h>               /* size_t */

/*
 * Port I/O.  These mirror the kernel-side ioread8()/iowrite8()
 * (kernel/io.c) purely through the SYSCALL_IO gate: the kernel executes
 * the privileged in/out at ring 0 and enforces CAP_ACCESS_IO port-range
 * grants.
 *
 * (The old `!arch_running_ring3()` raw in/out shortcut is gone: it only
 * served the removed design where user-registered syscall handlers ran
 * a server's code at CPL0, and it turned a boot-time register-delivery
 * race into a fatal #GP when its check came back wrong in ring 3 — see
 * ukernel.md GOTCHA 13.)
 */
u8 ioread8(u16 port)
{
    io_syscall_data cfg = {0};
    cfg.cmd  = IO_CTRL_IN8;
    cfg.port = port;
    user_syscall(SYSCALL_IO, &cfg, sizeof(cfg));
    return (u8)cfg.value;
}

void iowrite8(u16 port, u8 value)
{
    io_syscall_data cfg = {0};
    cfg.cmd   = IO_CTRL_OUT8;
    cfg.port  = port;
    cfg.value = value;
    user_syscall(SYSCALL_IO, &cfg, sizeof(cfg));
}

/* ---- thread control ---- */

void thread_yield(void)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_THREAD_CTRL_YIELD;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

/* ---- ring-3 stubs for ring-0-only entry points ---- */

void klog_init(void)
{
    /* COM1 is owned and brought up by the kernel's klog_init() at boot;
     * a user ELF must not touch it with cli/sti. */
}

void klog_write(const char* s)
{
    /* Ring-0 only (cli/sti → #GP at CPL3).  The ring-3 LOG path in
     * log_server2.c writes COM1 itself (via the io syscall gate). */
    (void)s;
}

int timer_is_ready(void)
{
    /* No timer syscall gate yet: report "not ready" so LOG() skips the
     * timestamp, exactly like early boot. */
    return 0;
}

int timer_read_time_str(char* buf, size_t size)
{
    /* Never reached while timer_is_ready() == 0. */
    (void)buf;
    (void)size;
    return -1;
}
