#include "bus.h"
#include "string.h"

/* Returns non-zero if drv matches dev for binding attempt. */
static int driver_matches(struct bus *bus, struct driver *drv, struct device *dev)
{
    if (!drv || !dev)
        return 0;

    if (bus && bus->match)
        return bus->match(drv, dev) == 0;

    if (!drv->type || !dev->type)
        return 0;

    return strcmp(drv->type, dev->type) == 0;
}

static int try_bind_and_probe(struct bus *bus, struct driver *drv, struct device *dev)
{
    if (!drv || !dev)
        return -1;

    if (dev->driver != NULL)
        return -1;

    if (!driver_matches(bus, drv, dev))
        return -1;

    if (!drv->probe)
        return -1;

    dev->driver = drv;
    int ret = drv->probe(dev);
    if (ret != 0) {
        dev->driver = NULL;
        return -1;
    }

    return 0;
}

static int unbind_driver_from_device(struct driver *drv, struct device *dev)
{
    if (!drv || !dev || dev->driver != drv)
        return -1;

    if (drv->remove)
        drv->remove(dev);

    dev->driver = NULL;
    return 0;
}

int bus_register_driver(struct bus *bus, struct driver *drv)
{
    if (!bus || !drv)
        return -1;

    spinlock_lock(bus->splock);
    list_add(&drv->drv_node, &bus->drivers);
    drv->ops = bus->bus_ops;
    list_for_each(node, &bus->devices) {
        struct device *dev = list_entry(node, struct device, dev_node);
        if (try_bind_and_probe(bus, drv, dev) == 0) {
            /* bound */
        }
    }
    spinlock_unlock(bus->splock);

    return 0;
}

int bus_unregister_driver(struct bus *bus, struct driver *drv)
{
    if (!bus || !drv)
        return -1;

    spinlock_lock(bus->splock);
    list_for_each(node, &bus->devices) {
        struct device *dev = list_entry(node, struct device, dev_node);
        if (dev->driver == drv)
            unbind_driver_from_device(drv, dev);
    }
    list_del(&drv->drv_node);
    spinlock_unlock(bus->splock);

    return 0;
}

int bus_add_device(struct bus *bus, struct device *dev)
{
    if (!bus || !dev)
        return -1;

    spinlock_lock(bus->splock);
    dev->state = DEV_REGISTERED;
    dev->bus = bus;
    list_add(&dev->dev_node, &bus->devices);
    list_for_each(node, &bus->drivers) {
        struct driver *drv = list_entry(node, struct driver, drv_node);
        if (try_bind_and_probe(bus, drv, dev) == 0)
            break;
    }
    spinlock_unlock(bus->splock);

    return 0;
}

int bus_remove_device(struct bus *bus, struct device *dev)
{
    if (!bus || !dev || dev->bus != bus)
        return -1;

    spinlock_lock(bus->splock);
    if (dev->driver)
        unbind_driver_from_device(dev->driver, dev);

    dev->state = DEV_UNREGISTERED;
    dev->bus = NULL;
    list_del(&dev->dev_node);
    spinlock_unlock(bus->splock);

    return 0;
}
