#include "kernel/io.h"
#include "kernel/syscall.h"
#include "kernel/uapi.h"
#include "arch_irq.h"
#include "regs.h"        /* arch_inb/inw/inl, arch_outb/outw/outl */
#include "lib/module.h"
#include "lib/types.h"
#include "kernel/process.h"
#include "kernel/capability.h"

/* Syscall handle allocated by syscall_register() in io_syscall_init(). */
static i32 io_scall_handle = -1;

/*
 * Port I/O primitives.
 *
 * The in/out instructions are privileged (CPL0 only), so only ring-0 code
 * may execute them.  Port I/O never blocks or switches context, so unlike
 * portal/mailbox there is no need to be inside a gate: io_run_direct() is
 * true for ANY ring-0 caller (kernel drivers, ISRs, even early boot before
 * the first task exists, where trapping via int $100 would be unsafe).
 */
static inline int io_run_direct(void)
{
    return !arch_running_ring3();
}

/*
 * Shared port I/O logic — the single place that maps an IO_CTRL_*
 * command to the actual in/out instruction.  It runs in two ways:
 *   - directly, from the public wrappers when io_run_direct() (any ring-0
 *     context), and
 *   - inside the syscall gate via io_syscall_isr() (ring-3 callers).
 *
 * There is deliberately NO capability check here: kernel callers are
 * trusted and must not be cap-checked against whatever process happens
 * to be scheduled.  io_syscall_isr() applies the CAP_ACCESS_IO check on
 * the ring-3 trap path only.
 */
static int io_exec(io_syscall_data* cfg)
{
    if (!cfg)
        return E_INVAL;

    switch (cfg->cmd) {
    case IO_CTRL_IN8:
        cfg->value = arch_inb(cfg->port);
        cfg->ret = 0;
        break;
    case IO_CTRL_IN16:
        cfg->value = arch_inw(cfg->port);
        cfg->ret = 0;
        break;
    case IO_CTRL_IN32:
        cfg->value = arch_inl(cfg->port);
        cfg->ret = 0;
        break;
    case IO_CTRL_OUT8:
        arch_outb(cfg->port, (u8)cfg->value);
        cfg->ret = 0;
        break;
    case IO_CTRL_OUT16:
        arch_outw(cfg->port, (u16)cfg->value);
        cfg->ret = 0;
        break;
    case IO_CTRL_OUT32:
        arch_outl(cfg->port, cfg->value);
        cfg->ret = 0;
        break;
    default:
        cfg->ret = E_INVAL;
        break;
    }

    return cfg->ret;
}

/*
 * Public API — thin wrappers.  Each builds a config and either runs the
 * shared io_exec() directly (ring 0) or traps through the io syscall gate
 * (ring 3), so ring-3 code never executes in/out itself and is always
 * subject to the CAP_ACCESS_IO gate in io_syscall_isr().
 */

u8 ioread8(u16 port)
{
    io_syscall_data cfg = {0};
    cfg.cmd  = IO_CTRL_IN8;
    cfg.port = port;

    if (io_run_direct())
        io_exec(&cfg);
    else
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));

    return (u8)cfg.value;
}

u16 ioread16(u16 port)
{
    io_syscall_data cfg = {0};
    cfg.cmd  = IO_CTRL_IN16;
    cfg.port = port;

    if (io_run_direct())
        io_exec(&cfg);
    else
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));

    return (u16)cfg.value;
}

u32 ioread32(u16 port)
{
    io_syscall_data cfg = {0};
    cfg.cmd  = IO_CTRL_IN32;
    cfg.port = port;

    if (io_run_direct())
        io_exec(&cfg);
    else
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));

    return cfg.value;
}

void iowrite8(u16 port, u8 value)
{
    io_syscall_data cfg = {0};
    cfg.cmd   = IO_CTRL_OUT8;
    cfg.port  = port;
    cfg.value = value;

    if (io_run_direct())
        io_exec(&cfg);
    else
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
}

void iowrite16(u16 port, u16 value)
{
    io_syscall_data cfg = {0};
    cfg.cmd   = IO_CTRL_OUT16;
    cfg.port  = port;
    cfg.value = value;

    if (io_run_direct())
        io_exec(&cfg);
    else
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
}

void iowrite32(u16 port, u32 value)
{
    io_syscall_data cfg = {0};
    cfg.cmd   = IO_CTRL_OUT32;
    cfg.port  = port;
    cfg.value = value;

    if (io_run_direct())
        io_exec(&cfg);
    else
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
}

/*
 * I/O syscall gate (SYSCALL_IO).  Ring-3 entry only: applies the
 * CAP_ACCESS_IO check, then defers to the shared io_exec().
 */
static int io_syscall_isr(void* data)
{
    io_syscall_data* cfg = (io_syscall_data*)data;
    if (!cfg)
        return E_INVAL;

    /*
     * CAP_ACCESS_IO gate: a user (CPL3) process may only touch ports
     * covered by a CAP_ACCESS_IO grant.  CAP_ACCESS_IO is granted as a
     * port RANGE {port_base, port_count}, so the check is a simple
     * range-membership test: is the single port being accessed inside one
     * of the granted ranges?  Kernel processes / drivers are trusted and
     * skip the check.  The handler runs in the caller's context, so
     * get_current_process() is the process behind the syscall.
     */
    pcb* proc = get_current_process();
    if (proc && proc->priv != PROC_PRIV_KERNEL) {
        cap_io_port req = { cfg->port, 1 };
        if (cap_check(proc, CAP_ACCESS_IO, &req) != 0) {
            cfg->ret = E_PERM;
            return cfg->ret;
        }
    }

    return io_exec(cfg);
}

void io_syscall_init(void)
{
    io_scall_handle = syscall_register(SYSCALL_IO,
        io_syscall_isr, sizeof(io_syscall_data));
}

void io_syscall_exit(void)
{
    syscall_unregister(io_scall_handle);
}

module_init(io_syscall_init);
module_exit(io_syscall_exit);
