#ifndef __DRAWDEV_H__
#define __DRAWDEV_H__

#include "iodev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum draw_cmd {
    GRAPHIC_MODE,
    CHAR_MODE,
} draw_cmd;

int drawdev_init(iodev **out_dev);

#ifdef __cplusplus
}
#endif

#endif
