#include "sync/wait_queue.h"
#include "kernel/process.h"
#include "drivers/log_server.h"

int wait_queue_init(wait_queue *wq)
{
    wq->sp_lock = spinlock_alloc();
    if (!wq->sp_lock) {
        LOG("failed to alloc spin lock for wait queue\n");
        return E_LIMIT;
    }
    list_init(&wq->waiters);

    return 0;
}

void wait_queue_destroy(wait_queue *wq)
{
    spinlock_release(wq->sp_lock);
    wq->sp_lock = 0;
}

void wait_queue_sleep_locked(wait_queue *wq)
{
    tcb* curr = thread_get_by_tid(thread_get_tid());
    if (!curr || curr->waiting_on)
        return;

    curr->waiting_on = wq;
    list_add(&curr->wait_node, &wq->waiters);

    spinlock_unlock(wq->sp_lock);
    thread_block(curr->tid);
    spinlock_lock(wq->sp_lock);
}

void wait_queue_wake_one(wait_queue *wq)
{
    spinlock_lock(wq->sp_lock);
    if (list_empty(&wq->waiters)) {
        spinlock_unlock(wq->sp_lock);
        return;
    }

    list_for_each_safe(pos, n, &wq->waiters) {
        list_node* node = pos;
        tcb* t = list_entry(node, tcb, wait_node);

        if (t->waiting_on != wq) {
            continue;
        }

        t->waiting_on = 0;
        list_del(node);
        spinlock_unlock(wq->sp_lock);
        thread_unblock(t->tid);
        return;
    }
    spinlock_unlock(wq->sp_lock);
}

void wait_queue_wake_by_tid(wait_queue *wq, u32 tid)
{
    spinlock_lock(wq->sp_lock);
    if (list_empty(&wq->waiters)) {
        spinlock_unlock(wq->sp_lock);
        return;
    }

    list_for_each_safe(pos, n, &wq->waiters) {
        list_node* node = pos;
        tcb* t = list_entry(node, tcb, wait_node);

        if (t->waiting_on != wq || t->tid != tid) {
            continue;
        }

        t->waiting_on = 0;
        list_del(node);
        spinlock_unlock(wq->sp_lock);
        thread_unblock(t->tid);
        return;
    }
    spinlock_unlock(wq->sp_lock);
}

void wait_queue_wake_all(wait_queue *wq)
{
    spinlock_lock(wq->sp_lock);
    if (list_empty(&wq->waiters)) {
        spinlock_unlock(wq->sp_lock);
        return;
    }

    list_for_each_safe(pos, n, &wq->waiters) {
        list_node* node = pos;
        tcb* t = list_entry(node, tcb, wait_node);

        if (t->waiting_on != wq) {
            continue;
        }

        t->waiting_on = 0;
        list_del(node);
        thread_unblock(t->tid);
    }
    spinlock_unlock(wq->sp_lock);
}