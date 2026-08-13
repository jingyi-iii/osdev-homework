#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include "arch_task.h"
#include "lib/list.h"
#include "sync/spinlock.h"
#include "kernel/errno.h"
#include "mm/vmm.h"
#include "kernel/capability.h"
#include "sync/wait_queue.h"
#include "sync/semaphore.h"

#define PROCESS_SUPPORT_MAILBOX

typedef enum thread_run_state {
    TS_NULL = 0,
    TS_READY,
    TS_PENDING,
} thread_state;

typedef struct proc_thread_ctrl_config {
    uint8_t cmd;
    int32_t pid;    // out param for create, in param for delete, block and unblock
    int32_t tid;    // out param for create, in param for delete, block and unblock
    task_priv priv;
    task_entry_t entry;
    void* param;
} proc_thread_ctrl_config;

typedef enum proc_priv {
    PROC_PRIV_KERNEL = 0,
    PROC_PRIV_USER   = 1,
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
    void*               param;
    spinlock*           sp_lock;

    vmm_control_block   vcb;            /* address space context for this process */
    list_node           capabilities;
    spinlock*           cap_lock;
} pcb;

/* Thread Control Block */
typedef struct tcb {
    arch_task_context   context;
    task_entry_t        entry;
    int32_t             tid;
    thread_state        state;
    int                 wake_pending;   /* set by unblock, consumed on block/resume */
    list_node           this_node;      /* node in global scheduling list */
    list_node           proc_node;      /* node in parent->tcbs list */
    spinlock*           sp_lock;
    struct pcb*         parent;
    void*               param;          /* per-thread parameter (entry reads via thread_get_param) */
    list_node           irqs;
#ifdef PROCESS_SUPPORT_MAILBOX
    struct mailbox*     mailbox;
#endif
    list_node           wait_node;
    wait_queue*         waiting_on;
} tcb;

int32_t thread_create       (task_priv priv, task_entry_t entry, void* param);
void    thread_exit         (int32_t tid);
void    thread_yield        (void);
void    thread_block        (int32_t tid);
void    thread_unblock      (int32_t tid);
int     thread_get_tid      (void);
void*   thread_get_param    (void);
tcb*    thread_get_by_tid   (int32_t tid);

int32_t proc_create         (proc_priv priv, task_entry_t entry, void* param);
void    proc_exit           (int32_t pid);
int     proc_block          (int32_t pid);
int     proc_unblock        (int32_t pid);
int     proc_get_pid        (void);
pcb*    get_current_process (void);
pcb*    get_process_by_pid  (int32_t pid);

/* Exported for mailbox broadcast — must be held when iterating thread_head */
extern list_node thread_head;
extern spinlock* schedule_lock;

#endif
