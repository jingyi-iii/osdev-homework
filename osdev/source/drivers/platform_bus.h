#ifndef PLATFORM_BUS_H
#define PLATFORM_BUS_H

#include "bus.h"
#include <stdint.h>

struct platform_bus_ops {
    int (*in_port8)(uint16_t port);
    int (*in_port16)(uint16_t port);
    int (*in_port32)(uint16_t port);
    int (*out_port8)(uint16_t port, uint8_t data);
    int (*out_port16)(uint16_t port, uint16_t data);
    int (*out_port32)(uint16_t port, uint32_t data);
};

int platform_driver_register(struct driver* drv);
int platform_driver_unregister(struct driver* drv);
int platform_device_register(struct device* dev);
int platform_device_unregister(struct device* dev);

#endif
