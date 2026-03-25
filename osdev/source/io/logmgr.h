#ifndef __LOGMGR_H__
#define __LOGMGR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "iodev.h"
#include "arch_regs.h"
#include "spinlock.h"
#include "string.h"

extern iodev* glogdev;
#define LOG_DBG(dev, fmt, ...)                                                                      \
    do {                                                                                            \
        if (!glogdev)                                                                               \
            break;                                                                                  \
        if (!dev || !dev->type || !dev->name)                                                       \
            break;                                                                                  \
        char log_buf[256];                                                                          \
        snprintf(log_buf, sizeof(log_buf), "[%s][%s] " fmt, dev->type, dev->name, ##__VA_ARGS__);   \
        glogdev->write(glogdev, log_buf, strlen(log_buf));                                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
