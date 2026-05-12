#ifndef KB_DRIVER_H
#define KB_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "platform_bus.h"
#include "platform_device.h"

void kb_init(void);
void kb_exit(void);

#ifdef __cplusplus
}
#endif

#endif
