#ifndef __LOGMGR_H__
#define __LOGMGR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "iodev.h"
#include "arch_regs.h"
#include "spinlock.h"

int logdev_init(iodev **out_dev);

#ifdef __cplusplus
}
#endif

#endif
