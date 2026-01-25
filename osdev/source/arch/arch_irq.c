#include "arch_regs.h"
#include "arch_om.h"
#include "arch_irq.h"

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
    arch_outb(INT_MASTER_CMD,  0x11);
    arch_outb(INT_SLAVE_CMD,   0x11);
    arch_outb(INT_MASTER_DATA, INT_VECTOR_IRQ0);
    arch_outb(INT_SLAVE_DATA,  INT_VECTOR_IRQ8);
    arch_outb(INT_MASTER_DATA, 0x04);
    arch_outb(INT_SLAVE_DATA,  0x02);
    arch_outb(INT_MASTER_DATA, 0x01);
    arch_outb(INT_SLAVE_DATA,  0x01);
    arch_outb(INT_MASTER_DATA, 0xff);
    arch_outb(INT_SLAVE_DATA,  0xff);
}

void arch_master_unmask_irq(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ0 || irq_no >= (INT_VECTOR_IRQ0 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0)));
    port_val = arch_inb(0x21);
    port_val &= mask;
    arch_outb(0x21, port_val);
}

void arch_master_mask_irq(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ0 || irq_no >= (INT_VECTOR_IRQ0 + 8))
        return;

    mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0));
    port_val = arch_inb(0x21);
    port_val |= mask;
    arch_outb(0x21, port_val);
}

void arch_slave_unmask_irq(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ8 || irq_no >= (INT_VECTOR_IRQ8 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8)));
    port_val = arch_inb(0xa1);
    port_val &= mask;
    arch_outb(0xa1, port_val);
}

void arch_slave_mask_irq(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < INT_VECTOR_IRQ8 || irq_no > (INT_VECTOR_IRQ8 + 8))
        return;

    mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8));
    port_val = arch_inb(0xa1);
    port_val |= mask;
    arch_outb(0xa1, port_val);
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
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = gen_idesc((uint32_t)default_handler,
                           arch_om_get_sel(SYS_CODE),
                           IDT_GATE_INT32);
    }
    for (i = 0; i < NUM_EXCEPTIONS + NUM_INTERRUPTS; i++) {
        idt[i] = gen_idesc((uint32_t)sys_isr_tbl + 256 * i,
                           arch_om_get_sel(SYS_CODE),
                           IDT_GATE_INT32);
    }

    idtmeta.limit = sizeof(idesc_t) * IDT_ENTRIES - 1;
    idtmeta.base = (uint32_t)idt;
    arch_reload_idt(&idtmeta);
    arch_sti();
}

