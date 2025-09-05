#include "gdt.h"

#define GDT_ENTRY_COUNT 5

static uint64_t gdt[GDT_ENTRY_COUNT] = { 0 };
struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdtmeta = { 0 };

static uint64_t gen_gdesc(uint32_t base, uint32_t limit, uint16_t flags)
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

/*
static uint64_t get_gsel(uint64_t* ptr_gdesc, uint8_t rpl)
{
    if (!ptr_gdesc || ptr_gdesc < gdt || rpl > 3)
        return 0;

    return ((ptr_gdesc - gdt) | rpl); 
}
*/

void init_gdt(void)
{
    gdt[0] = gen_gdesc(0,       0,            0);
    gdt[1] = gen_gdesc(0, 0xFFFFF, GDT_CODE_PL0);
    gdt[2] = gen_gdesc(0, 0xFFFFF, GDT_DATA_PL0);
    gdt[3] = gen_gdesc(0, 0xFFFFF, GDT_CODE_PL3);
    gdt[4] = gen_gdesc(0, 0xFFFFF, GDT_DATA_PL3);

    gdtmeta.limit = sizeof(gdt) - 1;
    gdtmeta.base = (uint32_t)gdt;

    __asm__ volatile ("lgdt %0" : : "m" (gdtmeta));
    __asm__ volatile (
        "movl   %0,     %%ds\n"
        "movl   %0,     %%es\n"
        "movl   %0,     %%fs\n"
        "movl   %0,     %%gs\n"
        "movl   %0,     %%ss\n"
        "ljmp   %1,     $1f\n"
        "1:"
        : : "r" (SEL_KERNEL_DS), "i" (SEL_KERNEL_CS)
    );
}
