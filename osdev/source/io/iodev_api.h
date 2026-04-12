#ifndef __IODEV_API_H__
#define __IODEV_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "iodev.h"
#include "string.h"
#include "arch_irq.h"
#include "logmgr.h"

int kbdev_init(iodev **out_dev, const char* dev_name, iodev_cb cb);
void kbdev_release(iodev **dev);

int tmrdev_init(iodev **out_dev);
void tmrdev_release(iodev **dev);

int tmpdev_init(iodev **out_dev);

extern iodev* glogdev;
extern iodev* gtmrdev;
#define LOG_DBG(dev, fmt, ...)                                                                                              \
    do {                                                                                                                    \
        if (!glogdev)                                                                                                       \
            break;                                                                                                          \
        if (!dev || !dev->type || !dev->name)                                                                               \
            break;                                                                                                          \
        char log_buf[256] = {0};                                                                                            \
        if (gtmrdev) {                                                                                                      \
            char tmr_buf[32] = {0};                                                                                         \
            gtmrdev->read(gtmrdev, tmr_buf, 32);                                                                            \
            snprintf(log_buf, sizeof(log_buf), "%s [%4s] [%8s] " fmt "\n", tmr_buf, dev->type, dev->name, ##__VA_ARGS__);   \
        } else {                                                                                                            \
            snprintf(log_buf, sizeof(log_buf), "[%4s] [%8s] " fmt "\n", dev->type, dev->name, ##__VA_ARGS__);               \
        }                                                                                                                   \
        glogdev->write(glogdev, log_buf, strlen(log_buf));                                                                  \
    } while (0)

#define KLOG(fmt, ...)                                                                                                      \
    do {                                                                                                                    \
        if (!glogdev)                                                                                                       \
            break;                                                                                                          \
        char log_buf[256] = {0};                                                                                            \
        if (gtmrdev) {                                                                                                      \
            char tmr_buf[32] = {0};                                                                                         \
            gtmrdev->read(gtmrdev, tmr_buf, 32);                                                                            \
            snprintf(log_buf, sizeof(log_buf), "%s KLOG: " fmt "\n", tmr_buf, ##__VA_ARGS__);                               \
        } else {                                                                                                            \
            snprintf(log_buf, sizeof(log_buf), "KLOG: " fmt "\n", ##__VA_ARGS__);                                           \
        }                                                                                                                   \
        glogdev->write(glogdev, log_buf, strlen(log_buf));                                                                  \
    } while (0)

#define ULOG(fmt, ...)                                                                                                      \
    do {                                                                                                                    \
        char log_buf[256] = {0};                                                                                            \
        ulog_msg msg = {0};                                                                                                 \
        snprintf(log_buf, sizeof(log_buf), "ULOG: " fmt "\n", ##__VA_ARGS__);                                               \
        msg.msg = (const char*)log_buf;                                                                                     \
        msg.size = strlen(log_buf);                                                                                         \
        arch_syscall(0, &msg);                                                                                              \
    } while (0)


#ifdef __cplusplus
}
#endif

#endif
