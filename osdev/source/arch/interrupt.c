#include "interrupt.h"
#include "gdt.h"

static __attribute__((aligned(sizeof(idesc_t)))) idesc_t idt[IDT_ENTRIES] = { 0 };
void (*user_isrs[IDT_ENTRIES])(void) = { 0 };
static idtmeta_t idtmeta = { 0 };

static void cli(void) { __asm__ volatile ("cli"); }
static void sti(void) { __asm__ volatile ("sti"); }
static void default_handler(void) { for (;;) __asm__ volatile ("hlt"); }
static void out_byte(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

idesc_t gen_idesc(uint32_t isr, uint16_t sel_code, uint8_t flags)
{
    idesc_t desc = { 0 }; 

    desc.isr_low  = (uint32_t)isr & 0xffff;
    desc.sel_code = sel_code;
    desc.attrs    = flags;
    desc.isr_high = (uint32_t)isr >> 16;
    desc.reserved = 0;

    return desc;
}

void init_8259a(void)
{
    out_byte(INT_MASTER_CMD,  0x11);
    out_byte(INT_SLAVE_CMD,   0x11);
    out_byte(INT_MASTER_DATA, INT_VECTOR_IRQ0);
    out_byte(INT_SLAVE_DATA,  INT_VECTOR_IRQ8);
    out_byte(INT_MASTER_DATA, 0x04);
    out_byte(INT_SLAVE_DATA,  0x02);
    out_byte(INT_MASTER_DATA, 0x01);
    out_byte(INT_SLAVE_DATA,  0x01);
    out_byte(INT_MASTER_DATA, 0xff);
    out_byte(INT_SLAVE_DATA,  0xff);
}

void enable_8259a_master(uint16_t irq_no)
{
    unsigned char mask = 0;

    if (irq_no < INT_VECTOR_IRQ0 || irq_no >= (INT_VECTOR_IRQ0 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0)));

    __asm__ __volatile__(
            "inb    $0x21,          %%al    \n\t"
            "movb   %0,             %%ah    \n\t"
            "andb   %%ah,           %%al    \n\t"
            "outb   %%al,           $0x21   \n\t"
            :
            :"g"(mask)
            :"ax"
    );
}

void disable_8259a_master(uint16_t irq_no)
{
    unsigned char mask = 0;

    if (irq_no < INT_VECTOR_IRQ0 || irq_no >= (INT_VECTOR_IRQ0 + 8))
        return;

    mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0));
    
    __asm__ __volatile__(
            "inb    $0x21,          %%al    \n\t"
            "movb   %0,             %%ah    \n\t"
            "orb    %%ah,           %%al    \n\t"
            "outb   %%al,           $0x21   \n\t"
            :
            :"g"(mask)
            :"ax"
    );
}

void enable_8259a_slave(uint16_t irq_no)
{
    unsigned char mask = 0;

    if (irq_no < INT_VECTOR_IRQ8 || irq_no >= (INT_VECTOR_IRQ8 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8)));
    
    __asm__ __volatile__(
            "inb    $0xa1,          %%al    \n\t"
            "movb   %0,             %%ah    \n\t"
            "andb   %%ah,           %%al    \n\t"
            "outb   %%al,           $0xa1   \n\t"
            :
            :"g"(mask)
            :"ax"
    );
}

void disable_8259a_slave(uint16_t irq_no)
{
    unsigned char mask = 0;

    if (irq_no < INT_VECTOR_IRQ8 || irq_no > (INT_VECTOR_IRQ8 + 8))
        return;

    mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8));
    
    __asm__ __volatile__(
            "inb    $0xa1,          %%al    \n\t"
            "movb   %0,             %%ah    \n\t"
            "orb    %%ah,           %%al    \n\t"
            "outb   %%al,           $0xa1   \n\t"
            :
            :"g"(mask)
            :"ax"
    );
}

void set_isr(uint16_t irq_no, void (*handler)())
{
    if (irq_no >= IDT_ENTRIES)
        return; 

    user_isrs[irq_no] = handler ? handler : default_handler;
}

void init_idt(void)
{
    int i = 0;

    cli();
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = gen_idesc((uint32_t)default_handler,
                           SEL_KERNEL_CS, IDT_GATE_INT32);
    }
    for (i = 0; i < NUM_EXCEPTIONS + NUM_INTERRUPTS; i++) {
        idt[i] = gen_idesc((uint32_t)sys_isr_tbl + 64 * i,
                           SEL_KERNEL_CS, IDT_GATE_INT32);
    }

    idtmeta.limit = sizeof(idesc_t) * IDT_ENTRIES - 1;
    idtmeta.base = (uint32_t)idt;

    __asm__ volatile("lidt %0" : : "m" (idtmeta));
    sti();
}

