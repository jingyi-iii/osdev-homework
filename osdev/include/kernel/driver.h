#ifndef DRIVER_H
#define DRIVER_H

#include "lib/list.h"
#include "kernel/errno.h"
#include "kernel/process.h"

typedef enum driver_class {
    DRIVER_CLASS_KERNEL = 0,
    DRIVER_CLASS_USER,
} driver_class;

struct device;
struct driver {
    driver_class class;
    const char *type;
    list_node drv_node;
    void* ops;

    /* kernel driver fields */
    int (*probe)(struct device *dev);
    int (*remove)(struct device *dev);

    /* user driver fields */
    int (*start)(struct device *dev);
    int (*stop)(struct device *dev);
};

#endif
