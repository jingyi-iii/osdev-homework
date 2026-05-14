#ifndef PLATFORM_DEVICE_H
#define PLATFORM_DEVICE_H

#include <stdint.h>
#include "device.h"
#include "platform_bus.h"

#define MAX_PLATFORM_RES    (128)

enum platform_resource_type {
    PLAT_RES_IO,
    PLAT_RES_IRQ,
    PLAT_RES_MEM,
};

struct platform_resource {
    enum platform_resource_type type;
    union {
        struct {
            uint16_t base;
            uint16_t count;
        } io;

        struct {
            uint32_t nr;
        } irq;

        struct {
            uint32_t addr;
            uint32_t size;
        } mem;
    };
};

struct platform_device {
    struct device dev;
    struct platform_resource resources[MAX_PLATFORM_RES];
    int num_res;
};

struct platform_resource* platform_device_get_resource(
    struct platform_device* dev, enum platform_resource_type type, int index);

struct platform_bus_ops* platform_device_get_ops(struct platform_device* dev);

struct platform_device* to_platform_device(struct device* dev);

#endif
