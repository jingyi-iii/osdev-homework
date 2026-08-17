#include "sync/spinlock.h"
#include "kernel/errno.h"

#define SPIN_LOCK_MAX_COUNT (1024)

static spinlock spinlocks[SPIN_LOCK_MAX_COUNT] = {0};

spinlock* spinlock_alloc(void)
{
    int i = 0;

    for (i = 0; i < SPIN_LOCK_MAX_COUNT; i++) {
        if (spinlocks[i].state == LOCK_INVALID) {
            spinlocks[i].state = LOCK_UNLOCKED;
            return &spinlocks[i];
        }
    }
    return 0;
}

void spinlock_release(spinlock* lock)
{
    if (!lock)
        return;

    lock->state = LOCK_INVALID;
}

int spinlock_lock(spinlock* lock)
{
    if (!lock)
        return E_INVAL;

    while (__atomic_exchange_n(&lock->state, LOCK_LOCKED,
                               __ATOMIC_ACQUIRE) == LOCK_LOCKED) {
        __asm__ __volatile__("pause" ::: "memory");
    }
    barrier();
    return 0;
}

int spinlock_trylock(spinlock *lock)
{
    if (!lock)
        return E_INVAL;

    int expected = LOCK_UNLOCKED;
    return !__atomic_compare_exchange_n(&lock->state, &expected, LOCK_LOCKED,
                                       0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

int spinlock_unlock(spinlock *lock)
{
    if (!lock)
        return E_INVAL;

    __atomic_store_n(&lock->state, LOCK_UNLOCKED, __ATOMIC_RELEASE);
    return 0;
}

/* 1 if the caller executes at CPL3 (user mode).  Same idea as
 * arch_running_ring3() but kept local to avoid pulling in arch_irq.h. */
static inline int spin_running_ring3(void)
{
    u16 cs;
    __asm__ __volatile__("mov %%cs, %0" : "=r"(cs));
    return (cs & 3) == 3;
}

/*
 * spinlock_lock_irqsave - disable interrupts (save EFLAGS), then acquire
 * the lock.  Returns the saved EFLAGS; pass it to
 * spinlock_unlock_irqrestore() when releasing.
 *
 * Guarantees the holder can never be preempted by an interrupt handler, so
 * an ISR-side acquisition (spinlock_lock) always terminates.
 *
 * RING3 (CPL3) runs with IOPL=0 now (see arch/i386/task.c), so cli is NOT
 * available there.  At CPL3 the irq-save variant degrades to a plain
 * spinlock: the interrupt flag is still saved/restored (popfl at CPL3
 * simply ignores the IF/IOPL bits), but interrupts are not masked while
 * the lock is held.
 *
 * spinlock_lock(NULL) is a no-op, so a NULL lock still gets a correct
 * save/restore of the interrupt flag.
 */
u32 spinlock_lock_irqsave(spinlock* lock)
{
    u32 eflags;
    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    if (!spin_running_ring3())
        __asm__ __volatile__("cli" ::: "memory");
    spinlock_lock(lock);
    return eflags;
}

void spinlock_unlock_irqrestore(spinlock* lock, u32 eflags)
{
    spinlock_unlock(lock);
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}
