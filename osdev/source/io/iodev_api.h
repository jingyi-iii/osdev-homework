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

#ifdef __cplusplus
}
#endif

#endif
