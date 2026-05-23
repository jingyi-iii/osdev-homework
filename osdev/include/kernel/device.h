#ifndef DEVICE_H
#define DEVICE_H

#include "kernel/driver.h"

enum dev_state {
    DEV_UNREGISTERED,
    DEV_REGISTERED,
};

struct bus;
struct device {
    const char *name;
    const char *type;
    list_node dev_node;
    enum dev_state state;
    void *dev_data;

    struct bus *bus;
    struct driver *driver;
};

#endif
