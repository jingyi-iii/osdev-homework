#include "iodev.h"
#include "heap.h"

int io_alloc_dev(const char *name, void *context, iodev **out_dev)
{
    if (!out_dev)
        return -1;

    iodev *dev = (iodev*)kmalloc(sizeof(iodev));
    if (!dev)
        return -1;

    dev->type = "io";
    dev->name = name;
    dev->context = context;
    dev->sp_lock = spinlock_alloc();
    list_init(&dev->dev_node);

    *out_dev = dev;
    return 0;
}

int io_free_dev(iodev *dev)
{
    if (!dev)
        return -1;

    spinlock_free(dev->sp_lock);
    kfree(dev);
    return 0;
}
