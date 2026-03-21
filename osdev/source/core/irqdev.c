#include "irqdev.h"
#include "heap.h"

int irq_alloc_dev(uint32_t irq_nr, const char *name,
    void *context, irq_handler handler, irqdev **out_dev)
{
    if (!out_dev)
        return -1;

    irqdev *dev = (irqdev*)kmalloc(sizeof(irqdev));
    if (!dev)
        return -1;

    dev->name = name;
    dev->context = context;
    dev->irq_nr = irq_nr;
    dev->handler = handler;
    dev->enabled = 0;
    dev->next = NULL;

    *out_dev = dev;
    return 0;
}

int irq_free_dev(irqdev *dev)
{
    if (!dev)
        return -1;

    kfree(dev);
    return 0;
}

int irq_alloc_line(uint32_t irq_nr, irqline **out_line)
{
    if (!out_line)
        return -1;

    irqline *line = (irqline*)kmalloc(sizeof(irqline));
    if (!line)
        return -1;

    line->devs = 0;
    line->irq_nr = irq_nr;
    line->dev_cnt = 0;
    line->enabled = 0;

    *out_line = line;

    return 0;
}

int irq_free_line(irqline *line)
{
    if (!line)
        return -1;

    kfree(line);
    return 0;
}
