#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "arch_thread.h"
#include "lib/list.h"
#include "sync/spinlock.h"

typedef void (*thread_entry_t)(void);

typedef enum thread_run_state {
    TS_NULL = 0,
    TS_READY,
    TS_PENDING,
} thread_state;

typedef struct proc_thread_ctrl_config {
    uint8_t cmd;
    int32_t pid;    // out param for create, in param for delete, block and unblock
    int32_t tid;    // out param for create, in param for delete, block and unblock
    thread_priv priv;
    thread_entry_t entry;
} proc_thread_ctrl_config;

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

void thread_exit(int32_t tid);
void thread_yield(void);
void thread_block(int32_t tid);
void thread_unblock(int32_t tid);

int p_create(proc_priv priv, thread_entry_t main_thread_entry);
void p_exit(int32_t pid);
int p_block(int32_t pid);
int p_unblock(int32_t pid);

void proc_create(proc_priv priv, thread_entry_t entry);
void proc_exit(int32_t pid);
int proc_block(int32_t pid);
int proc_unblock(int32_t pid);

#endif
