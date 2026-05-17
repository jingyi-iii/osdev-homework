#ifndef __IRQDEV_H__
#define __IRQDEV_H__

#include <stdint.h>
#include <stddef.h>
#include "list.h"
#include "spinlock.h"

#define IRQ_ANY_MINOR  UINT32_MAX

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*irq_handler_fn)(void* context);

typedef struct irq {
    const char *name;
    void *context;
    uint32_t major;
    uint32_t minor;
    int enabled;
    spinlock* sp_lock;
    list_node node;
    irq_handler_fn handler;
} irq;

typedef struct irqline {
    uint32_t major;
    int enabled;
    spinlock* sp_lock;
    list_node irqs;
} irqline;

int irq_request(irq **out, const char* name, uint32_t major, uint32_t minor,
                    irq_handler_fn cb, void* cb_param);
void irq_release(irq *p);
int irq_mask(struct irq* p);
int irq_unmask(struct irq* p);

#ifdef __cplusplus
}
#endif

#endif
