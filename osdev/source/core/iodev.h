#ifndef __IODEV_H__
#define __IODEV_H__

#include <stdint.h>
#include <stddef.h>

typedef struct iodev {
    const char *name;
    void *context;
    struct iodev *next;

    // Function pointers for device operations.
    // The specific io device can implement these as needed.
    int (*init)(struct iodev *dev);
    int (*read)(struct iodev *dev, uint8_t *buf, size_t size);
    int (*write)(struct iodev *dev, const uint8_t *buf, size_t size);
    int (*ctrl)(struct iodev *dev, int cmd, void *arg);
    int (*shutdown)(struct iodev *dev);

    // Callback for data reception (if applicable by user)
    int (*data_cb)(struct iodev *dev, void* data);
} iodev;

int io_alloc_dev(const char *name, void *context, iodev **out_dev);
int io_free_dev(iodev *dev);

#endif
