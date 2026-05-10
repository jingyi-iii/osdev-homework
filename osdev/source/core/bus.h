#ifndef BUS_H
#define BUS_H

#include "list.h"
#include "device.h"
#include "driver.h"
#include "spinlock.h"

struct bus {
    const char *name;
    const char *type;
    list_node devices;
    list_node drivers;
    void* bus_ops;
    spinlock* splock;

    int (*match)(struct driver* drv, struct device* dev);
};

int bus_register_driver(struct bus *bus, struct driver *drv);
int bus_unregister_driver(struct bus *bus, struct driver *drv);
int bus_add_device(struct bus *bus, struct device *dev);
int bus_remove_device(struct bus *bus, struct device *dev);

#endif
