#ifndef DRIVER_H
#define DRIVER_H

#include "lib/list.h"

struct device;
struct driver {
    const char *type;
    list_node drv_node;
    void* ops;

    int (*probe)(struct device *dev);
    int (*remove)(struct device *dev);
};

#endif
