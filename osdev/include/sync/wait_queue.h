#ifndef WAIT_QUEUE_H
#define WAIT_QUEUE_H

#include "lib/list.h"
#include "sync/spinlock.h"

typedef struct wait_queue {
    list_node waiters;
    spinlock* sp_lock;
} wait_queue;

int  wait_queue_init        (wait_queue *wq);
void wait_queue_destroy     (wait_queue *wq);
void wait_queue_sleep_locked(wait_queue *wq);
void wait_queue_wake_one    (wait_queue *wq);
void wait_queue_wake_by_tid (wait_queue *wq, u32 tid);
void wait_queue_wake_all    (wait_queue *wq);
/* Wake the first waiter on @wq.  Caller MUST already hold schedule_lock
 * (interrupts disabled) — same effect as wait_queue_wake_one but calls
 * thread_unblock_locked() instead of thread_unblock() so it is safe from
 * a waker that is itself walking the thread list (mailbox broadcast). */
void wait_queue_wake_one_locked(wait_queue *wq);

#endif
