#include "arch_regs.h"
#include "arch_protm.h"
#include "arch_irq.h"
#include "lockmgr.h"
#include "privilege.h"

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


static void hlt_handler(void) { for (;;) __asm__ volatile ("hlt"); }

// irq_handler user_isrs[IDT_ENTRIES]= { 0 };

irqdev* irqdevs[IDT_ENTRIES] = {0};
irqline* irqlines[IDT_ENTRIES] = {0};

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

static void arch_unmask_irq(uint16_t irq_nr)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_nr < ARCH_IRQ_BEGIN || irq_nr > ARCH_IRQ_END)
        return;

    irq_lock->lock(irq_lock);
    if (irq_nr >= RL_TIMER_IRQ_NO) {
        mask = ~((unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ8)));
        port_val = arch_inb(0xa1) & mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = ~((unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ0)));
        port_val = arch_inb(0x21) & mask;
        arch_outb(0x21, port_val);
    }
    irq_lock->unlock(irq_lock);
}

static void arch_mask_irq(uint16_t irq_nr)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_nr < ARCH_IRQ_BEGIN || irq_nr > ARCH_IRQ_END)
        return;

    irq_lock->lock(irq_lock);
    if (irq_nr >= RL_TIMER_IRQ_NO) {
        mask = (unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ8));
        port_val = arch_inb(0xa1) | mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = (unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ0));
        port_val = arch_inb(0x21) | mask;
        arch_outb(0x21, port_val);
    }
    irq_lock->unlock(irq_lock);
}

// static void arch_set_isr(uint16_t irq_nr, irq_handler handler)
// {
//     if (irq_nr >= IDT_ENTRIES)
//         return; 

//     irq_lock->lock(irq_lock);
//     user_isrs[irq_nr] = handler;
//     irq_lock->unlock(irq_lock);
// }

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

static int irq_dev_mask(struct irqdev* dev)
{
    if (!dev)
        return -1;

    dev->enabled = 0;
    if (irqlines[dev->irq_nr]) {
        irqlines[dev->irq_nr]->mask(irqlines[dev->irq_nr]);
    }

    // arch_mask_irq(dev->irq_nr);

    return 0;
}

static int irq_dev_unmask(struct irqdev* dev)
{
    if (!dev)
        return -1;

    dev->enabled = 1;
    if (irqlines[dev->irq_nr]) {
        irqlines[dev->irq_nr]->unmask(irqlines[dev->irq_nr]);
    }

    // arch_set_isr(dev->irq_nr, dev->handler);
    // arch_unmask_irq(dev->irq_nr);

    return 0;
}

// struct irqdev* get_dev_by_irq_nr(uint32_t irq_nr)
// {
//     return irqdevs[irq_nr];
// }

int irqdev_init(irqdev **out_dev, const char* name, uint32_t irq_nr, irq_handler handler)
{
    if (!out_dev || irq_nr >= IDT_ENTRIES)
        return -1;

    irqdev* dev = 0;
    int ret = 0;

    ret = irq_alloc_dev(irq_nr, name, 0, handler, out_dev);
    if (ret != 0)
        return ret;
    
    dev = *out_dev;
    dev->mask = irq_dev_mask;
    dev->unmask = irq_dev_unmask;

    if (!irqlines[irq_nr]) {
        if (!irqline_init(&irqlines[irq_nr], irq_nr) && irqlines[irq_nr])
            irqlines[irq_nr]->add(irqlines[irq_nr], dev);
    }

    return 0;
}

static int irqline_mask(struct irqline* line)
{
    if (!line)
        return -1;
    if (!line->devs)
        return 0;

    int enable = 1;

    irqdev* dev = line->devs;
    while (dev) {
        if (!dev->enabled) {
            enable = 0;
            break;
        }
        dev = dev->next;
    }

    if (!enable)
        arch_mask_irq(line->irq_nr);

    return 0;
}

static int irqline_unmask(struct irqline* line)
{
    if (!line)
        return -1;
    if (!line->devs)
        return 0;

    irqdev* dev = line->devs;
    while (dev) {
        if (dev->enabled) {
            arch_unmask_irq(line->irq_nr);
            break;
        }
        dev = dev->next;
    }

    return 0;
}

static int irqline_add(struct irqline* line, struct irqdev* dev)
{
    if (!line || !dev)
        return -1;

    line->sp_lock->lock(line->sp_lock);

    if (!line->devs) {
        line->devs = dev;
    } else {
        irqdev* tail = line->devs;
        while (tail->next)
            tail = tail->next;
        tail->next = dev;
        dev->next = 0;
    }

    line->sp_lock->unlock(line->sp_lock);

    return 0;
}

static int irqline_remove(struct irqline* line, struct irqdev* dev)
{
    return 0;
}

static int irqline_remove_all(struct irqline* line)
{
    return 0;
}

int irqline_init(irqline** out_line, uint32_t irq_nr)
{
    if (!out_line || irq_nr >= IDT_ENTRIES)
        return -1;

    irqline* line = 0;
    int ret = 0;

    ret = irq_alloc_line(irq_nr, out_line);
    if (ret != 0)
        return ret;
    
    line = *out_line;
    line->mask = irqline_mask;
    line->unmask = irqline_unmask;
    line->add = irqline_add;
    line->remove = irqline_remove;
    line->remove_all = irqline_remove_all;

    SPIN_LOCK_INIT(line->sp_lock);

    return 0;   
}

void irqline_handler(uint32_t irq_nr)
{
    if (!irqlines[irq_nr])
        return;

    irqdev* dev = irqlines[irq_nr]->devs;
    while (dev) {
        if (dev->enabled) {
            dev->handler(dev);
        }
        dev = dev->next;
    }
}