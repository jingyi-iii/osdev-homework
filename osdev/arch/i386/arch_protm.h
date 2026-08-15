#ifndef ARCH_PROTECT_MODE
#define ARCH_PROTECT_MODE

#include "regs.h"
#include "lib/compiler.h"

enum arch_seltype {
    SELTYPE_START = 1,
    SYS_CODE      = 1,
    SYS_DATA      = 2,
    USER_CODE     = 3,
    USER_DATA     = 4,
    TSS           = 5,
    LDT           = 6,
    SELTYPE_END   = 6,
};

void arch_switch_pm(void);
void arch_switch_rm(void);
u16 arch_get_sel(enum arch_seltype type);
u64 arch_get_desc(enum arch_seltype type);
int arch_set_desc(enum arch_seltype type, u64 val);
u64 arch_gen_desc(u32 base, u32 limit, u16 flags);

#endif