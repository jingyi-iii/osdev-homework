#ifndef __LOGMGR_H__
#define __LOGMGR_H__

#include "list.h"
#include "module.h"
#include "iodev.h"
#include "arch_regs.h"
#include "spinlock.h"
#include "core_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ulog_msg {
    const char* msg;
    size_t size;
} ulog_msg;

#ifdef __cplusplus
}
#endif

#endif
