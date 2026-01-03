#ifndef LOCK_H
#define LOCK_H

#define mb()        __asm__ __volatile__("mfence" ::: "memory")
#define rmb()       __asm__ __volatile__("lfence" ::: "memory")
#define wmb()       __asm__ __volatile__("sfence" ::: "memory")
#define barrier()   __asm__ __volatile__("" ::: "memory")

typedef struct {
    volatile int lock;
} spinlock_t;

void spinlock_init(spinlock_t* lock);
void spinlock_lock(spinlock_t* lock);
int spinlock_trylock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
#endif
