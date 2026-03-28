#ifndef __IODEV_API_H__
#define __IODEV_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "iodev.h"
#include "string.h"

int kbdev_init(iodev **out_dev, const char* dev_name, iodev_cb cb);
int tmrdev_init(iodev **out_dev);
int tmpdev_init(iodev **out_dev);

extern iodev* glogdev;
extern iodev* gtmrdev;
#define LOG_DBG(dev, fmt, ...)                                                                                      \
    do {                                                                                                            \
        if (!glogdev)                                                                                               \
            break;                                                                                                  \
        if (!dev || !dev->type || !dev->name)                                                                       \
            break;                                                                                                  \
        char log_buf[256] = {0};                                                                                    \
        char tmr_buf[32] = {0};                                                                                     \
        gtmrdev->read(gtmrdev, tmr_buf, 32);                                                                        \
        snprintf(log_buf, sizeof(log_buf), "%s [%4s] [%8s] " fmt "\n", tmr_buf, dev->type, dev->name, ##__VA_ARGS__); \
        glogdev->write(glogdev, log_buf, strlen(log_buf));                                                          \
    } while (0)


#ifdef __cplusplus
}
#endif

#endif
