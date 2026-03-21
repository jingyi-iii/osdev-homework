#ifndef __IRQDEV_H__
#define __IRQDEV_H__

#include <stdint.h>
#include <stddef.h>

typedef struct irqdev {
    const char *name;
    void *context;
    uint32_t irq_no;
    int enabled;
    void (*handler)(void);
    struct irqdev *next;

    int (*mask)(struct irqdev* dev);
    int (*unmask)(struct irqdev* dev);
} irqdev;

typedef void (*irq_handler)(void);

typedef struct irqline {
    irqdev *devs;
    uint32_t dev_cnt;
    int enabled;

    int (*mask)(struct irqline* dev);
    int (*unmask)(struct irqline* dev);
} irqline;

int irq_alloc_dev(uint32_t irq_no, const char *name,
    void *context, irq_handler handler, irqdev **out_dev);
int irq_free_dev(irqdev *dev);

#endif
