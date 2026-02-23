#ifndef __IRQDEV_H__
#define __IRQDEV_H__

#include <stdint.h>
#include <stddef.h>

typedef void (*irq_handler)(struct irqdev *dev);

typedef struct irqdev {
    const char *name;
    void *context;
    uint32_t irq_no;
    int enabled;
    irq_handler handler;
    struct irqdev *next;
} irqdev;

typedef struct irqline {
    irqdev *devs;
    uint32_t dev_count;
    int enabled;
} irqline;

int irq_alloc_dev(uint32_t irq_no, const char *name,
    void *context, irq_handler handler, irqdev **out_dev);
int irq_free_dev(irqdev *dev);
int irq_mask_dev(irqdev *dev);
int irq_unmask_dev(irqdev *dev);
int irq_mask_line(uint32_t irq_no);
int irq_unmask_line(uint32_t irq_no);

#endif
