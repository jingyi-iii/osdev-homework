#ifndef __IODEV_H__
#define __IODEV_H__

#include <stdint.h>
#include <stddef.h>
#include "list.h"
#include "spinlock.h"

typedef void (*iodev_cb)(struct iodev *dev, void* data, size_t size);

typedef struct iodev {
    const char *name;
    const char *type;
    void *context;
    list_node dev_node;
    spinlock* sp_lock;

    // Function pointers for device operations.
    // The specific io device can implement these as needed.
    int (*init)(struct iodev *dev);
    int (*read)(struct iodev *dev, char *buf, size_t size);
    int (*write)(struct iodev *dev, const char *buf, size_t size);
    int (*ctrl)(struct iodev *dev, int cmd, void *arg);
    int (*shutdown)(struct iodev *dev);

    // Callback for data reception (if implemeted by user)
    iodev_cb data_cb;
} iodev;

int io_alloc_dev(const char *name, void *context, iodev **out_dev);
int io_free_dev(iodev *dev);

#endif
