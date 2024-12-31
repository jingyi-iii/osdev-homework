#include "interrupt.h"

// from asm
void isr_table(void);

static __attribute__((aligned(sizeof(idesc_t)))) idesc_t idt[256] = { 0 };
void (*isr_handlers[256])(void);
idtmeta_t idtmeta = { 0 };

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

static inline void out_byte(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
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


void enable_8259a_master(uint16_t irq)
{
    unsigned char mask = 0;

    if (irq < INT_VECTOR_IRQ0 || irq > (INT_VECTOR_IRQ0 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq - INT_VECTOR_IRQ0)));

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

void disable_8259a_master(uint16_t irq)
{
    unsigned char mask = 0;

    if (irq < INT_VECTOR_IRQ0 || irq > (INT_VECTOR_IRQ0 + 8))
        return;

    mask = (unsigned char)(1 << (irq - INT_VECTOR_IRQ0));
    
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

void enable_8259a_slave(uint16_t irq)
{
    unsigned char mask = 0;

    if (irq < INT_VECTOR_IRQ8 || irq > (INT_VECTOR_IRQ8 + 8))
        return;

    mask = ~((unsigned char)(1 << (irq - INT_VECTOR_IRQ8)));
    
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

void disable_8259a_slave(uint16_t irq)
{
    unsigned char mask = 0;

    if (irq < INT_VECTOR_IRQ8 || irq > (INT_VECTOR_IRQ8 + 8))
        return;

    mask = (unsigned char)(1 << (irq - INT_VECTOR_IRQ8));
    
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

void set_irq_handler(uint16_t irq, void (*handler)())
{
    if (!handler || irq >= 256)
        return; 

    isr_handlers[irq] = handler;
}

void init_idt(void)
{
    int i = 0;

    for (i = 0; i < 48; i++) {
        idt[i] = gen_idesc((uint32_t)isr_table + 32 * i, 0x8, 0x8e);
    }

    idtmeta.limit = 8 * 48 - 1;
    idtmeta.base = (uint32_t)idt;

    __asm__ __volatile__("lidt idtmeta":::);
    __asm__ __volatile__("sti":::);
}

