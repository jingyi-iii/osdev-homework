#include "sync/semaphore.h"
#include "lib/module.h"
#include "drivers/log_server.h"

DECLARE_HEAD_NODE(semaphore_head);
static spinlock* semaphore_lock = 0;

static void semaphore_subsys_init(void)
{
    semaphore_lock = spinlock_alloc();
    if (!semaphore_lock) {
        LOG("failed to alloc spin lock for semaphore registry\n");
    }
}
module_init(semaphore_subsys_init);

semaphore* semaphore_create(int init_value)
{
    static int next_id = 0;

    if (!semaphore_lock)
        return 0;

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

    spinlock_lock(semaphore_lock);
    list_add(&sem->this_node, &semaphore_head);
    spinlock_unlock(semaphore_lock);

    return sem;
}

int semaphore_destroy(semaphore* sem)
{
    if (!sem)
        return E_INVAL;

    spinlock_lock(sem->wq.sp_lock);
    if (!list_empty(&sem->wq.waiters)) {
        spinlock_unlock(sem->wq.sp_lock);
        return E_BUSY;
    }
    spinlock_unlock(sem->wq.sp_lock);

    spinlock_lock(semaphore_lock);
    list_del(&sem->this_node);
    spinlock_unlock(semaphore_lock);

    spinlock_release(sem->wq.sp_lock);
    kfree(sem);

    return 0;
}

semaphore* semaphore_get(int id)
{
    semaphore* sem = 0;
    int found = 0;

    spinlock_lock(semaphore_lock);
    list_for_each(node, &semaphore_head) {
        sem = list_entry(node, semaphore, this_node);
        if (!sem || sem->id != id)
            continue;

        found = 1;
        break;
    }
    spinlock_unlock(semaphore_lock);

    return found ? sem : 0;
}

int semaphore_wait(int semid)
{
    semaphore* sem = semaphore_get(semid);
    if (!sem)
        return E_INVAL;

    spinlock_lock(sem->wq.sp_lock);
    sem->value--;
    if (sem->value < 0)
        wait_queue_sleep_locked(&sem->wq);
    spinlock_unlock(sem->wq.sp_lock);

    return 0;
}

int semaphore_signal(int semid)
{
    int wake = 0;
    semaphore* sem = semaphore_get(semid);
    if (!sem)
        return E_INVAL;

    spinlock_lock(sem->wq.sp_lock);
    sem->value++;
    if (sem->value <= 0)
        wake = 1;
    spinlock_unlock(sem->wq.sp_lock);

    if (wake)
        wait_queue_wake_one(&sem->wq);
    return 0;
}
