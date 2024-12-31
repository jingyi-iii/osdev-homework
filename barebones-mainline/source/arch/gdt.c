#include "gdt.h"

static uint64_t gdt[128] = { 0 };
struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdtmeta = { 0 };

uint64_t gen_gdesc(uint32_t base, uint32_t limit, uint16_t flags)
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

uint64_t get_gsel(uint64_t* ptr_gdesc, uint8_t rpl)
{
    if (!ptr_gdesc || ptr_gdesc < gdt || rpl > 3)
        return 0;

    return ((ptr_gdesc - gdt) | rpl); 
}

void init_gdt(void)
{
    gdt[0] = gen_gdesc(0,       0,            0);
    gdt[1] = gen_gdesc(0, 0xFFFFF, GDT_CODE_PL0);
    gdt[2] = gen_gdesc(0, 0xFFFFF, GDT_DATA_PL0);

    gdtmeta.limit = 128 * sizeof(uint64_t) - 1;
    gdtmeta.base = (uint32_t)gdt;

    __asm__("lgdt gdtmeta":::);
}
