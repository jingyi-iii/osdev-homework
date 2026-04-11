#include "arch_irq.h"
#include "irqdev.h"
#include "string.h"
#include "iodev_api.h"

static irqline* irqlines[IDT_ENTRIES] = {0};

static int irq_dev_mask(struct irqdev* dev)
{
    if (!dev)
        return -1;

    spinlock_lock(dev->sp_lock);
    dev->enabled = 0;
    spinlock_unlock(dev->sp_lock);
    if (irqlines[dev->irq_nr]) {
        irqlines[dev->irq_nr]->mask(irqlines[dev->irq_nr]);
    }

    return 0;
}

static int irq_dev_unmask(struct irqdev* dev)
{
    if (!dev)
        return -1;

    spinlock_lock(dev->sp_lock);
    dev->enabled = 1;
    spinlock_unlock(dev->sp_lock);
    if (irqlines[dev->irq_nr]) {
        irqlines[dev->irq_nr]->unmask(irqlines[dev->irq_nr]);
    }

    return 0;
}

static int irqline_mask(struct irqline* line)
{
    if (!line)
        return -1;

    int disable = 1;

    list_for_each(node, &line->dev_list) {
        irqdev* dev = list_entry(node, irqdev, dev_node);
        if (dev->enabled) {
            disable = 0;
            break;
        }
    }

    if (disable) {
        spinlock_lock(line->sp_lock);
        arch_mask_irq(line->irq_nr);
        spinlock_unlock(line->sp_lock);
    }

    return 0;
}

static int irqline_unmask(struct irqline* line)
{
    if (!line)
        return -1;

    list_for_each(node, &line->dev_list) {
        irqdev* dev = list_entry(node, irqdev, dev_node);
        if (dev->enabled) {
            spinlock_lock(line->sp_lock);
            arch_unmask_irq(line->irq_nr);
            spinlock_unlock(line->sp_lock);
            break;
        }
    }

    return 0;
}

static int irqline_add(struct irqline* line, struct irqdev* dev)
{
    if (!line || !dev)
        return -1;

    spinlock_lock(line->sp_lock);
    list_add(&dev->dev_node, &line->dev_list);
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_remove(struct irqline* line, struct irqdev* dev)
{
    if (!line || !dev)
        return -1;

    spinlock_lock(line->sp_lock);
    list_del(&dev->dev_node);
    spinlock_unlock(line->sp_lock);

    return 0;
}

static int irqline_remove_all(struct irqline* line)
{
    if (!line)
        return -1;

    spinlock_lock(line->sp_lock);
    list_for_each(node, &line->dev_list) {
        list_del(node);
    }
    spinlock_unlock(line->sp_lock);

    return 0;
}

int irqline_init(irqline** out_line, uint32_t irq_nr)
{
    if (!out_line || irq_nr >= IDT_ENTRIES)
        return -1;

    irqline* line = 0;
    int ret = 0;

    ret = irq_alloc_line(irq_nr, out_line);
    if (ret != 0)
        return ret;
    
    line = *out_line;
    line->mask = irqline_mask;
    line->unmask = irqline_unmask;
    line->add = irqline_add;
    line->remove = irqline_remove;
    line->remove_all = irqline_remove_all;

    return 0;
}

void irqline_release(irqline* line)
{
    if (!line)
        return;

    line->remove_all(line);
    list_del(&line->dev_list);
    spinlock_release(line->sp_lock);
    memset(line, 0, sizeof(irqline));
}

void irqline_handler(uint32_t irq_nr)
{
    if (!irqlines[irq_nr])
        return;

    list_for_each(node, &irqlines[irq_nr]->dev_list) {
        irqdev* dev = list_entry(node, irqdev, dev_node);
        if (dev->enabled) {
            dev->handler(dev);
        }
    }
}

// void syscall_handler(uint32_t minor_nr, void* data)
void syscall_handler(void)
{
    // KLOG("syscall: %s", (const char*)data);
    KLOG("syscall:");
    return;
}

int irqdev_init(irqdev **out_dev, const char* name, uint32_t irq_nr, irq_handler handler)
{
    if (!out_dev || irq_nr >= IDT_ENTRIES)
        return -1;

    irqdev* dev = 0;
    int ret = 0;

    ret = irq_alloc_dev(irq_nr, name, 0, handler, out_dev);
    if (ret != 0)
        return ret;
    
    dev = *out_dev;
    dev->mask = irq_dev_mask;
    dev->unmask = irq_dev_unmask;

    if (!irqlines[irq_nr]) {
        if (!irqline_init(&irqlines[irq_nr], irq_nr) && irqlines[irq_nr])
            irqlines[irq_nr]->add(irqlines[irq_nr], dev);
    }

    return 0;
}

void irqdev_release(irqdev *dev)
{
    if (!dev)
        return;

    if (irqlines[dev->irq_nr]) {
        irqlines[dev->irq_nr]->remove(irqlines[dev->irq_nr], dev);
    }
    spinlock_release(dev->sp_lock);
    irq_free_dev(dev);
}
