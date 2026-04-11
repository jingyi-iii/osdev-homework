#include "arch_regs.h"
#include "arch_protm.h"
#include "arch_irq.h"
#include "spinlock.h"
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
#define IDT_GATE_SYSCALL32      (0xEE)

#define NUM_EXCEPTIONS          (32)
#define NUM_INTERRUPTS          (16)

int32_t k_reenter = -1;
static idtmeta_t idtmeta = { 0 };
static ATTR_ALIGINED(idesc_t) idesc_t idt[IDT_ENTRIES] = { 0 };
void arch_isr_tbl(void);
void arch_syscall_entry(void);
static void hlt_handler(void) { for (;;) __asm__ volatile ("hlt"); }

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

void arch_unmask_irq(uint16_t irq_nr)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_nr < ARCH_IRQ_BEGIN || irq_nr > ARCH_IRQ_END)
        return;

    if (irq_nr >= RL_TIMER_IRQ_NO) {
        mask = ~((unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ8)));
        port_val = arch_inb(0xa1) & mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = ~((unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ0)));
        port_val = arch_inb(0x21) & mask;
        arch_outb(0x21, port_val);
    }
}

void arch_mask_irq(uint16_t irq_nr)
{
    unsigned char mask = 0;
    uint8_t port_val = 0;

    if (irq_nr < ARCH_IRQ_BEGIN || irq_nr > ARCH_IRQ_END)
        return;

    if (irq_nr >= RL_TIMER_IRQ_NO) {
        mask = (unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ8));
        port_val = arch_inb(0xa1) | mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = (unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ0));
        port_val = arch_inb(0x21) | mask;
        arch_outb(0x21, port_val);
    }
}

void arch_init_irq(void)
{
    int i = 0;

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

    // syscall
    idt[100] = gen_idesc((uint32_t)arch_syscall_entry,
                           arch_get_sel(SYS_CODE),
                           IDT_GATE_SYSCALL32);

    idtmeta.limit = sizeof(idesc_t) * IDT_ENTRIES - 1;
    idtmeta.base = (uint32_t)idt;
    arch_reload_idt(&idtmeta);
    arch_sti();

    arch_init_8259a();
}

void arch_syscall(uint32_t minor, void* data)
{
    __asm__ __volatile__(
            "movl %0,       %%eax   \n\t"
            "movl %1,       %%ebx   \n\t"
            "int  $100              \n\t"
            :
            :"g"(minor), "g"(data)
            :"eax", "ebx"
    );
}
