#include "arch_irq.h"
#include "irq.h"
#include "string.h"
#include "iodev_api.h"
#include "heap.h"

static irqline* irqlines[IDT_ENTRIES] = {0};

static int irqline_alloc(uint32_t major, irqline **out)
{
    if (!out)
        return -1;

    irqline *line = (irqline*)kmalloc(sizeof(irqline));
    if (!line)
        return -1;

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
        return -1;

    spinlock_release(line->sp_lock);
    kfree(line);
    return 0;
}

static int irqline_mask(struct irqline* line)
{
    if (!line)
        return -1;

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
        return -1;

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
        return -1;

    spinlock_lock(line->sp_lock);
    list_add(&p->node, &line->irqs);
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_remove_irq(struct irqline* line, struct irq* p)
{
    if (!line || !p)
        return -1;

    spinlock_lock(line->sp_lock);
    list_del(&p->node);
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_remove_all(struct irqline* line)
{
    if (!line)
        return -1;

    spinlock_lock(line->sp_lock);
    list_for_each(node, &line->irqs) {
        list_del(node);
    }
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_init(irqline** out_line, uint32_t major)
{
    if (!out_line || major >= IDT_ENTRIES)
        return -1;

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
        return -1;

    irq *p = (irq*)kmalloc(sizeof(irq));
    if (!p)
        return -1;

    p->name = name;
    p->context = context;
    p->major = major;
    p->minor = minor;
    p->handler = handler;
    p->enabled = 0;
    p->sp_lock = spinlock_alloc();
    if (!p->sp_lock)
        return -1;

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

int irq_request(irq **out, const char* name, uint32_t major, uint32_t minor, irq_handler_fn handler)
{
    if (!out || major >= IDT_ENTRIES)
        return -1;

    int ret = 0;
    int minor_existed = 0;

    if (minor == IRQ_ANY_MINOR) {
        if (!irqlines[major]) {
            if (irqline_init(&irqlines[major], major) != 0)
                return -1;
        }
        if (!irqlines[major])
            return -1;

        minor = irqline_find_free_minor(irqlines[major]);
        if (minor == IRQ_ANY_MINOR) {
            KLOG("%s: no free minor on major %d", __FUNCTION__, major);
            return -1;
        }
    }

    ret = irq_alloc(major, minor, name, 0, handler, out);
    if (ret != 0 || *out == 0)
        return ret;

    if (!irqlines[major]) {
        if (irqline_init(&irqlines[major], major) && irqlines[major])
            return -1;
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
            return -1;
        }

        irqline_add_irq(irqlines[major], *out);
    }

    return 0;
}

void irq_release(irq *p)
{
    if (!p)
        return;

    if (irqlines[p->major]) {
        irqline_remove_irq(irqlines[p->major], p);
    }
    /* irq_free() releases the spinlock and kfrees the struct */
    irq_free(p);
}

int irq_mask(struct irq* p)
{
    if (!p)
        return -1;

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
        return -1;

    spinlock_lock(p->sp_lock);
    p->enabled = 1;
    spinlock_unlock(p->sp_lock);
    if (irqlines[p->major]) {
        irqline_unmask(irqlines[p->major]);
    }

    return 0;
}