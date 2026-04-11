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
    if (irqlines[dev->major]) {
        irqlines[dev->major]->mask(irqlines[dev->major]);
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
    if (irqlines[dev->major]) {
        irqlines[dev->major]->unmask(irqlines[dev->major]);
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
        arch_mask_irq(line->major);
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
            arch_unmask_irq(line->major);
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

int irqline_init(irqline** out_line, uint32_t major)
{
    if (!out_line || major >= IDT_ENTRIES)
        return -1;

    irqline* line = 0;
    int ret = 0;

    ret = irq_alloc_line(major, out_line);
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

void irqline_handler(uint32_t major, uint32_t minor, void* data)
{
    (void)minor;
    (void)data;

    if (!irqlines[major])
        return;

    list_for_each(node, &irqlines[major]->dev_list) {
        irqdev* dev = list_entry(node, irqdev, dev_node);
        if (dev->enabled) {
            if (dev->major != 100) {
                dev->handler((void*)dev);
            } else {
                if (dev->minor == minor) {
                    dev->handler((void*)data);
                    KLOG("syscall: minor %d triggled", minor);
                }
            }
        }
    }
}

int irqdev_init(irqdev **out_dev, const char* name, uint32_t major, uint32_t minor, irq_handler handler)
{
    if (!out_dev || major >= IDT_ENTRIES)
        return -1;

    irqdev* dev = 0;
    int ret = 0;

    ret = irq_alloc_dev(major, minor, name, 0, handler, out_dev);
    if (ret != 0)
        return ret;
    
    dev = *out_dev;
    dev->mask = irq_dev_mask;
    dev->unmask = irq_dev_unmask;

    if (!irqlines[major]) {
        if (!irqline_init(&irqlines[major], major) && irqlines[major])
            irqlines[major]->add(irqlines[major], dev);
    }

    return 0;
}

void irqdev_release(irqdev *dev)
{
    if (!dev)
        return;

    if (irqlines[dev->major]) {
        irqlines[dev->major]->remove(irqlines[dev->major], dev);
    }
    spinlock_release(dev->sp_lock);
    irq_free_dev(dev);
}
