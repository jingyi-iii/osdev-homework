#ifndef __CORE_API_H__
#define __CORE_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "irqdev.h"
#include "arch_irq.h"

typedef void (*proc_entry_t)(void);

int irqdev_init(irqdev **out_dev, const char* name,
    uint32_t irq_nr, irq_handler handler);
int32_t create_proc(uint8_t ring, proc_entry_t entry);

#ifdef __cplusplus
}
#endif
#endif
