#include "kernel/io.h"
#include "kernel/syscall.h"
#include "arch_irq.h"
#include "lib/module.h"
#include "lib/types.h"
#include "kernel/process.h"
#include "kernel/capability.h"

/* Syscall handle allocated by syscall_register() in io_syscall_init(). */
static i32 io_scall_handle = -1;

/*
 * Port I/O primitives.
 *
 * The in/out instructions are privileged (CPL0 only).  When the caller runs
 * in user mode (CPL3) the operation is forwarded to ring 0 through the
 * syscall gate (int $100); the io_syscall_isr() handler executes the
 * actual instruction at ring 0.  In kernel mode the instructions are
 * executed directly.
 */

u8 ioread8(u16 port)
{
    u8 data = 0;

    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        io_syscall_data cfg = {0};
        cfg.cmd  = IO_CTRL_IN8;
        cfg.port = port;
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
        return (u8)cfg.value;
    }

    /* Kernel mode (CPL0). */
    __asm__ volatile("inb %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

u16 ioread16(u16 port)
{
    u16 data = 0;

    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        io_syscall_data cfg = {0};
        cfg.cmd  = IO_CTRL_IN16;
        cfg.port = port;
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
        return (u16)cfg.value;
    }

    /* Kernel mode (CPL0). */
    __asm__ volatile("inw %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

u32 ioread32(u16 port)
{
    u32 data = 0;

    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        io_syscall_data cfg = {0};
        cfg.cmd  = IO_CTRL_IN32;
        cfg.port = port;
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
        return cfg.value;
    }

    /* Kernel mode (CPL0). */
    __asm__ volatile("inl %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

void iowrite8(u16 port, u8 value)
{
    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        io_syscall_data cfg = {0};
        cfg.cmd   = IO_CTRL_OUT8;
        cfg.port  = port;
        cfg.value = value;
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
        return;
    }

    /* Kernel mode (CPL0). */
    __asm__ volatile("outb %0, %1" : : "a"(value), "dN"(port));
}

void iowrite16(u16 port, u16 value)
{
    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        io_syscall_data cfg = {0};
        cfg.cmd   = IO_CTRL_OUT16;
        cfg.port  = port;
        cfg.value = value;
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
        return;
    }

    /* Kernel mode (CPL0). */
    __asm__ volatile("outw %0, %1" : : "a"(value), "dN"(port));
}

void iowrite32(u16 port, u32 value)
{
    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        io_syscall_data cfg = {0};
        cfg.cmd   = IO_CTRL_OUT32;
        cfg.port  = port;
        cfg.value = value;
        arch_syscall(io_scall_handle, &cfg, sizeof(cfg));
        return;
    }

    /* Kernel mode (CPL0). */
    __asm__ volatile("outl %0, %1" : : "a"(value), "dN"(port));
}

/*
 * ============================================================================
 * I/O syscall layer (RING3)
 *
 * ioread8/16/32 and iowrite8/16/32 are routed through this gate
 * whenever the caller runs in user mode
 * (CPL3).  The handler runs in kernel context, so the privileged in/out
 * instructions are executed at ring 0.  Since the handler itself runs with a
 * ring-0 CS, the ioread8/16/32 and iowrite8/16/32 calls inside it take the
 * direct path and never re-enter the gate.
 * ============================================================================
 */
static int io_syscall_isr(void* data)
{
    io_syscall_data* cfg = (io_syscall_data*)data;
    if (!cfg)
        return -E_INVAL;

    /*
     * Capability enforcement: a user (CPL3) process may only touch ports
     * covered by a CAP_ACCESS_IO grant.  CAP_ACCESS_IO is granted as a
     * port RANGE {port_base, port_count} (see the platform device table),
     * so the check is a simple range-membership test: is the single port
     * being accessed inside one of the granted ranges?  Kernel processes /
     * drivers are trusted and skip the check.  The handler runs in the
     * caller's context, so get_current_process() is the process behind the
     * syscall.
     */
    pcb* proc = get_current_process();
    if (proc && proc->priv != PROC_PRIV_KERNEL) {
        cap_io_port req = { cfg->port, 1 };
        if (cap_check(proc, CAP_ACCESS_IO, &req) != 0) {
            cfg->ret = -E_PERM;
            return cfg->ret;
        }
    }

    switch (cfg->cmd) {
    case IO_CTRL_IN8:
        cfg->value = ioread8(cfg->port);
        cfg->ret = 0;
        break;
    case IO_CTRL_IN16:
        cfg->value = ioread16(cfg->port);
        cfg->ret = 0;
        break;
    case IO_CTRL_IN32:
        cfg->value = ioread32(cfg->port);
        cfg->ret = 0;
        break;
    case IO_CTRL_OUT8:
        iowrite8(cfg->port, (u8)cfg->value);
        cfg->ret = 0;
        break;
    case IO_CTRL_OUT16:
        iowrite16(cfg->port, (u16)cfg->value);
        cfg->ret = 0;
        break;
    case IO_CTRL_OUT32:
        iowrite32(cfg->port, cfg->value);
        cfg->ret = 0;
        break;
    default:
        cfg->ret = -EINVAL;
        break;
    }

    return cfg->ret;
}

void io_syscall_init(void)
{
    io_scall_handle = syscall_register(io_syscall_isr, sizeof(io_syscall_data));
}

void io_syscall_exit(void)
{
    syscall_unregister(io_scall_handle);
}

module_init(io_syscall_init);
module_exit(io_syscall_exit);
