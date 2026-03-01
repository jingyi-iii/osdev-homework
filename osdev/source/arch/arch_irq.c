#include "arch_regs.h"
#include "arch_protm.h"
#include "arch_irq.h"
#include "lockmgr.h"

#define INT_MASTER_CMD          (0x20)
#define INT_MASTER_DATA         (0x21)
#define INT_SLAVE_CMD           (0xA0)
#define INT_SLAVE_DATA          (0xA1)
#define	INT_VECTOR_IRQ0         (0x20)
#define	INT_VECTOR_IRQ8         (0x28)

#define IDT_GATE_TASK32         (0x85)
#define IDT_GATE_INT16          (0x86)
#define IDT_GATE_TRAP16         (0x87)
#define IDT_GATE_INT32          (0x8E)
#define IDT_GATE_TRAP32         (0x8F)

#define IDT_ENTRIES             (256)
#define NUM_EXCEPTIONS          (32)
#define NUM_INTERRUPTS          (16)

#define ATTR_ALIGINED(T)    \
    __attribute__((aligned(sizeof(T))))
typedef void (*isr_t)(void);

static void hlt_handler(void) { for (;;) __asm__ volatile ("hlt"); }

isr_t user_isrs[IDT_ENTRIES]= { 0 };
static idtmeta_t idtmeta = { 0 };
static spinlock_dev* irq_lock;
static ATTR_ALIGINED(idesc_t) idesc_t idt[IDT_ENTRIES] = { 0 };

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

static void arch_init_8259a(void)
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

void arch_unmask_irq(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < ARCH_IRQ_BEGIN || irq_no > ARCH_IRQ_END)
        return;

    irq_lock->lock(irq_lock);
    if (irq_no >= RL_TIMER_IRQ_NO) {
        mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8)));
        port_val = arch_inb(0xa1) & mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = ~((unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0)));
        port_val = arch_inb(0x21) & mask;
        arch_outb(0x21, port_val);
    }
    irq_lock->unlock(irq_lock);
}

void arch_mask_irq(uint16_t irq_no)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_no < ARCH_IRQ_BEGIN || irq_no > ARCH_IRQ_END)
        return;

    irq_lock->lock(irq_lock);
    if (irq_no >= RL_TIMER_IRQ_NO) {
        mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ8));
        port_val = arch_inb(0xa1) | mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = (unsigned char)(1 << (irq_no - INT_VECTOR_IRQ0));
        port_val = arch_inb(0x21) | mask;
        arch_outb(0x21, port_val);
    }
    irq_lock->unlock(irq_lock);
}

void arch_set_isr(uint16_t irq_no, void (*handler)())
{
    if (irq_no >= IDT_ENTRIES)
        return; 

    irq_lock->lock(irq_lock);
    user_isrs[irq_no] = handler ? handler : hlt_handler;
    irq_lock->unlock(irq_lock);
}

void arch_isr_tbl(void);
void arch_init_irq(void)
{
    int i = 0;

    if (SPIN_LOCK_INIT(irq_lock))
        return;
        
    arch_cli();
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = gen_idesc((uint32_t)hlt_handler,
                           arch_get_sel(SYS_CODE),
                           IDT_GATE_INT32);
    }
    for (i = 0; i < NUM_EXCEPTIONS + NUM_INTERRUPTS; i++) {
        idt[i] = gen_idesc((uint32_t)arch_isr_tbl + 256 * i,
                           arch_get_sel(SYS_CODE),
                           IDT_GATE_INT32);
    }

    idtmeta.limit = sizeof(idesc_t) * IDT_ENTRIES - 1;
    idtmeta.base = (uint32_t)idt;
    arch_reload_idt(&idtmeta);
    arch_sti();

    arch_init_8259a();
}

