/*
 * arch/i386/crash.c — deliberately trigger x86 exceptions (diagnostic).
 *
 * Used by kernel.c's "crash" command to exercise the exception handler:
 * each helper forces the named fault in a controlled way.  The triggering
 * instructions are x86-specific, so they live here; kernel.c only calls
 * the arch_crash_*() wrappers.
 */

#include "arch_irq.h"     /* arch_crash_* prototypes */
#include "regs.h"

void arch_crash_div0(void)
{
    volatile int zero = 0;
    volatile int x = 1 / zero;   /* #DE Divide Error */
    (void)x;
}

void arch_crash_ud(void)
{
    __asm__ volatile("ud2");     /* #UD Invalid Opcode */
}

void arch_crash_pf(void)
{
    *(volatile int*)0xDEAD0000 = 0;   /* #PF Page Fault (NULL-ish deref) */
}

void arch_crash_gp(void)
{
    /* Privileged instruction from this context — #GP. */
    __asm__ volatile("cli; hlt");
}

void arch_crash_bp(void)
{
    __asm__ volatile("int3");         /* #BP Breakpoint */
}
