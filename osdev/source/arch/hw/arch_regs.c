#include "arch_regs.h"

void arch_reload_gdt(void* pgdtmeta)
{
    if (!pgdtmeta)
        return;

    __asm__ __volatile__ (
        "lgdt %0                        \n\t"
        :
        : "m" (*(uint32_t*)pgdtmeta)
        :
    );
}

void arch_reload_idt(void* pidtmeta)
{
    if (!pidtmeta)
        return;

    __asm__ __volatile__ (
        "lidt %0                        \n\t"
        :
        : "m" (*(uint32_t*)pidtmeta)
        :
    );
}

void arch_set_cr0(uint8_t pos)
{
    if (pos > 31)
        return;

    __asm__ __volatile__ (
        "movl   %%cr0,          %%eax   \n\t"
        "orl    %0,             %%eax   \n\t"
        "movl   %%eax,          %%cr0   \n\t"
        :
        : "r"(1 << pos)
        : "%eax", "memory"
    );
}

void arch_clr_cr0(uint8_t pos)
{
    if (pos > 31)
        return;

    __asm__ __volatile__ (
        "movl   %%cr0,          %%eax   \n\t"
        "movl   %0,             %%ebx   \n\t"
        "notl   %%ebx                   \n\t"
        "andl   %%ebx,          %%eax   \n\t"
        "movl   %%eax,          %%cr0   \n\t"
        :
        : "r"((uint32_t)1 << pos)
        : "%eax", "memory"
    );
}

void arch_cli(void) { __asm__ volatile ("cli"); }
void arch_sti(void) { __asm__ volatile ("sti"); }

void outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

uint8_t inb(uint16_t port)
{
    unsigned char value = 0;
    __asm__ __volatile__("inb %1, %0" : "=a" (value) : "Nd" (port));
    return value;
}