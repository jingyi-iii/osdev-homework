#ifndef __LOCK_H__
#define __LOCK_H__

#define mb()        __asm__ __volatile__("mfence" ::: "memory")
#define rmb()       __asm__ __volatile__("lfence" ::: "memory")
#define wmb()       __asm__ __volatile__("sfence" ::: "memory")
#define barrier()   __asm__ __volatile__("" ::: "memory")

typedef enum lock_type {
    SPIN_LOCK,
} lock_type;

typedef struct spinlock_dev {
    volatile int allocated;
    volatile int state;

    int (*lock)(struct spinlock_dev *dev);
    int (*trylock)(struct spinlock_dev *dev);
    int (*unlock)(struct spinlock_dev *dev);
} spinlock_dev;

int lockmgr_alloc_dev(lock_type type, void **out_dev);
void lockmgr_free_dev(lock_type type, void *dev);

#define SPIN_LOCK_INIT(l)                                   \
    lockmgr_alloc_dev(SPIN_LOCK, (void**)&(l))

#define SPIN_LOCK_RELEASE(l)                                \
    do {                                                    \
        if (l) {                                            \
            lockmgr_free_dev(SPIN_LOCK, (void*)(l));        \
            (l) = 0;                                        \
        }                                                   \
    } while (0)

#endif
