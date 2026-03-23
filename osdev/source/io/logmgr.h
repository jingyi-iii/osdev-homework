#ifndef __LOGMGR_H__
#define __LOGMGR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "iodev.h"
#include "arch_regs.h"
#include "spinlock.h"
#include "string.h"

#define LOG_DBG(dev, fmt, ...)                                                      \
    do {                                                                            \
        if (!dev)                                                                   \
            break;                                                                  \
        char log_buf[256];                                                          \
        snprintf(log_buf, sizeof(log_buf), "[%s] " fmt, dev->name, ##__VA_ARGS__);  \
        dev->write(dev, log_buf, strlen(log_buf));                                  \
    } while (0)

int logdev_init(iodev **out_dev, const char* name);

#ifdef __cplusplus
}
#endif

#endif
