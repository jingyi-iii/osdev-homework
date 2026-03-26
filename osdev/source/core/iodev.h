#ifndef __IODEV_H__
#define __IODEV_H__

#include <stdint.h>
#include <stddef.h>
#include "list.h"
#include "spinlock.h"

#ifdef __cplusplus
extern "C" {
#endif

// C structure for iodev
typedef void (*iodev_cb)(struct iodev *dev, void* data, size_t size);
typedef struct iodev {
    const char *name;
    const char *type;
    list_node dev_node;
    spinlock* sp_lock;
    void *context;

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

int io_alloc_dev(const char *name, iodev **out_dev);
int io_free_dev(iodev *dev);

#ifdef __cplusplus
}
#endif

// C++ wrapper macros for iodev
#ifdef __cplusplus
#define IODEV_CPP_BIND_CLASS(clazz)                             \
    static int _c_init(iodev* d)                                \
    {                                                           \
        return static_cast<clazz*>(d->context)->Init();         \
    }                                                           \
    static int _c_read(iodev* d, char* b, size_t s)             \
    {                                                           \
        return static_cast<clazz*>(d->context)->Read(b, s);     \
    }                                                           \
    static int _c_write(iodev* d, const char* b, size_t s)      \
    {                                                           \
        return static_cast<clazz*>(d->context)->Write(b, s);    \
    }                                                           \
    static int _c_ctrl(iodev* d, int c, void* a)                \
    {                                                           \
        return static_cast<clazz*>(d->context)->Ctrl(c, a);     \
    }                                                           \
    static int _c_shutdown(iodev* d)                            \
    {                                                           \
        return static_cast<clazz*>(d->context)->Shutdown();     \
    }                                                           \
    void _bind_c_interface(iodev* dev)                          \
    {                                                           \
        dev->context = this;                                    \
        dev->init = _c_init;                                    \
        dev->read = _c_read;                                    \
        dev->write = _c_write;                                  \
        dev->ctrl = _c_ctrl;                                    \
        dev->shutdown = _c_shutdown;                            \
        mIoDev = dev;                                           \
    }                                                           \
    iodev* mIoDev;
#endif
#endif
