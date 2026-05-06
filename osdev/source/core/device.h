#ifndef DEVICE_H
#define DEVICE_H

#include "bus.h"
#include "driver.h"

enum dev_state {
    DEV_UNREGISTERED,
    DEV_REGISTERED,
};

struct device {
    const char *name;
    const char *type;
    list_node dev_node;
    void *dev_data;

    struct bus *bus;
    struct driver *driver;
};

#endif
