#ifndef ARCH_OPERATING_MODE
#define ARCH_OPERATING_MODE

#include "arch_regs.h"

enum arch_seltype {
    SYS_CODE  = 1,
    SYS_DATA  = 2,
    USER_CODE = 3,
    USER_DATA = 4,
    TSS       = 5,
    LDT       = 6,
};

void arch_om_switch_pm(void);
void arch_om_switch_rm(void);
uint16_t arch_om_get_sel(enum arch_seltype type);
uint64_t* arch_om_get_desc(enum arch_seltype type);
uint64_t arch_om_gen_desc(uint32_t base, uint32_t limit, uint16_t flags);

#endif