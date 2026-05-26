#ifndef __THREAD_H_
#define __THREAD_H_

#include <stdint.h>
#include "lib/list.h"
#include "arch_thread.h"
#include "sync/spinlock.h"
#include "kernel/process.h"

typedef enum thread_run_state {
    TS_NULL = 0,
    TS_READY,
    TS_PENDING,
} thread_state;

typedef struct thread_ctrl_config {
    uint8_t cmd;
    int32_t tid;    // out param for create, in param for delete, block and unblock
    thread_priv priv;
    thread_entry_t entry;
} thread_ctrl_config;

struct pcb;

/* Thread Control Block */
typedef struct tcb {
    arch_thread_context context;
    thread_entry_t      entry;
    int32_t             tid;
    thread_state        state;
    list_node           this_node;   /* node in global scheduling list */
    list_node           proc_node;   /* node in parent->tcbs list */
    spinlock*           sp_lock;
    struct pcb*         parent;
} tcb;

int32_t thread_create(thread_priv priv, thread_entry_t entry);
void thread_exit(int32_t tid);
void thread_yield(void);
void thread_block(int32_t tid);
void thread_unblock(int32_t tid);

void thread_evn_setup(void);

#endif
