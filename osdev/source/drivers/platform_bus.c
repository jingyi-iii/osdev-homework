#include "platform_bus.h"
#include "module.h"
#include "string.h"
#include "spinlock.h"

static int in8(uint16_t port)
{
    uint8_t data;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

static int in16(uint16_t port)
{
    uint16_t data;
    __asm__ volatile("inw %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

static int in32(uint16_t port)
{
    uint32_t data;
    __asm__ volatile("inl %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

static int out8(uint16_t port, uint8_t data)
{
    __asm__ volatile("outb %0, %1" : : "a"(data), "dN"(port));
    return 0;
}

static int out16(uint16_t port, uint16_t data)
{
    __asm__ volatile("outw %0, %1" : : "a"(data), "dN"(port));
    return 0;
}

static int out32(uint16_t port, uint32_t data)
{
    __asm__ volatile("outl %0, %1" : : "a"(data), "dN"(port));
    return 0;
}

static struct platform_bus_ops ops = {
    .in_port8 = in8,
    .in_port16 = in16,
    .in_port32 = in32,
    .out_port8 = out8,
    .out_port16 = out16,
    .out_port32 = out32,
};
static struct bus platform_bus = {0};

static int platform_match(struct driver *drv, struct device *dev)
{
    if (!drv || !dev)
        return -1;

    if (!drv->type || !dev->type)
        return -1;

    return strcmp(drv->type, dev->type);
}

static void platform_bus_init(void)
{
    list_init(&platform_bus.devices);
    list_init(&platform_bus.drivers);

    platform_bus.name = "platform_bus";
    platform_bus.type = "platform";
    platform_bus.match = platform_match;
    platform_bus.bus_ops = (void*)&ops;
    platform_bus.splock = spinlock_alloc();
}

int platform_driver_register(struct driver* drv)
{
    if (!drv)
        return -1;

    return bus_register_driver(&platform_bus, drv);
}

int platform_device_register(struct device* dev)
{
    if (!dev)
        return -1;

    return bus_add_device(&platform_bus, dev);
}

int platform_driver_unregister(struct driver* drv)
{
    if (!drv)
        return -1;
    
    return bus_unregister_driver(&platform_bus, drv);
}

int platform_device_unregister(struct device* dev)
{
    if (!dev)
        return -1;

    return bus_remove_device(&platform_bus, dev);
}

module_init(platform_bus_init);

