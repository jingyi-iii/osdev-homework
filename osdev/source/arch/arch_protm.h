#ifndef ARCH_PROTECT_MODE
#define ARCH_PROTECT_MODE

#include "arch_regs.h"

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

enum error_type {
    EINVALID = 1,
};

void arch_switch_pm(void);
void arch_switch_rm(void);
uint16_t arch_get_sel(enum arch_seltype type);
uint64_t arch_get_desc(enum arch_seltype type);
int arch_set_desc(enum arch_seltype type, uint64_t val);
uint64_t arch_gen_desc(uint32_t base, uint32_t limit, uint16_t flags);

#endif