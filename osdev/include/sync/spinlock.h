#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include "lib/types.h"

#define mb()        __asm__ __volatile__("mfence" ::: "memory")
#define rmb()       __asm__ __volatile__("lfence" ::: "memory")
#define wmb()       __asm__ __volatile__("sfence" ::: "memory")
#define barrier()   __asm__ __volatile__("" ::: "memory")

typedef enum lock_state {
    LOCK_INVALID = 0,
    LOCK_UNLOCKED,
    LOCK_LOCKED,
} lock_state;

typedef struct spinlock {
    volatile lock_state state;
} spinlock;

spinlock* spinlock_alloc(void);
void spinlock_release(spinlock* lock);
int spinlock_lock(spinlock* lock);
int spinlock_trylock(spinlock* lock);
int spinlock_unlock(spinlock* lock);

/* IRQ-safe variants: disable interrupts while holding the lock, so on this
 * single-CPU kernel an ISR can never preempt the holder (which would let a
 * preempted holder deadlock an ISR-side spin).  RING3 runs with IOPL=0
 * (see arch/i386/task.c), so at CPL3 cli is not available and the pair
 * degrades to a plain spinlock with an EFLAGS save/restore. */
u32 spinlock_lock_irqsave(spinlock* lock);
void spinlock_unlock_irqrestore(spinlock* lock, u32 eflags);

#endif
