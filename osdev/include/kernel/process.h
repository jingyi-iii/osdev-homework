#ifndef __PROCESS_H_
#define __PROCESS_H_

#include <stdint.h>
#include "lib/list.h"
#include "arch_process.h"
#include "sync/spinlock.h"

typedef enum proc_run_state {
    PS_NULL = 0,
    PS_READY_TO_RUN,
    PS_PENDING,
} proc_state;

typedef struct proc_ctrl_config {
    uint8_t cmd;
    int32_t pid;    // out param for create, in param for delete, block and unblock
    proc_priv priv;
    proc_entry_t entry;
} proc_ctrl_config;

/* Process Control Block */
typedef struct pcb {
    arch_proc_context   context;
    proc_entry_t        entry;
    int32_t             pid;
    proc_state          state;
    list_node           pcb_node;
    spinlock*           sp_lock;
} pcb;

typedef void (*proc_entry_t)(void);

int32_t proc_create(proc_priv priv, proc_entry_t entry);
void proc_exit(int32_t pid);
void proc_yield(void);
void proc_block(int32_t pid);
void proc_unblock(int32_t pid);

void process_evn_setup(void);

#endif
