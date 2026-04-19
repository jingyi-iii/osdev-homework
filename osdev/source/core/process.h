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

typedef struct proc_ctrl_config {
    uint8_t cmd;
    int32_t pid;
} proc_ctrl_config;

/* Process Control Block */
typedef struct pcb {
    proc_context    context;
    proc_entry_t    entry;
    int32_t         pid;
    proc_state      state;
    list_node       pcb_node;
    spinlock*       sp_lock;
} pcb;

typedef void (*proc_entry_t)(void);

int32_t create_proc(proc_priv priv, proc_entry_t entry);
void schedule(void);
void block(int32_t pid);
void unblock(int32_t pid);

#endif
