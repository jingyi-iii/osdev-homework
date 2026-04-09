#ifndef __PROCESS_H_
#define __PROCESS_H_

#include <stdint.h>
#include "list.h"
#include "arch_process.h"
#include "spinlock.h"

#define MAX_NAME_LEN    (16)

enum proc_run_state {
    PS_NULL = 0,
    PS_READY_TO_RUN,
    PS_PENDING,
};

/* Process Control Block */
typedef struct pcb {
    proc_context    context;
    proc_entry_t    entry;
    int32_t         pid;
    list_node       pcb_node;
    spinlock*       sp_lock;
} pcb;

#endif
