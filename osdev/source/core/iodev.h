#ifndef __IODEV_H__
#define __IODEV_H__

#include <stdint.h>
#include <stddef.h>

typedef struct iodev {
    const char *name;
    void *context;
    struct iodev *next;

    int (*init)(struct iodev *dev);
    int (*read)(struct iodev *dev, uint8_t *buf, size_t size);
    int (*write)(struct iodev *dev, const uint8_t *buf, size_t size);
    int (*shutdown)(struct iodev *dev);
} iodev;

int io_alloc_dev(const char *name, void *context, iodev **out_dev);
int io_free_dev(iodev *dev);

#endif
