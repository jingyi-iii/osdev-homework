#include "arch_irq.h"
#include "kernel/irq.h"
#include "kernel/syscall.h"
#include "lib/string.h"
#include "lib/module.h"
#include "drivers/log_server.h"
#include "mm/heap.h"
#include "kernel/capability.h"
#include "kernel/process.h"
#include "ipc/mailbox.h"
#include <stdint.h>

/* Syscall handle allocated by syscall_register() in irq_syscall_init(). */
static i32 irq_scall_handle = -1;

static irqline* irqlines[IDT_ENTRIES] = {0};

static int irqline_alloc(u32 major, irqline **out)
{
    if (!out)
        return E_INVAL;

    irqline *line = (irqline*)kmalloc(sizeof(irqline));
    if (!line)
        return E_NOMEM;

    line->major = major;
    line->enabled = 0;
    line->sp_lock = spinlock_alloc();
    list_init(&line->irqs);

    *out = line;

    return 0;
}

static int irqline_free(irqline *line)
{
    if (!line)
        return E_INVAL;

    spinlock_release(line->sp_lock);
    kfree(line);
    return 0;
}

static int irqline_mask(struct irqline* line)
{
    if (!line)
        return E_INVAL;

    int disable = 1;

    list_for_each(each, &line->irqs) {
        irq* p = list_entry(each, irq, node);
        if (p->enabled) {
            disable = 0;
            break;
        }
    }

    if (disable) {
        spinlock_lock(line->sp_lock);
        arch_mask_irq(line->major);
        spinlock_unlock(line->sp_lock);
    }

    return 0;
}

static int irqline_unmask(struct irqline* line)
{
    if (!line)
        return E_INVAL;

    list_for_each(each, &line->irqs) {
        irq* p = list_entry(each, irq, node);
        if (p->enabled) {
            spinlock_lock(line->sp_lock);
            arch_unmask_irq(line->major);
            spinlock_unlock(line->sp_lock);
            break;
        }
    }

    return 0;
}

static int irqline_add_irq(struct irqline* line, struct irq* p)
{
    if (!line || !p)
        return E_INVAL;

    spinlock_lock(line->sp_lock);
    list_add(&p->node, &line->irqs);
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_remove_irq(struct irqline* line, struct irq* p)
{
    if (!line || !p)
        return E_INVAL;

    spinlock_lock(line->sp_lock);
    list_del(&p->node);
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_remove_all(struct irqline* line)
{
    if (!line)
        return E_INVAL;

    spinlock_lock(line->sp_lock);
    list_for_each_safe(node, next, &line->irqs) {
        list_del(node);
    }
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_init(irqline** out_line, u32 major)
{
    if (!out_line || major >= IDT_ENTRIES)
        return E_INVAL;

    return irqline_alloc(major, out_line);
}

static void irqline_release(irqline* line)
{
    if (!line)
        return;

    irqline_remove_all(line);
    list_del(&line->irqs);
    spinlock_release(line->sp_lock);
    memset(line, 0, sizeof(irqline));
}

/*
 * Find the first free (unused) minor number on the given irqline.
 * Returns IRQ_ANY_MINOR if none is available.
 */
static u32 irqline_find_free_minor(struct irqline* line)
{
    for (u32 candidate = 0; candidate < UINT32_MAX; candidate++) {
        int used = 0;
        list_for_each(each, &line->irqs) {
            irq* p = list_entry(each, irq, node);
            if (p->minor == candidate) {
                used = 1;
                break;
            }
        }
        if (!used)
            return candidate;
    }
    return IRQ_ANY_MINOR;
}

static void dispatch_user_mode_irq(irq* p)
{
    /* dispatch by mailbox: deliver to the thread that
        * registered the IRQ (cached owner tcb, no schedule_lock) */
    tcb* t = (tcb*)p->owner;
    if (!t || !t->mailbox)
        return;

    mail* m = alloc_mail();
    if (!m)
        return;

    m->type = MAIL_TYPE_IRQ;
    m->receiver_tid = p->tid;

    send_mail(t->mailbox, m);
}

void irqline_handler(u32 major, u32 minor, void* context)
{
    /* Syscalls no longer flow through this path: int $100 goes straight to
     * syscall_dispatch() (see irq.S / kernel/syscall.c).  This handler now
     * only serves real hardware IRQ lines. */
    (void)minor;
    (void)context;

    if (!irqlines[major])
        return;

    list_for_each(each, &irqlines[major]->irqs) {
        irq* p = list_entry(each, irq, node);
        if (!p->enabled)
            continue;

        if (p->is_user_irq)
            dispatch_user_mode_irq(p);
        else
            p->handler(p->context);
    }
}

static int irq_alloc(u32 major, u32 minor, int is_user_irq, int tid, const char *name,
    void *context, irq_handler_fn handler, irq **out)
{
    if (!out)
        return E_INVAL;

    irq *p = (irq*)kmalloc(sizeof(irq));
    if (!p)
        return E_NOMEM;

    p->name = name;
    p->context = context;
    p->major = major;
    p->minor = minor;
    p->handler = handler;
    p->enabled = 0;
    p->is_user_irq = is_user_irq;
    p->tid = tid;
    p->owner = 0;
    p->sp_lock = spinlock_alloc();
    if (!p->sp_lock) {
        *out = 0;
        kfree(p);
        return E_LIMIT;
    }

    list_init(&p->node);
    list_init(&p->thread_node);

    *out = p;
    return 0;
}

static int irq_free(irq *p)
{
    if (!p)
        return 0;

    spinlock_release(p->sp_lock);
    kfree(p);
    return 0;
}

/*
 * Core implementation of irq_request().
 *
 * is_user_irq tells the interrupt dispatcher how the handler must be
 * delivered:
 *   - 0 : kernel-mode irq   (handler runs with the kernel's own context)
 *   - 1 : user-mode irq     (handler delivered through the user flow)
 *
 * The value is decided by the kernel (from the caller's privilege), never
 * taken blindly from the caller, so a user process cannot spoof a kernel irq.
 */
static int irq_request_internal(irq **out, const char* name, u32 major,
                                u32 minor, irq_handler_fn cb, void* cb_param,
                                int is_user_irq, int tid)
{
    if (!out || major >= IDT_ENTRIES)
        return E_INVAL;

    /* Transition phase: only enforce capability checks on untrusted user
     * processes.
     * - proc == NULL (early boot, scheduler not up)  -> trusted, allow
     * - proc->priv == PROC_PRIV_KERNEL (kernel driver) -> trusted, allow
     * - user process -> must hold CAP_OWN_IRQ for this IRQ line */
    pcb* proc = get_current_process();
    if (proc && proc->priv != PROC_PRIV_KERNEL) {
        if (cap_check(proc, CAP_OWN_IRQ, &major) != 0) {
            LOG("no irq permission for pid %d", proc->pid);
            return E_PERM;
        }
    }

    int ret = 0;
    int minor_existed = 0;

    if (minor == IRQ_ANY_MINOR) {
        if (!irqlines[major]) {
            if (irqline_init(&irqlines[major], major) != 0)
                return E_INTERNAL;
        }
        if (!irqlines[major])
            return E_INTERNAL;

        minor = irqline_find_free_minor(irqlines[major]);
        if (minor == IRQ_ANY_MINOR) {
            LOG("%s: no free minor on major %d", __FUNCTION__, major);
            return E_IRQ_NOTAVAIL;
        }
    }

    ret = irq_alloc(major, minor, is_user_irq, tid, name, cb_param, cb, out);
    if (ret != 0 || *out == 0)
        return ret;


    if (!irqlines[major]) {
        if (irqline_init(&irqlines[major], major) != 0) {
            irq_release(*out);
            *out = 0;
            return E_INTERNAL;
        }
    }
    if (irqlines[major]) {
        list_for_each(each, &irqlines[major]->irqs) {
            irq* p = list_entry(each, irq, node);
            if (p->minor == minor) {
                minor_existed = 1;
                break;
            }
        }

        if (minor_existed) {
            LOG("%s: initialization failed - minor %d already exists", __FUNCTION__, minor);
            irq_release(*out);
            *out = 0;
            return E_IRQ_INUSE;
        }

        /* User IRQ: cache the registering thread's tcb so irqline_handler()
        * (ISR context, interrupts disabled) can deliver the mail without
        * taking schedule_lock via thread_get_by_tid(). */
        if (is_user_irq) {
            tcb* t = thread_get_by_tid(tid);
            if (t) {
                (*out)->owner = (void*)t;
                list_add(&(*out)->thread_node, &t->irqs);
            }
        }

        irqline_add_irq(irqlines[major], *out);
    }

    return 0;
}

int irq_request(irq **out, const char* name, u32 major, u32 minor,
                    irq_handler_fn cb, void* cb_param)
{
    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        irq_syscall_data data = {0};
        data.cmd         = IRQ_SYSCALL_REQUEST;
        data.name        = name;
        data.major       = major;
        data.minor       = minor;
        data.handler     = cb;
        data.param       = cb_param;
        data.is_user_irq = 1;
        data.tid         = thread_get_tid();
        arch_syscall(irq_scall_handle, &data, sizeof(data));
        if (out)
            *out = data.handle;
        return data.ret;
    }

    /* Kernel mode (CPL0): kernel irq, no syscall round-trip. */
    return irq_request_internal(out, name, major, minor, cb, cb_param, 0, 0);
}

void irq_release(irq *p)
{
    if (!p)
        return;

    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        irq_syscall_data data = {0};
        data.cmd    = IRQ_SYSCALL_RELEASE;
        data.handle = p;
        arch_syscall(irq_scall_handle, &data, sizeof(data));
        return;
    }

    if (p->major >= IDT_ENTRIES)
        return;

    if (irqlines[p->major]) {
        irqline_remove_irq(irqlines[p->major], p);
        irqline_mask(irqlines[p->major]);
    }

    if (p->owner) {
        tcb* t = (tcb*)p->owner;
        spinlock_lock(t->sp_lock);
        list_del(&p->thread_node);
        spinlock_unlock(t->sp_lock);
    }

    /* irq_free() releases the spinlock and kfrees the struct */
    irq_free(p);
}

int irq_mask(struct irq* p)
{
    if (!p)
        return E_INVAL;

    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        irq_syscall_data data = {0};
        data.cmd    = IRQ_SYSCALL_MASK;
        data.handle = p;
        arch_syscall(irq_scall_handle, &data, sizeof(data));
        return data.ret;
    }

    if (p->major >= IDT_ENTRIES)
        return E_INVAL;

    spinlock_lock(p->sp_lock);
    p->enabled = 0;
    spinlock_unlock(p->sp_lock);
    if (irqlines[p->major]) {
        irqline_mask(irqlines[p->major]);
    }

    return 0;
}

int irq_unmask(struct irq* p)
{
    if (!p)
        return E_INVAL;

    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        irq_syscall_data data = {0};
        data.cmd    = IRQ_SYSCALL_UNMASK;
        data.handle = p;
        arch_syscall(irq_scall_handle, &data, sizeof(data));
        return data.ret;
    }

    if (p->major >= IDT_ENTRIES)
        return E_INVAL;

    spinlock_lock(p->sp_lock);
    p->enabled = 1;
    spinlock_unlock(p->sp_lock);
    if (irqlines[p->major]) {
        irqline_unmask(irqlines[p->major]);
    }

    return 0;
}

/*
 * ============================================================================
 * IRQ syscall layer (RING3)
 *
 * irq_request / irq_release / irq_mask / irq_unmask are routed through this
 * gate (int $100) whenever the caller runs in user mode (CPL3).  The
 * handler runs in kernel context, so the capability checks inside the
 * kernel implementations still apply to the calling process.
 * ============================================================================
 */
static int irq_syscall_handler(void* context)
{
    irq_syscall_data* data = (irq_syscall_data*)context;
    if (!data)
        return -E_INVAL;

    switch (data->cmd) {
    case IRQ_SYSCALL_REQUEST:
        /* The caller ran in user mode, so this is a user irq.  The flag was
         * set by the public irq_request() entry and carried through the
         * syscall data; forward it into the core implementation. */
        data->ret = irq_request_internal(&data->handle, data->name, data->major,
                                         data->minor, data->handler, data->param,
                                         data->is_user_irq, data->tid);
        break;
    case IRQ_SYSCALL_RELEASE:
        irq_release(data->handle);
        data->ret = 0;
        break;
    case IRQ_SYSCALL_MASK:
        data->ret = irq_mask(data->handle);
        break;
    case IRQ_SYSCALL_UNMASK:
        data->ret = irq_unmask(data->handle);
        break;
    default:
        data->ret = -E_INVAL;
        break;
    }

    return data->ret;
}

void irq_syscall_init(void)
{
    irq_scall_handle = syscall_register(irq_syscall_handler, sizeof(irq_syscall_data));
}

void irq_syscall_exit(void)
{
    syscall_unregister(irq_scall_handle);
}

module_init(irq_syscall_init);
module_exit(irq_syscall_exit);