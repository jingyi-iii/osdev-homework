#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#ifdef __cplusplus
extern "C" {
#endif

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
void spinlock_free(spinlock* lock);
int spinlock_lock(spinlock* lock);
int spinlock_trylock(spinlock* lock);
int spinlock_unlock(spinlock* lock);

#ifdef __cplusplus
}
#endif

#endif
