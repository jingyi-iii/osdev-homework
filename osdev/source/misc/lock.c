#include "lock.h"

#define SPIN_LOCK_UNLOCKED (0)
#define SPIN_LOCK_LOCKED   (1)

void spinlock_init(spinlock_t* lock)
{
    __atomic_store_n(&lock->lock, SPIN_LOCK_UNLOCKED, __ATOMIC_RELAXED);
}

void spinlock_lock(spinlock_t* lock)
{
    while (__atomic_exchange_n(&lock->lock, SPIN_LOCK_LOCKED,
                               __ATOMIC_ACQUIRE) == SPIN_LOCK_LOCKED) {
        __asm__ __volatile__("pause" ::: "memory");
    }
}

int spinlock_trylock(spinlock_t *lock)
{
    int expected = SPIN_LOCK_UNLOCKED;
    return __atomic_compare_exchange_n(&lock->lock, &expected, SPIN_LOCK_LOCKED, 
                                       0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

void spinlock_unlock(spinlock_t *lock)
{
    __atomic_store_n(&lock->lock, SPIN_LOCK_UNLOCKED, __ATOMIC_RELEASE);
}