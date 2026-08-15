#include "drivers/platform_bus.h"
#include "drivers/platform_devices.h"
#include "lib/string.h"
#include "sync/spinlock.h"
#include <stddef.h>

static DECLARE_HEAD_NODE(servers);
static spinlock* servers_lock = 0;

static int in8(u16 port)
{
    u8 data = 0;
    __asm__ volatile("inb %1, %0" : "=a"(data) : "dN"(port));

    return data;
}

static int in16(u16 port)
{
    u16 data = 0;
    __asm__ volatile("inw %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

static int in32(u16 port)
{
    u32 data = 0;
    __asm__ volatile("inl %1, %0" : "=a"(data) : "dN"(port));
    return data;
}

static int out8(u16 port, u8 data)
{
    __asm__ volatile("outb %0, %1" : : "a"(data), "dN"(port));
    return 0;
}

static int out16(u16 port, u16 data)
{
    __asm__ volatile("outw %0, %1" : : "a"(data), "dN"(port));
    return 0;
}

static int out32(u16 port, u32 data)
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
        return E_INVAL;

    if (!drv->type || !dev->type)
        return E_INVAL;

    return strcmp(drv->type, dev->type);
}

static void user_drv_probe(void)
{
    user_driver_param* param = (user_driver_param*)thread_get_param();
    if (!param)
        proc_exit(proc_get_pid());

    struct driver* drv = param->drv;
    struct device* dev = param->dev;

    if (drv)
        drv->start(dev);

    kfree(param);

    for ( ;; )
        thread_yield();
}

static int platform_probe(struct driver *drv, struct device *dev)
{
    if (!drv || !dev)
        return E_DRV_PROBE;

    if (drv->class == DRIVER_CLASS_KERNEL) {
        if (!drv->probe)
            return E_DRV_PROBE;

        if (drv->probe(dev) != 0)
            return E_DRV_PROBE;
    } else if (drv->class == DRIVER_CLASS_USER) {
        user_driver_param* param =
                (user_driver_param*)kmalloc(sizeof(user_driver_param));
        if (!param)
            return E_NOMEM;
        param->drv = drv;
        param->dev = dev;

        int pid = proc_create(PROC_PRIV_USER, user_drv_probe, (void*)param);
        if (pid < 0) {
            kfree(param);
            return E_DRV_PROBE;
        }

        get_platform_device(dev)->server_pid = pid;

        /* Register the server synchronously on the parent side, right after
         * server_pid is stored, so platform_server_lookup() is immediately
         * consistent (no async window where the child thread has not run
         * yet and the pid would be stale/zero).  servers_lock is allocated
         * in platform_bus_init() (CPL0, single-threaded).  Lock order here:
         * bus->splock (held by the caller) -> servers_lock. */
        spinlock_lock(servers_lock);
        list_add(&get_platform_device(dev)->this_node, &servers);
        spinlock_unlock(servers_lock);

        platform_device_grant_capabilities(get_platform_device(dev));
    } else {
        return E_DRV_PROBE;
    }

    return 0;
}

static int platform_remove(struct driver *drv, struct device *dev)
{
    if (!drv || !dev)
        return E_INVAL;

    if (drv->class == DRIVER_CLASS_KERNEL) {
        if (drv->remove)
            drv->remove(dev);
    } else if (drv->class == DRIVER_CLASS_USER) {
        if (drv->stop) {
            drv->stop(dev);
        }

        /* Drop the server entry so a stale pid is never returned by
         * platform_server_lookup().  Safe: remove is only reached for
         * bound devices, and probe always added the entry. */
        if (servers_lock) {
            spinlock_lock(servers_lock);
            list_del(&get_platform_device(dev)->this_node);
            spinlock_unlock(servers_lock);
        }
    }

    return 0;
}

void platform_bus_init(void)
{
    list_init(&platform_bus.devices);
    list_init(&platform_bus.drivers);

    platform_bus.type = "platform";
    platform_bus.match = platform_match;
    platform_bus.probe = platform_probe;
    platform_bus.remove = platform_remove;
    platform_bus.bus_ops = (void*)&ops;
    platform_bus.splock = spinlock_alloc();
    servers_lock = spinlock_alloc();
}

int platform_driver_register(struct driver* drv)
{
    int ret = 0;
    if (!drv)
        return E_INVAL;

    return bus_register_driver(&platform_bus, drv);
}

int platform_device_register(struct device* dev)
{
    int ret = 0;
    if (!dev)
        return E_INVAL;

    return bus_add_device(&platform_bus, dev);
}

int platform_driver_unregister(struct driver* drv)
{
    if (!drv)
        return E_INVAL;
    
    return bus_unregister_driver(&platform_bus, drv);
}

int platform_device_unregister(struct device* dev)
{
    if (!dev)
        return E_INVAL;

    return bus_remove_device(&platform_bus, dev);
}

int platform_server_lookup(const char* name)
{
    if (!name)
        return E_INVAL;
    if (!servers_lock)
        return E_IDLE;

    spinlock_lock(servers_lock);
    list_for_each(node, &servers) {
        platform_device* dev = list_entry(node, platform_device, this_node);
        if (!dev || strcmp(name, dev->dev.name))
            continue;

        spinlock_unlock(servers_lock);
        return dev->server_pid;
    }
    spinlock_unlock(servers_lock);

    return E_NOTFOUND;
}

/* ================================================================
 * Platform Device Helpers (merged from platform_device.c)
 * ================================================================ */

struct platform_resource* platform_device_get_resource(
    struct platform_device* dev, enum platform_resource_type type, int index)
{
    if (!dev)
        return NULL;

    int count = 0;
    for (int i = 0; i < dev->num_res && i < MAX_PLATFORM_RES; i++) {
        if (dev->resources[i].type == type) {
            if (count == index)
                return &dev->resources[i];
            count++;
        }
    }

    return NULL;
}

struct platform_bus_ops* platform_device_get_ops(struct platform_device* dev)
{
    if (!dev || !dev->dev.bus)
        return NULL;

    return (struct platform_bus_ops*)dev->dev.bus->bus_ops;
}
