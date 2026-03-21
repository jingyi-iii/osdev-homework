#include "irqdev.h"
#include "heap.h"

int irq_alloc_dev(uint32_t irq_no, const char *name,
    void *context, irq_handler handler, irqdev **out_dev)
{
    if (!out_dev)
        return -1;

    irqdev *dev = (irqdev*)kmalloc(sizeof(irqdev));
    if (!dev)
        return -1;

    dev->name = name;
    dev->context = context;
    dev->irq_no = irq_no;
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
