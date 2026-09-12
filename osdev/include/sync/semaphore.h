#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "lib/list.h"
#include "sync/spinlock.h"
#include "sync/wait_queue.h"

typedef struct semaphore {
    int         id;
    int         value;
    list_node   this_node;
    wait_queue  wq;
} semaphore;

semaphore* semaphore_create (int init_value);
int        semaphore_destroy(semaphore* sem);
semaphore* semaphore_get    (int id);
int        semaphore_wait   (int semid);
int        semaphore_signal (int semid);

/* Return 1 if @sem has threads blocked in semaphore_wait(), 0 otherwise.
 * Used by teardown paths (e.g. portal destroy) to decide whether freeing
 * the semaphore would strand its waiters. */
int        semaphore_has_waiters(semaphore* sem);

#endif
