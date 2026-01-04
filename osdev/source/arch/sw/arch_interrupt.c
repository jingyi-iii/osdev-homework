#include "arch_regs.h"
#include "arch_protect_mode.h"
#include "arch_interrupt.h"

static __attribute__((aligned(sizeof(idesc_t)))) idesc_t idt[IDT_ENTRIES] = { 0 };
void (*user_isrs[IDT_ENTRIES])(void) = { 0 };
static idtmeta_t idtmeta = { 0 };
static void default_handler(void) { for (;;) __asm__ volatile ("hlt"); }

static idesc_t gen_idesc(uint32_t isr, uint16_t sel_code, uint8_t flags)
{
    idesc_t desc = { 0 }; 

    desc.isr_low  = (uint32_t)isr & 0xffff;
    desc.sel_code = sel_code;
    desc.attrs    = flags;
    desc.isr_high = (uint32_t)isr >> 16;
    desc.reserved = 0;

    return desc;
}

void arch_init_8259a(void)
{
    outb(INT_MASTER_CMD,  0x11);
    outb(INT_SLAVE_CMD,   0x11);
    outb(INT_MASTER_DATA, INT_VECTOR_IRQ0);
    outb(INT_SLAVE_DATA,  INT_VECTOR_IRQ8);
    outb(INT_MASTER_DATA, 0x04);
    outb(INT_SLAVE_DATA,  0x02);
    outb(INT_MASTER_DATA, 0x01);
    outb(INT_SLAVE_DATA,  0x01);
    outb(INT_MASTER_DATA, 0xff);
    outb(INT_SLAVE_DATA,  0xff);
}

void arch_enable_8259a_master(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ0 || irq_no >= (INT_VECTOR_IRQ0 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0)));
    port_val = inb(0x21);
    port_val &= mask;
    outb(0x21, port_val);
}

void arch_disable_8259a_master(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ0 || irq_no >= (INT_VECTOR_IRQ0 + 8))
        return;

    mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0));
    port_val = inb(0x21);
    port_val |= mask;
    outb(0x21, port_val);
}

void arch_enable_8259a_slave(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ8 || irq_no >= (INT_VECTOR_IRQ8 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8)));
    port_val = inb(0xa1);
    port_val &= mask;
    outb(0xa1, port_val);
}

void arch_disable_8259a_slave(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ8 || irq_no > (INT_VECTOR_IRQ8 + 8))
        return;

    mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8));
    port_val = inb(0xa1);
    port_val |= mask;
    outb(0xa1, port_val);
}

void arch_set_isr(uint16_t irq_no, void (*handler)())
{
    if (irq_no >= IDT_ENTRIES)
        return; 

    user_isrs[irq_no] = handler ? handler : default_handler;
}

void arch_init_idt(void)
{
    int i = 0;

    arch_cli();
    // for (i = 0; i < IDT_ENTRIES; i++) {
    //     idt[i] = gen_idesc((uint32_t)default_handler,
    //                        arch_get_selector(SYS_CODE),
    //                        IDT_GATE_INT32);
    // }
    // for (i = 0; i < NUM_EXCEPTIONS + NUM_INTERRUPTS; i++) {
    //     idt[i] = gen_idesc((uint32_t)sys_isr_tbl + 64 * i,
    //                        arch_get_selector(SYS_CODE),
    //                        IDT_GATE_INT32);
    // }

    idt[32] = gen_idesc((uint32_t)timer_isr,
                       arch_get_selector(SYS_CODE),
                       IDT_GATE_INT32);
    idt[33] = gen_idesc((uint32_t)keyboard_isr,
                       arch_get_selector(SYS_CODE),
                       IDT_GATE_INT32);

    idtmeta.limit = sizeof(idesc_t) * IDT_ENTRIES - 1;
    idtmeta.base = (uint32_t)idt;
    arch_reload_idt(&idtmeta);
    arch_sti();
}

