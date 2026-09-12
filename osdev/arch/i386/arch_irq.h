#ifndef ARCH_INTERRUPT_H
#define ARCH_INTERRUPT_H

#include "lib/types.h"
#include "lib/compiler.h"
#include "kernel/irq.h"

#define IDT_ENTRIES             (256)

typedef struct {
    u16 isr_low;
    u16 sel_code;
    u8  reserved;
    u8  attrs;
    u16 isr_high;
} ATTR_PACKED idesc;

typedef struct {
    u16 limit;
    u32 base;
} ATTR_PACKED idtmeta;

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

void arch_unmask_irq(u16 irq_nr);
void arch_mask_irq(u16 irq_nr);
int arch_syscall(u32 handle, void* data, size_t data_size);

/*
 * Gate reentrancy counter (arch/i386/irq.c): -1 = idle, 0 = inside a
 * syscall/IRQ gate, >0 = nested.  Ring-0 code uses it to decide whether a
 * context-switching call may run directly inside the current gate frame.
 */
extern i32 irq_reenter_cnt;

/*
 * Return 1 if the caller is currently executing at CPL3 (user mode),
 * 0 otherwise.  Drivers use this to decide whether privileged I/O
 * must go through the syscall gate instead of being done directly.
 */
static inline int arch_running_ring3(void)
{
    u16 cs;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    return (cs & 3) == 3;
}

/*
 * IRQ-flag save/restore (x86 EFLAGS.IF).  arch_irq_save() masks interrupts
 * when the caller runs at CPL0; at CPL3 (IOPL=0) cli would #GP, so it only
 * records the flag state and the pair degrades to a no-op mask — the same
 * behaviour the spinlock irq-save pair relies on.
 */
static inline u32 arch_irq_save(void)
{
    u32 eflags;
    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    if (!arch_running_ring3())
        __asm__ __volatile__("cli" ::: "memory");
    return eflags;
}

static inline void arch_irq_restore(u32 eflags)
{
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}

/*
 * Return 1 while inside a syscall/IRQ gate (irq_reenter_cnt == 0),
 * 0 otherwise.  Ring-0 code uses it to decide whether a context-switching
 * call may run directly inside the current gate frame.
 */
static inline int arch_in_gate(void)
{
    return irq_reenter_cnt == 0;
}

/*
 * Deliberately trigger x86 exceptions (diagnostic "crash" command,
 * see kernel.c).  The actual faulting instructions live in arch/i386/crash.c.
 */
void arch_crash_div0(void);
void arch_crash_ud(void);
void arch_crash_pf(void);
void arch_crash_gp(void);
void arch_crash_bp(void);

#endif
