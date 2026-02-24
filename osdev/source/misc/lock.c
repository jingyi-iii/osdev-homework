#include "lock.h"

#define SPIN_LOCK_UNLOCKED  (0)
#define SPIN_LOCK_LOCKED    (1)
#define SPIN_LOCK_MAX_COUNT (1024)

static spinlock_dev spinlocks[SPIN_LOCK_MAX_COUNT] = {0};

static int lock(spinlock_dev* dev)
{
    if (!dev)
        return -1;

    while (__atomic_exchange_n(&dev->state, SPIN_LOCK_LOCKED,
                               __ATOMIC_ACQUIRE) == SPIN_LOCK_LOCKED) {
        __asm__ __volatile__("pause" ::: "memory");
    }
    return 0;
}

static int trylock(spinlock_dev *dev)
{
    if (!dev)
        return -1;

    int expected = SPIN_LOCK_UNLOCKED;
    return !__atomic_compare_exchange_n(&dev->state, &expected, SPIN_LOCK_LOCKED,
                                       0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static int unlock(spinlock_dev *dev)
{
    if (!dev)
        return -1;

    __atomic_store_n(&dev->state, SPIN_LOCK_UNLOCKED, __ATOMIC_RELEASE);
    return 0;
}

static int init(spinlock_dev* dev)
{
    if (!dev)
        return -1;

    __atomic_store_n(&dev->state, SPIN_LOCK_UNLOCKED, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->allocated, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->lock, lock, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->trylock, trylock, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->unlock, unlock, __ATOMIC_RELAXED);

    return 0;
}

static int destroy(spinlock_dev* dev)
{
    if (!dev)
        return -1;

    __atomic_store_n(&dev->state, SPIN_LOCK_UNLOCKED, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->allocated, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->lock, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->trylock, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&dev->unlock, 0, __ATOMIC_RELAXED);

    return 0;
}

int spinlock_alloc_dev(spinlock_dev **out_dev)
{
    int i = 0;

    if (!out_dev)
        return -1;

    for (i = 0; i < SPIN_LOCK_MAX_COUNT; i++) {
        if (spinlocks[i].allocated == 0) {
            spinlocks[i].allocated = 1;
            *out_dev = &spinlocks[i];
            return init(*out_dev);
        }
    }

    *out_dev = 0;
    return -1;
}

int spinlock_free_dev(spinlock_dev *dev)
{
    if (!dev)
        return -1;

    return destroy(dev);
}
