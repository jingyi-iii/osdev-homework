#ifndef __IRQDEV_H__
#define __IRQDEV_H__

#include <stdint.h>
#include <stddef.h>
#include "list.h"
#include "spinlock.h"

typedef struct irqdev {
    const char *name;
    const char *type;
    void *context;
    uint32_t irq_nr;
    int enabled;
    spinlock* sp_lock;
    list_node dev_node;

    void (*handler)(struct irqdev* dev);
    int (*mask)(struct irqdev* dev);
    int (*unmask)(struct irqdev* dev);
} irqdev;

typedef void (*irq_handler)(struct irqdev* dev);

typedef struct irqline {
    uint32_t irq_nr;
    int enabled;
    spinlock* sp_lock;
    list_node dev_list;

    int (*mask)(struct irqline* line);
    int (*unmask)(struct irqline* line);
    int (*add)(struct irqline* line, struct irqdev* dev);
    int (*remove)(struct irqline* line, struct irqdev* dev);
    int (*remove_all)(struct irqline* line);
} irqline;

int irq_alloc_dev(uint32_t irq_nr, const char *name,
    void *context, irq_handler handler, irqdev **out_dev);
int irq_free_dev(irqdev *dev);

int irq_alloc_line(uint32_t irq_nr, irqline **out_line);
int irq_free_line(irqline *line);

#endif
