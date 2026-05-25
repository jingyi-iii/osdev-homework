#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "lib/list.h"
#include "sync/spinlock.h"
#include "kernel/thread.h"

typedef enum proc_priv {
    PROC_PRIV_KERNEL = 0,
    PROC_PRIV_USER,
} proc_priv;

typedef enum proc_state {
    PS_NULL = 0,
    PS_READY,
    PS_PENDING,
    PS_ZOMBIE,
} proc_state;

/* Process Control Block */
typedef struct pcb {
    int32_t             pid;
    proc_state          state;
    proc_priv           priv;
    list_node           this_node;
    list_node           tcbs;
    spinlock*           sp_lock;
} pcb;

#endif
