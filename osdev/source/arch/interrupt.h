#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdint.h>

#define INT_MASTER_CMD          (0x20)
#define INT_MASTER_DATA         (0x21)
#define INT_SLAVE_CMD           (0xA0)
#define INT_SLAVE_DATA          (0xA1)
#define	INT_VECTOR_IRQ0         (0x20)
#define	INT_VECTOR_IRQ8         (0x28)

#define IDT_ENTRIES             (256)
#define NUM_EXCEPTIONS          (32)
#define NUM_INTERRUPTS          (16)

typedef struct {
    uint16_t isr_low;
    uint16_t sel_code;
    uint8_t  reserved;
    uint8_t  attrs;
    uint16_t isr_high;
} __attribute__((packed)) idesc_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idtmeta_t;

// from asm
void sys_isr_tbl(void);

void init_8259a(void);
void enable_8259a_master(uint16_t irq);
void disable_8259a_master(uint16_t irq);
void enable_8259a_slave(uint16_t irq);
void disable_8259a_slave(uint16_t irq);
void set_interrupt_handler(uint16_t irq, void (*handler)());
#endif

