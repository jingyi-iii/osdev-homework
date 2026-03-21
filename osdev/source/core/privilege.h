#ifndef __PRIVILEGE_H__
#define __PRIVILEGE_H__

#include "compiler.h"

typedef enum privilege_mode {
    KERNEL_MODE,
    USER_MODE,
} priv_mode;

void priv_switch(priv_mode mode);

#endif
