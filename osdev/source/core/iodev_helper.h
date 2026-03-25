#ifndef __IODEV_HELPER_H__
#define __IODEV_HELPER_H__

/**
 * C++ Wrapper Macros for iodev
 * 
 * Usage in C++ classes:
 *   1. Add IODEV_CPP_BIND_CLASS() macro inside your class definition
 *   2. Implement static methods: init(), read(), write(), ctrl(), shutdown()
 *   3. Call IODEV_BIND_OPS() in your device initialization function
 * 
 * Example:
 *   class MyDevice {
 *   public:
 *       IODEV_CPP_BIND_CLASS(MyDevice);
 *       static int init(MyDevice* self);
 *       static int read(MyDevice* self, char* buf, size_t size);
 *       // ...
 *   };
 */

#ifdef __cplusplus

#define IODEV_CPP_BIND_CLASS(clazz) \
    static int _c_init(iodev* d) { \
        return static_cast<clazz*>(d->context)->Initialize(); \
    } \
    static int _c_read(iodev* d, char* b, size_t s) { \
        return static_cast<clazz*>(d->context)->Read(b, s); \
    } \
    static int _c_write(iodev* d, const char* b, size_t s) { \
        return static_cast<clazz*>(d->context)->Write(b, s); \
    } \
    static int _c_ctrl(iodev* d, int c, void* a) { \
        return static_cast<clazz*>(d->context)->Ctrl(c, a); \
    } \
    static int _c_shutdown(iodev* d) { \
        return static_cast<clazz*>(d->context)->Shutdown(); \
    } \
    void _bind_c_interface(iodev* dev) { \
        dev->context = this; \
        dev->init = _c_init; \
        dev->read = _c_read; \
        dev->write = _c_write; \
        dev->ctrl = _c_ctrl; \
        dev->shutdown = _c_shutdown; \
    }

#define IODEV_BIND_OPS(clazz, dev_ptr, name, ctx) \
    do { \
        io_alloc_dev((name), (ctx), &(dev_ptr)); \
        if (dev_ptr) { \
            static_cast<clazz*>(ctx)->_bind_c_interface(dev_ptr); \
        } \
    } while (0)

#define IODEV_BIND_OPS_SIMPLE(clazz, dev_ptr, name) \
    IODEV_BIND_OPS(clazz, dev_ptr, name, clazz::GetInstance())

#else

/* C language - no macros needed, just use io_alloc_dev directly */

#endif /* __cplusplus */

#endif /* __IODEV_HELPER_H__ */
