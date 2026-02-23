#ifndef __LOCK_H__
#define __LOCK_H__

#define mb()        __asm__ __volatile__("mfence" ::: "memory")
#define rmb()       __asm__ __volatile__("lfence" ::: "memory")
#define wmb()       __asm__ __volatile__("sfence" ::: "memory")
#define barrier()   __asm__ __volatile__("" ::: "memory")

typedef struct spinlock_dev {
    volatile int state;
    volatile int allocated;

    int (*lock)(struct spinlock_dev *dev);
    int (*trylock)(struct spinlock_dev *dev);
    int (*unlock)(struct spinlock_dev *dev);
} spinlock_dev;

int spinlock_alloc_dev(spinlock_dev **out_dev);
int spinlock_free_dev(spinlock_dev *dev);

#endif
