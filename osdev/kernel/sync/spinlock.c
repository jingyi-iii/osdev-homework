#include "sync/spinlock.h"
#include "kernel/errno.h"
#include "arch_irq.h"      /* arch_irq_save/arch_irq_restore */

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

/*
 * spinlock_lock_irqsave - disable interrupts (save EFLAGS), then acquire
 * the lock.  Returns the saved EFLAGS; pass it to
 * spinlock_unlock_irqrestore() when releasing.
 *
 * Guarantees the holder can never be preempted by an interrupt handler, so
 * an ISR-side acquisition (spinlock_lock) always terminates.
 *
 * The x86 interrupt-mask mechanics (EFLAGS.IF plus the CPL3 IOPL=0
 * restriction) live in arch_irq_save()/arch_irq_restore() (arch_irq.h):
 * at CPL3 the irq-save variant degrades to a plain spinlock — the flag is
 * still saved/restored, but interrupts are not masked while the lock is
 * held.
 *
 * spinlock_lock(NULL) is a no-op, so a NULL lock still gets a correct
 * save/restore of the interrupt flag.
 */
u32 spinlock_lock_irqsave(spinlock* lock)
{
    u32 eflags = arch_irq_save();
    spinlock_lock(lock);
    return eflags;
}

void spinlock_unlock_irqrestore(spinlock* lock, u32 eflags)
{
    spinlock_unlock(lock);
    arch_irq_restore(eflags);
}
