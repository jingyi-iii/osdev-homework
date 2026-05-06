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
    spinlock* splock;

    int (*match)(struct driver* drv, struct device* dev);
};

#endif
