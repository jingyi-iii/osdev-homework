#include "sync/semaphore.h"
#include "lib/module.h"
#include "mm/heap.h"
#include "kernel/log.h"

DECLARE_HEAD_NODE(semaphore_head);
static spinlock semaphore_lock = { .state = LOCK_UNLOCKED };

semaphore* semaphore_create(int init_value)
{
    static int next_id = 0;

    semaphore* sem = (semaphore*)kmalloc(sizeof(semaphore));
    if (!sem)
        return 0;

    if (wait_queue_init(&sem->wq) != 0) {
        kfree(sem);
        return 0;
    }

    sem->id = __atomic_fetch_add(&next_id, 1, __ATOMIC_RELAXED);
    sem->value = init_value;
    list_init(&sem->this_node);

    u32 eflags = spinlock_lock_irqsave(&semaphore_lock);
    list_add(&sem->this_node, &semaphore_head);
    spinlock_unlock_irqrestore(&semaphore_lock, eflags);

    return sem;
}

int semaphore_destroy(semaphore* sem)
{
    if (!sem)
        return E_INVAL;

    u32 eflags = spinlock_lock_irqsave(sem->wq.sp_lock);
    if (!list_empty(&sem->wq.waiters)) {
        spinlock_unlock_irqrestore(sem->wq.sp_lock, eflags);
        return E_BUSY;
    }
    spinlock_unlock_irqrestore(sem->wq.sp_lock, eflags);

    eflags = spinlock_lock_irqsave(&semaphore_lock);
    list_del(&sem->this_node);
    spinlock_unlock_irqrestore(&semaphore_lock, eflags);

    spinlock_release(sem->wq.sp_lock);
    kfree(sem);

    return 0;
}

semaphore* semaphore_get(int id)
{
    semaphore* sem = 0;
    int found = 0;

    u32 eflags = spinlock_lock_irqsave(&semaphore_lock);
    list_for_each(node, &semaphore_head) {
        sem = list_entry(node, semaphore, this_node);
        if (!sem || sem->id != id)
            continue;

        found = 1;
        break;
    }
    spinlock_unlock_irqrestore(&semaphore_lock, eflags);

    return found ? sem : 0;
}

int semaphore_wait(int semid)
{
    semaphore* sem = semaphore_get(semid);
    if (!sem)
        return E_INVAL;

    u32 eflags = spinlock_lock_irqsave(sem->wq.sp_lock);
    sem->value--;
    if (sem->value < 0)
        wait_queue_sleep_locked(&sem->wq);
    spinlock_unlock_irqrestore(sem->wq.sp_lock, eflags);

    return 0;
}

int semaphore_signal(int semid)
{
    int wake = 0;
    semaphore* sem = semaphore_get(semid);
    if (!sem)
        return E_INVAL;

    u32 eflags = spinlock_lock_irqsave(sem->wq.sp_lock);
    sem->value++;
    if (sem->value <= 0)
        wake = 1;
    spinlock_unlock_irqrestore(sem->wq.sp_lock, eflags);

    if (wake)
        wait_queue_wake_one(&sem->wq);
    return 0;
}

int semaphore_has_waiters(semaphore* sem)
{
    int busy = 0;

    if (!sem)
        return 0;

    u32 eflags = spinlock_lock_irqsave(sem->wq.sp_lock);
    busy = !list_empty(&sem->wq.waiters);
    spinlock_unlock_irqrestore(sem->wq.sp_lock, eflags);

    return busy;
}
