#ifndef ARCH_INTERRUPT_H
#define ARCH_INTERRUPT_H

#include <stdint.h>
#include "compiler.h"
#include "irqdev.h"

#define IDT_ENTRIES             (256)

typedef struct {
    uint16_t isr_low;
    uint16_t sel_code;
    uint8_t  reserved;
    uint8_t  attrs;
    uint16_t isr_high;
} ATTR_PACKED idesc_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} ATTR_PACKED idtmeta_t;

enum arch_irq_no {
    ARCH_IRQ_BEGIN  = 0x20,
    TIMER_IRQ_NO    = 0x20,
    KEYBOARD_IRQ_NO = 0x21,
    SLAVE_IRQ_NO    = 0x22,
    COM2_IRQ_NO     = 0x23,
    COM1_IRQ_NO     = 0x24,
    LPT2_IRQ_NO     = 0x25,
    FLOPPY_IRQ_NO   = 0x26,
    LPT1_IRQ_NO     = 0x27,
    RL_TIMER_IRQ_NO = 0x28,
    RD_IRQ2_NO      = 0x29,
    RESV1_IRQ_NO    = 0x30,
    RESV2_IRQ_NO    = 0x31,
    PS2_IRQ_NO      = 0x32,
    FPU_IRQ_NO      = 0x33,
    AT_IRQ_NO       = 0x34,
    RESV3_IRQ_NO    = 0x35,
    ARCH_IRQ_END    = 0x35,
};

void arch_unmask_irq(uint16_t irq_nr);
void arch_mask_irq(uint16_t irq_nr);
// void arch_syscall(uint32_t minor, void* data);
void arch_syscall(void);
#endif
