#include "arch_irq.h"
#include "kernel/irq.h"
#include "lib/string.h"
#include "lib/module.h"
#include "drivers/log_driver.h"
#include "mm/heap.h"
#include "kernel/capability.h"
#include "kernel/process.h"

static irqline* irqlines[IDT_ENTRIES] = {0};

static int irqline_alloc(uint32_t major, irqline **out)
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

static int irqline_init(irqline** out_line, uint32_t major)
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
static uint32_t irqline_find_free_minor(struct irqline* line)
{
    for (uint32_t candidate = 0; candidate < UINT32_MAX; candidate++) {
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

void irqline_handler(uint32_t major, uint32_t minor, void* context)
{
    (void)minor;

    /* TEMP DEBUG: count enabled handlers on the keyboard line per IRQ */
    if (major == KEYBOARD_IRQ_NO && irqlines[major]) {
        int n_enabled = 0;
        list_for_each(each, &irqlines[major]->irqs) {
            irq* p = list_entry(each, irq, node);
            if (p->enabled)
                n_enabled++;
        }
        KLOG("irqline_handler: KB major=%x minor=%d enabled=%d",
             major, minor, n_enabled);
    }

    if (!irqlines[major])
        return;

    list_for_each(each, &irqlines[major]->irqs) {
        irq* p = list_entry(each, irq, node);
        if (p->enabled) {
            if (p->major != 100) {
                /* Normal IRQ: pass handler's own context (usually NULL) */
                p->handler(p->context);
            } else {
                /* Syscall (major 100): dispatch by minor, pass real data */
                if (p->minor == minor) {
                    if (minor != 0)
                        KLOG("syscall: minor %d triggled", minor);
                    p->handler(context);
                }
            }
        }
    }
}

static int irq_alloc(uint32_t major, uint32_t minor, const char *name,
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
    p->sp_lock = spinlock_alloc();
    if (!p->sp_lock)
        return E_LIMIT;

    list_init(&p->node);

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

int irq_request(irq **out, const char* name, uint32_t major, uint32_t minor,
                    irq_handler_fn cb, void* cb_param)
{
    /* User mode (CPL3): go through the syscall gate (major 100). */
    if (arch_running_ring3()) {
        irq_syscall_data data = {0};
        data.cmd     = IRQ_SYSCALL_REQUEST;
        data.name    = name;
        data.major   = major;
        data.minor   = minor;
        data.handler = cb;
        data.param   = cb_param;
        arch_syscall(IRQ_SYSCALL_MINOR, &data);
        if (out)
            *out = data.handle;
        return data.ret;
    }

    if (!out || major >= IDT_ENTRIES)
        return E_INVAL;

    /* Transition phase: only enforce capability checks on untrusted user
     * processes.
     * - proc == NULL (early boot, scheduler not up)  -> trusted, allow
     * - proc->priv == PROC_PRIV_KERNEL (kernel driver) -> trusted, allow
     * - user process -> must hold CAP_IRQ_OWN for this IRQ line */
    pcb* proc = get_current_process();
    if (proc && proc->priv != PROC_PRIV_KERNEL) {
        if (cap_check(proc, CAP_IRQ_OWN, &major) != 0) {
            KLOG("no irq permission for pid %d", proc->pid);
            return E_PERM;
        }
    }

    int ret = 0;
    int minor_existed = 0;

    if (minor == IRQ_ANY_MINOR) {
        if (!irqlines[major]) {
            if (irqline_init(&irqlines[major], major) != 0)
                return E_IRQ_NOTAVAIL;
        }
        if (!irqlines[major])
            return E_INTERNAL;

        minor = irqline_find_free_minor(irqlines[major]);
        if (minor == IRQ_ANY_MINOR) {
            KLOG("%s: no free minor on major %d", __FUNCTION__, major);
            return E_IRQ_NOTAVAIL;
        }
    }

    ret = irq_alloc(major, minor, name, cb_param, cb, out);
    if (ret != 0 || *out == 0)
        return ret;

    if (!irqlines[major]) {
        if (irqline_init(&irqlines[major], major) && irqlines[major])
            return E_INTERNAL;
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
            KLOG("%s: initialization failed - minor %d already exists", __FUNCTION__, minor);
            irq_release(*out);
            *out = 0;
            return E_IRQ_INUSE;
        }

        irqline_add_irq(irqlines[major], *out);
    }

    return 0;
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
        arch_syscall(IRQ_SYSCALL_MINOR, &data);
        return;
    }

    if (irqlines[p->major]) {
        irqline_remove_irq(irqlines[p->major], p);
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
        arch_syscall(IRQ_SYSCALL_MINOR, &data);
        return data.ret;
    }

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
        arch_syscall(IRQ_SYSCALL_MINOR, &data);
        return data.ret;
    }

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
 * gate (major 100, minor IRQ_SYSCALL_MINOR) whenever the caller runs in user
 * mode (CPL3).  The handler runs in kernel context, so the capability checks
 * inside the kernel implementations still apply to the calling process.
 * ============================================================================
 */
static irq* irq_scall = 0;

static void irq_syscall_handler(void* context)
{
    irq_syscall_data* data = (irq_syscall_data*)context;
    if (!data)
        return;

    switch (data->cmd) {
    case IRQ_SYSCALL_REQUEST:
        data->ret = irq_request(&data->handle, data->name, data->major,
                                data->minor, data->handler, data->param);
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
}

void irq_syscall_init(void)
{
    int ret = irq_request(&irq_scall, "irq_syscall", 100,
                          IRQ_SYSCALL_MINOR, irq_syscall_handler, NULL);
    if (ret == 0 && irq_scall)
        irq_unmask(irq_scall);
}

void irq_syscall_exit(void)
{
    if (irq_scall) {
        irq_mask(irq_scall);
        irq_release(irq_scall);
        irq_scall = 0;
    }
}

module_init(irq_syscall_init);
module_exit(irq_syscall_exit);