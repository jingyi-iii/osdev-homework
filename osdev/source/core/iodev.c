#include "iodev.h"
#include "heap.h"

int io_alloc_dev(const char *name, void *context, iodev **out_dev)
{
    if (!out_dev)
        return -1;

    iodev *dev = (iodev*)kmalloc(sizeof(iodev));
    if (!dev)
        return -1;

    dev->name = name;
    dev->context = context;
    dev->next = NULL;

    *out_dev = dev;
    return 0;
}

int io_free_dev(iodev *dev)
{
    if (!dev)
        return -1;

    kfree(dev);
    return 0;
}
