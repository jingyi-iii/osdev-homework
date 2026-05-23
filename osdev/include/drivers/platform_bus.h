#ifndef PLATFORM_BUS_H
#define PLATFORM_BUS_H

#include <stdint.h>
#include "kernel/bus.h"
#include "kernel/device.h"

#define MAX_PLATFORM_RES    (128)

#define container_of(ptr, type, member) ({                \
    const typeof(((type *)0)->member) *__mptr = (ptr);    \
    (type *)((char *)__mptr - offsetof(type, member));    \
})

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
            uint16_t size;
        } io;

        struct {
            uint32_t major;
            uint32_t minor;
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

struct platform_bus_ops {
    int (*in_port8)(uint16_t port);
    int (*in_port16)(uint16_t port);
    int (*in_port32)(uint16_t port);
    int (*out_port8)(uint16_t port, uint8_t data);
    int (*out_port16)(uint16_t port, uint16_t data);
    int (*out_port32)(uint16_t port, uint32_t data);
};

/* --- platform device helpers --- */
struct platform_resource* platform_device_get_resource(
    struct platform_device* dev, enum platform_resource_type type, int index);

struct platform_bus_ops* platform_device_get_ops(struct platform_device* dev);

struct platform_device* to_platform_device(struct device* dev);

/* --- platform bus registration --- */
int platform_driver_register(struct driver* drv);
int platform_driver_unregister(struct driver* drv);
int platform_device_register(struct device* dev);
int platform_device_unregister(struct device* dev);

void platform_bus_init(void);

#endif
