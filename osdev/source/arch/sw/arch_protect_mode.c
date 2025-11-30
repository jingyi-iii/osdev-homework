#include "arch_protect_mode.h"

#define GDT_MAX_COUNT   (255)
static uint64_t gdt[GDT_MAX_COUNT] = { 0 };
static uint16_t segsels[GDT_MAX_COUNT] = { 0 };
static struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdtmeta = { 0 };


#define GENMASK32(l, h) (((uint32_t)~0U >> (31 - (h))) & ((uint32_t)~0U << (l)))
#define GENMASK64(l, h) (((uint64_t)~0ULL >> (63 - (h))) & ((uint64_t)~0ULL << (l)))
uint64_t arch_gen_desc(uint32_t base, uint32_t limit, uint16_t flags)
{
    uint64_t desc = 0;

    desc  = limit        & 0x000f0000;
    desc |= (flags << 8) & 0x00f0ff00;
    desc |= (base >> 16) & 0x000000ff;
    desc |= base         & 0xff000000;

    desc <<= 32;
    desc |= base << 16;
    desc |= limit & 0x0000ffff;

    return desc;
}

static uint16_t arch_add_segment(uint32_t base, uint32_t limit, uint16_t flags)
{
    int i = 0;

    for (i = 1; i < GDT_MAX_COUNT; i++) {
        if (gdt[i] == 0) {
            gdt[i] = arch_gen_desc(base, limit, flags);
            return (i * sizeof(uint64_t));
        }
    }

    return 0;
}

static void arch_prepare_pm(void)
{
    segsels[0]         = arch_add_segment(0,       0,            0);
    segsels[SYS_CODE]  = arch_add_segment(0, 0xFFFFF, GDT_CODE_PL0);
    segsels[SYS_DATA]  = arch_add_segment(0, 0xFFFFF, GDT_DATA_PL0);
    segsels[USER_CODE] = arch_add_segment(0, 0xFFFFF, GDT_CODE_PL3) | 0x3;
    segsels[USER_DATA] = arch_add_segment(0, 0xFFFFF, GDT_DATA_PL3) | 0x3;
    segsels[TSS]       = arch_add_segment(0, 0, 0);
    segsels[LDT]       = arch_add_segment(0, 0, 0);

    gdtmeta.limit = sizeof(gdt) - 1;
    gdtmeta.base = (uint32_t)gdt;
    arch_reload_gdt(&gdtmeta);
}

void arch_switch_pm(void)
{
    arch_prepare_pm();
    arch_set_cr0(0);
}

void arch_switch_rm(void)
{
    arch_clr_cr0(0);
}

uint16_t arch_get_selector(enum arch_seltype type)
{
    return ((uint32_t)&gdt[type] - (uint32_t)&gdt[0]);
}

uint64_t* arch_get_desc(enum arch_seltype type)
{
    return ((uint64_t*)&gdt[type]);
}