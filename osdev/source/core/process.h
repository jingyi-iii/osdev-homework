#ifndef __PROCESS_H_
#define __PROCESS_H_

#include <stdint.h>
#include "list.h"
#include "arch_process.h"
#include "spinlock.h"

typedef enum proc_run_state {
    PS_NULL = 0,
    PS_READY_TO_RUN,
    PS_PENDING,
} proc_state;

/* Process Control Block */
typedef struct pcb {
    proc_context    context;
    proc_entry_t    entry;
    int32_t         pid;
    list_node       pcb_node;
    spinlock*       sp_lock;
} pcb;

typedef void (*proc_entry_t)(void);

int32_t create_proc(proc_priv priv, proc_entry_t entry);
void schedule(void);

#endif
