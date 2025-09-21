#ifndef ARCH_PROTECT_MODE
#define ARCH_PROTECT_MODE

enum arch_seltype {
    SYS_CODE  = 1,
    SYS_DATA  = 2,
    USER_CODE = 3,
    USER_DATA = 4,
};

void arch_switch_pm(void);
void arch_switch_rm(void);
uint32_t arch_get_selector(enum arch_seltype type);

#endif