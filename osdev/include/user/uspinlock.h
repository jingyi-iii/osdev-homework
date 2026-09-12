#ifndef USER_USPINLOCK_H
#define USER_USPINLOCK_H

#include "lib/types.h"

/*
 * User-mode spinlock (Plan A).
 *
 * A plain atomic spinlock usable from the ring-3 server processes.
 * Unlike the kernel spinlock (kernel/sync/spinlock.c) it needs no
 * privileged instruction and no kernel-owned lock object: the lock word
 * lives in user-accessible memory (a static in the server's own data, or
 * the user heap via malloc).  Implemented inline so it is available both
 * to kernel-image code (the *_server drivers) and to user programs.
 *
 * Contract (single-CPU, preemptive scheduler):
 *   - provides user-vs-user mutual exclusion ONLY;
 *   - does NOT mask interrupts (no cli at CPL3, IOPL=0) and does NOT
 *     protect against kernel/ISR code — that is the kernel's job;
 *   - critical sections must be SHORT and must NEVER block while holding
 *     the lock (mailbox/portal/semaphore/thread_yield): on a preemptive
 *     single CPU the holder may be descheduled, in which case a waiter
 *     spins until the holder's next timeslice.
 */

typedef struct uspinlock {
    volatile u32 state;          /* 0 = unlocked, 1 = locked */
} uspinlock;

#define USPINLOCK_INIT  { .state = 0 }

static inline void uspin_lock(uspinlock* l)
{
    while (__atomic_exchange_n(&l->state, 1, __ATOMIC_ACQUIRE) == 1)
        __asm__ __volatile__("pause" ::: "memory");
}

static inline void uspin_unlock(uspinlock* l)
{
    __atomic_store_n(&l->state, 0, __ATOMIC_RELEASE);
}

/* Returns 1 if the lock was acquired, 0 if it was already held. */
static inline int uspin_trylock(uspinlock* l)
{
    u32 expected = 0;
    return !__atomic_compare_exchange_n(&l->state, &expected, 1, 0,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

#endif
