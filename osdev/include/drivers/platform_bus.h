#ifndef PLATFORM_BUS_H
#define PLATFORM_BUS_H

#include "lib/types.h"
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
            u16 base;
            u16 size;
        } io;

        struct {
            u32 major;
            u32 minor;
        } irq;

        struct {
            u32 addr;
            u32 size;
        } mem;
    };
};

typedef struct platform_device {
    struct device dev;
    struct platform_resource resources[MAX_PLATFORM_RES];
    int num_res;
    list_node this_node;
    int server_pid;
} platform_device;

struct platform_bus_ops {
    int (*in_port8)(u16 port);
    int (*in_port16)(u16 port);
    int (*in_port32)(u16 port);
    int (*out_port8)(u16 port, u8 data);
    int (*out_port16)(u16 port, u16 data);
    int (*out_port32)(u16 port, u32 data);
};

typedef struct user_driver_param {
    struct driver* drv;
    struct device* dev;
} user_driver_param;

/* --- platform device helpers --- */
struct platform_resource* platform_device_get_resource(
    struct platform_device* dev, enum platform_resource_type type, int index);

struct platform_bus_ops* platform_device_get_ops(struct platform_device* dev);

/* --- platform server registry --- */
int platform_server_lookup(const char* name);

/* --- platform bus registration --- */
int platform_driver_register(struct driver* drv);
int platform_driver_unregister(struct driver* drv);
int platform_device_register(struct device* dev);
int platform_device_unregister(struct device* dev);

void platform_bus_init(void);

#endif
