#include "spinlock.h"

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
        return -1;

    while (__atomic_exchange_n(&lock->state, LOCK_LOCKED,
                               __ATOMIC_ACQUIRE) == LOCK_LOCKED) {
        __asm__ __volatile__("pause" ::: "memory");
    }
    return 0;
}

int spinlock_trylock(spinlock *lock)
{
    if (!lock)
        return -1;

    int expected = LOCK_UNLOCKED;
    return !__atomic_compare_exchange_n(&lock->state, &expected, LOCK_LOCKED,
                                       0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

int spinlock_unlock(spinlock *lock)
{
    if (!lock)
        return -1;

    __atomic_store_n(&lock->state, LOCK_UNLOCKED, __ATOMIC_RELEASE);
    return 0;
}
