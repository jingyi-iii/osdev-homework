#ifndef __CORE_API_H__
#define __CORE_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "irqdev.h"
#include "arch_irq.h"
#include "process.h"

int irqdev_init(irqdev **out_dev, const char* name,
    uint32_t irq_nr, irq_handler handler);
void irqdev_release(irqdev *dev);

#ifdef __cplusplus
}
#endif
#endif
