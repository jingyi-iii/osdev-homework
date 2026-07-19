#include "kernel/process.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "arch_protm.h"
#include "lib/module.h"
#include "lib/string.h"
#include "drivers/log_driver.h"
#include "kernel/irq.h"
#include "kernel/mailbox.h"

extern mailbox* alloc_mailbox(int owner_pid, int owner_tid);
extern void release_mailbox(mailbox* mb);

enum proc_thread_ctrl {
    THREAD_CTRL_CREATE = 0,
    THREAD_CTRL_DELETE,
    THREAD_CTRL_YIELD,
    THREAD_CTRL_BLOCK,
    THREAD_CTRL_UNBLOCK,
    PROC_CTRL_CREATE,
    PROC_CTRL_EXIT,
    PROC_CTRL_BLOCK,
    PROC_CTRL_UNBLOCK,
};

static DECLARE_HEAD_NODE(proc_head);
DECLARE_HEAD_NODE(thread_head);
static tcb *thread_run = 0;
spinlock* schedule_lock = 0;

/*
 * find_next_runnable - find the next runnable thread starting from @current.
 * Caller MUST hold schedule_lock.
 * Returns the next TS_READY thread, or NULL if none found.
 */
static tcb* find_next_runnable(tcb* current)
{
    if (!current)
        return 0;

    /* search from current->next to end of list */
    for (list_node* pos = list_next(&current->this_node); pos != &thread_head; pos = pos->next) {
        tcb* t = list_entry(pos, tcb, this_node);
        spinlock_lock(t->sp_lock);
        if (t->state == TS_READY) {
            spinlock_unlock(t->sp_lock);
            return t;
        }
        spinlock_unlock(t->sp_lock);
    }

    /* wrap around: from list head to current */
    for (list_node* pos = thread_head.next; pos != &current->this_node; pos = pos->next) {
        tcb* t = list_entry(pos, tcb, this_node);
        spinlock_lock(t->sp_lock);
        if (t->state == TS_READY) {
            spinlock_unlock(t->sp_lock);
            return t;
        }
        spinlock_unlock(t->sp_lock);
    }

    return 0;
}

/*
 * switch_address_space - Load the page directory of @next if it differs
 * from the currently active one.  Must be called with schedule_lock held.
 */
static void switch_address_space(tcb* old, tcb* next)
{
    if (!old || !next)
        return;
    if (old->parent == next->parent)
        return;  /* same process, no CR3 switch needed */

    vmm_switch(&next->parent->vcb);
}

static int32_t t_create(pcb* parent, task_priv priv, task_entry_t entry)
{
    tcb* thread = 0;
    static uint32_t tid = 0;

    if (!parent) {
        KLOG("failed to create thread without parent process");
        return E_INVAL;
    }

    KLOG("adding thread, tid %d", tid);

    thread = (tcb*)kmalloc(sizeof(tcb));
    if (!thread) {
        KLOG("failed to alloc memory for tcb");
        return E_NOMEM;
    }

    if (arch_task_context_init(&parent->vcb, &thread->context, entry, priv)) {
        KLOG("failed to init thread context");
        kfree(thread);
        return E_THREAD_CREATE;
    }

    list_init(&thread->this_node);
    list_init(&thread->proc_node);
    thread->sp_lock = spinlock_alloc();
    if (!thread->sp_lock) {
        KLOG("failed to alloc spin lock for tcb");
        arch_task_context_release(&thread->context);
        kfree(thread);
        return E_LIMIT;
    }

    thread->parent = parent;
    thread->tid = tid++;

    spinlock_lock(schedule_lock);

    spinlock_lock(parent->sp_lock);
    list_add(&thread->proc_node, &parent->tcbs);
    spinlock_unlock(parent->sp_lock);

    list_add(&thread->this_node, &thread_head);
    thread->state = TS_READY;

    if (!thread_run) {    // the first thread
        thread_run = thread;
        arch_task_restore_context(&thread->context);
    }

    spinlock_unlock(schedule_lock);
    
#ifdef PROCESS_SUPPORT_MAILBOX
    thread->mailbox = alloc_mailbox(thread->parent->pid, thread->tid);
#endif

    KLOG("add thread, tid %d", thread->tid);

    return thread->tid;
}

static void t_delete(int32_t tid)
{
    if (!thread_run)
        return;

    spinlock_lock(schedule_lock);

    /* find the target thread */
    tcb* target = 0;
    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (t->tid == tid) {
            target = t;
            break;
        }
    }

    if (!target) {
        spinlock_unlock(schedule_lock);
        return;
    }

    if (target != thread_run) {
        /* deleting a non-running thread */
        spinlock_lock(target->sp_lock);
        arch_task_context_release(&target->context);
        spinlock_unlock(target->sp_lock);

        list_del(&target->this_node);

        /*
         * Keep schedule_lock held across proc_node removal so that
         * proc->tcbs is always modified under schedule_lock protection.
         * Lock ordering: schedule_lock -> parent->sp_lock.
         */
        spinlock_lock(target->parent->sp_lock);
        list_del(&target->proc_node);
        spinlock_unlock(target->parent->sp_lock);

        spinlock_unlock(schedule_lock);

        spinlock_release(target->sp_lock);
#ifdef PROCESS_SUPPORT_MAILBOX
        release_mailbox(target->mailbox);
#endif
        kfree(target);
    } else {
        /* deleting the running thread: switch to next runnable first */
        tcb* next = find_next_runnable(thread_run);
        if (!next) {
            KLOG("no more thread to run after deleting thread with tid %d", tid);
            spinlock_unlock(schedule_lock);
            return;
        }

        tcb* old = thread_run;
        thread_run = next;

        /*
         * Switch address space if we're moving to a different process.
         * Must be done before arch_task_restore_context so the new
         * page tables are active when iret returns to the new context.
         */
        switch_address_space(old, next);

        /*
         * Switch curr_task_ctx to the next thread BEFORE freeing the old
         * thread's stack.  This closes the window where curr_task_ctx pointed
         * to freed memory in case a nested exception fires.
         */
        arch_task_restore_context(&next->context);

        /* Now safe to release the old thread's resources */
        arch_task_context_release(&old->context);
        list_del(&old->this_node);
        list_del(&old->proc_node);

        spinlock_release(old->sp_lock);
#ifdef PROCESS_SUPPORT_MAILBOX
        release_mailbox(old->mailbox);
#endif
        kfree(old);

        spinlock_unlock(schedule_lock);
    }
}

static void t_block(int32_t tid)
{
    if (!thread_run)
        return;

    spinlock_lock(schedule_lock);

    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (!t || t->tid != tid)
            continue;

        spinlock_lock(t->sp_lock);
        t->state = TS_PENDING;
        spinlock_unlock(t->sp_lock);

        if (t == thread_run) {
            tcb* next = find_next_runnable(thread_run);
            if (next) {
                tcb* old = thread_run;
                thread_run = next;

                /* Switch address space if we're moving to a different process */
                switch_address_space(old, next);

                arch_task_restore_context(&next->context);
            }
        }
        break;
    }

    spinlock_unlock(schedule_lock);
}

static void t_unblock(int32_t tid)
{
    if (!thread_run)
        return;

    spinlock_lock(schedule_lock);

    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (!t || t->tid != tid)
            continue;

        spinlock_lock(t->sp_lock);
        t->state = TS_READY;
        spinlock_unlock(t->sp_lock);
        break;
    }

    spinlock_unlock(schedule_lock);
}

static void t_yield(void)
{
    if (!thread_run)
        return;

    spinlock_lock(schedule_lock);

    tcb* next = find_next_runnable(thread_run);
    if (next) {
        tcb* old = thread_run;
        thread_run = next;

        /* Switch address space if we're moving to a different process */
        switch_address_space(old, next);

        arch_task_restore_context(&next->context);
    }

    spinlock_unlock(schedule_lock);
}

static int p_create(proc_priv priv, task_entry_t main_thread_entry)
{
    static uint32_t pid = 0;

    struct pcb* proc = (struct pcb*)kmalloc(sizeof(struct pcb));
    if (!proc) {
        KLOG("failed to alloc memory for pcb");
        return E_NOMEM;
    }

    proc->sp_lock = spinlock_alloc();
    if (!proc->sp_lock) {
        KLOG("failed to alloc spin lock for pcb");
        kfree(proc);
        return E_LIMIT;
    }

    proc->pid = pid++;
    proc->state = PS_READY;
    proc->priv = priv;

    /* Allocate a private page directory for user processes.
     * Kernel processes share the kernel's master page directory. */
    if (vmm_create(&proc->vcb, priv == PROC_PRIV_USER)) {
        KLOG("failed to create address space for pid %d", proc->pid);
        spinlock_release(proc->sp_lock);
        kfree(proc);
        return E_NOMEM;
    }

    list_init(&proc->this_node);
    list_init(&proc->tcbs);

    spinlock_lock(schedule_lock);
    list_add(&proc->this_node, &proc_head);
    spinlock_unlock(schedule_lock);

    return t_create(proc, (task_priv)priv, main_thread_entry);
}

static void p_exit(int32_t pid)
{
    struct pcb* found = 0;
    int self_in_proc = 0;

    spinlock_lock(schedule_lock);

    list_for_each(node, &proc_head) {
        struct pcb* proc = list_entry(node, struct pcb, this_node);
        if (!proc || proc->pid != pid)
            continue;

        found = proc;

        /* Check if the calling thread belongs to this process */
        if (thread_run && thread_run->parent == proc)
            self_in_proc = 1;

        /*
         * Delete all threads belonging to this process.
         * schedule_lock protects both thread_head and proc->tcbs
         * (t_create and t_delete also hold schedule_lock when
         * modifying proc->tcbs).  t->sp_lock is acquired only for
         * context release; proc->sp_lock is not needed here because
         * schedule_lock already serializes proc->tcbs accesses.
         */
        while (!list_empty(&proc->tcbs)) {
            list_node* pos = proc->tcbs.next;
            struct tcb* thread = list_entry(pos, struct tcb, proc_node);

            if (thread == thread_run) {
                /*
                 * Deleting the currently running thread:
                 * must switch to another thread first.
                 */
                tcb* next = find_next_runnable(thread_run);
                if (!next) {
                    KLOG("no more thread to run during proc exit, pid %d", pid);
                    break;
                }

                tcb* old = thread_run;
                thread_run = next;

                /* Switch address space if needed */
                switch_address_space(old, next);

                arch_task_restore_context(&next->context);

                arch_task_context_release(&old->context);
                list_del(&old->this_node);
                list_del(&old->proc_node);
                spinlock_release(old->sp_lock);
                kfree(old);
            } else {
                spinlock_lock(thread->sp_lock);
                arch_task_context_release(&thread->context);
                spinlock_unlock(thread->sp_lock);

                list_del(&thread->this_node);
                list_del(&thread->proc_node);

                spinlock_release(thread->sp_lock);
                kfree(thread);
            }
        }

        list_del(&proc->this_node);
        break;
    }

    spinlock_unlock(schedule_lock);

    if (found) {
        vmm_destroy(&found->vcb);
        spinlock_release(found->sp_lock);
        kfree(found);
    }

    /*
     * If we just deleted our own thread, it will never return here —
     * arch_task_restore_context switched to the next thread.
     * If we reach this point, we were not deleting our own thread,
     * or we already switched away and this code is unreachable.
     */
    (void)self_in_proc;
}

static int p_block(int32_t pid)
{
    spinlock_lock(schedule_lock);

    list_for_each(node, &proc_head) {
        struct pcb* proc = list_entry(node, struct pcb, this_node);
        if (!proc || proc->pid != pid)
            continue;

        spinlock_lock(proc->sp_lock);
        proc->state = PS_PENDING;
        spinlock_unlock(proc->sp_lock);

        /*
         * Iterate proc->tcbs under schedule_lock protection.
         * Only t->sp_lock is acquired per thread (never nested with
         * proc->sp_lock) to avoid ABBA deadlock with t_delete.
         */
        list_for_each(tcb_node, &proc->tcbs) {
            struct tcb* thread = list_entry(tcb_node, struct tcb, proc_node);
            if (!thread)
                continue;

            spinlock_lock(thread->sp_lock);
            thread->state = TS_PENDING;
            spinlock_unlock(thread->sp_lock);
        }

        break;
    }

    spinlock_unlock(schedule_lock);
    return 0;
}

static int p_unblock(int32_t pid)
{
    spinlock_lock(schedule_lock);

    list_for_each(node, &proc_head) {
        struct pcb* proc = list_entry(node, struct pcb, this_node);
        if (!proc || proc->pid != pid)
            continue;

        spinlock_lock(proc->sp_lock);
        proc->state = PS_READY;
        spinlock_unlock(proc->sp_lock);

        /*
         * Iterate proc->tcbs under schedule_lock protection.
         * Only t->sp_lock is acquired per thread (never nested with
         * proc->sp_lock) to avoid ABBA deadlock with t_delete.
         */
        list_for_each(tcb_node, &proc->tcbs) {
            struct tcb* thread = list_entry(tcb_node, struct tcb, proc_node);
            if (!thread)
                continue;

            spinlock_lock(thread->sp_lock);
            thread->state = TS_READY;
            spinlock_unlock(thread->sp_lock);
        }

        break;
    }

    spinlock_unlock(schedule_lock);
    return 0;
}

static void schedule_isr(void* p)
{
    (void)p;
    static uint32_t timeslice = 0;

    timeslice++;
    if (timeslice < 5)
        return;

    spinlock_lock(schedule_lock);

    if (!thread_run) {
        spinlock_unlock(schedule_lock);
        return;
    }

    timeslice = 0;

    tcb* next = find_next_runnable(thread_run);
    if (next) {
        tcb* old = thread_run;
        thread_run = next;

        /* Switch address space if we're moving to a different process */
        switch_address_space(old, next);

        arch_task_restore_context(&next->context);
    }

    spinlock_unlock(schedule_lock);
}

static void syscall_isr(void* data)
{
    proc_thread_ctrl_config *config = (proc_thread_ctrl_config*)data;
    tcb* cur;

    /*
     * Read thread_run into a local variable under schedule_lock to prevent
     * a race with schedule_isr on another CPU.
     */
    spinlock_lock(schedule_lock);
    cur = thread_run;
    spinlock_unlock(schedule_lock);

    switch (config->cmd) {
    case THREAD_CTRL_CREATE:
        if (cur)
            config->tid = t_create(cur->parent, config->priv, config->entry);
        break;
    case THREAD_CTRL_DELETE:
        t_delete(config->tid);
        break;
    case THREAD_CTRL_YIELD:
        t_yield();
        break;
    case THREAD_CTRL_BLOCK:
        t_block(config->tid);
        break;
    case THREAD_CTRL_UNBLOCK:
        t_unblock(config->tid);
        break;
    case PROC_CTRL_CREATE:
        config->pid = p_create((proc_priv)config->priv, config->entry);
        break;
    case PROC_CTRL_EXIT:
        p_exit(config->pid);
        break;
    case PROC_CTRL_BLOCK:
        p_block(config->pid);
        break;
    case PROC_CTRL_UNBLOCK:
        p_unblock(config->pid);
        break;
    default:
        break;
    }
}

static irq* schedule_irq = 0;
static irq* syscall_irq = 0;

static void proc_env_init(void)
{
    schedule_lock = spinlock_alloc();
    if (!schedule_lock) {
        KLOG("failed to alloc spin lock for scheduler");
        return;
    }

    tss_init();
    irq_request(&schedule_irq, "proc_tmr", TIMER_IRQ_NO, 0, schedule_isr, 0);
    if (schedule_irq)
        irq_unmask(schedule_irq);

    irq_request(&syscall_irq, "proc_syscall", 100, 0, syscall_isr, 0);
    if (syscall_irq)
        irq_unmask(syscall_irq);

#ifdef PROCESS_SUPPORT_MAILBOX
    mailbox_syscall_init();
#endif
}

static void proc_env_exit(void)
{
    if (schedule_lock)
        spinlock_release(schedule_lock);

    if (schedule_irq) {
        irq_mask(schedule_irq);
        irq_release(schedule_irq);
    }

    if (syscall_irq) {
        irq_mask(syscall_irq);
        irq_release(syscall_irq);
    }

#ifdef PROCESS_SUPPORT_MAILBOX
    mailbox_syscall_exit();
#endif
}

/* Syscall Interfaces */
void thread_yield(void)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_YIELD;

    arch_syscall(0, &config);
}

void thread_block(int32_t tid)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_BLOCK;
    config.tid = tid;

    arch_syscall(0, &config);  
}

void thread_unblock(int32_t tid)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_UNBLOCK;
    config.tid = tid;

    arch_syscall(0, &config);
}

int32_t thread_create(task_priv priv, task_entry_t entry)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_CREATE;
    config.priv = priv;
    config.entry = entry;

    arch_syscall(0, &config);

    return config.tid;
}

void thread_exit(int32_t tid)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_DELETE;
    config.tid = tid;

    arch_syscall(0, &config);
}

int thread_get_tid(void)
{
    tcb* cur = thread_run;
    return cur ? cur->tid : -1;
}

tcb* thread_get_by_tid(int32_t tid)
{
    tcb* target = 0;

    spinlock_lock(schedule_lock);

    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (t->tid == tid) {
            target = t;
            break;
        }
    }

    spinlock_unlock(schedule_lock);
    return target;
}

void proc_create(proc_priv priv, task_entry_t entry)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = PROC_CTRL_CREATE;
    config.priv = priv;
    config.entry = entry;

    arch_syscall(0, &config);
}

void proc_exit(int32_t pid)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = PROC_CTRL_EXIT;
    config.pid = pid;

    arch_syscall(0, &config);
}

int proc_block(int32_t pid)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = PROC_CTRL_BLOCK;
    config.pid = pid;

    arch_syscall(0, &config);

    return 0;
}

int proc_unblock(int32_t pid)
{
    proc_thread_ctrl_config config = {0};
    config.cmd = PROC_CTRL_UNBLOCK;
    config.pid = pid;

    arch_syscall(0, &config);

    return 0;
}

int proc_get_pid(void)
{
    tcb* cur = thread_run;
    return cur ? cur->parent->pid : -1;
}

module_init(proc_env_init);
module_exit(proc_env_exit);
