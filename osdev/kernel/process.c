#include "kernel/process.h"
#include "mm/heap.h"
#include "mm/vmm.h"
#include "arch_protm.h"
#include "lib/module.h"
#include "lib/string.h"
#include "drivers/log_server.h"
#include "kernel/irq.h"
#include "kernel/syscall.h"
#include "kernel/uapi.h"
#include "ipc/mailbox.h"
#include "kernel/capability.h"
#include "lib/elf.h"

extern mailbox* alloc_mailbox(int owner_pid, int owner_tid);
extern void release_mailbox(mailbox* mb);

enum proc_thread_ctrl {
    THREAD_CTRL_CREATE = 0,
    THREAD_CTRL_DELETE,
    THREAD_CTRL_YIELD,
    THREAD_CTRL_BLOCK,
    THREAD_CTRL_UNBLOCK,
    PROC_CTRL_CREATE,
    PROC_CTRL_LOAD_FROM_ELF,
    PROC_CTRL_EXIT,
    PROC_CTRL_BLOCK,
    PROC_CTRL_UNBLOCK,
};

static DECLARE_HEAD_NODE(proc_head);
DECLARE_HEAD_NODE(thread_head);
static tcb *thread_run = 0;
spinlock* schedule_lock = 0;

/*
 * Whether a context-switching t_xxx/p_xxx call may execute directly.
 * True only for ring-0 code already inside a syscall/IRQ gate
 * (irq_reenter_cnt == 0): the gate entry saved the current thread's
 * context and the gate exit performs the actual switch.  At CPL3 or in
 * plain ring-0 context (irq_reenter_cnt == -1) the syscall gate must be
 * used — arch_task_restore_context() only re-points curr_task_ctx, the
 * switch completes on gate exit, and a direct call outside a gate would
 * leave the current thread's context unsaved (and corrupt the target's
 * saved frame on the next gate entry).
 */
static inline int may_run_direct(void)
{
    return !arch_running_ring3() && irq_reenter_cnt == 0;
}

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
            t->wake_pending = 0;   /* wakeup consumed: thread is about to run */
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
            t->wake_pending = 0;   /* wakeup consumed: thread is about to run */
            spinlock_unlock(t->sp_lock);
            return t;
        }
        spinlock_unlock(t->sp_lock);
    }

    return 0;
}

/*
 * tcb_detach_wait - remove @t from the wait queue it is sleeping on.
 * Must be called before a tcb is freed, otherwise a later
 * wait_queue_wake_* would dereference a freed node.  Callers hold
 * schedule_lock with interrupts disabled, so taking wq->sp_lock here
 * can never deadlock against a concurrent waker (single CPU, IF=0).
 */
static void tcb_detach_wait(tcb* t)
{
    wait_queue* wq;

    if (!t || !t->waiting_on)
        return;

    wq = t->waiting_on;
    spinlock_lock(wq->sp_lock);
    if (t->waiting_on == wq) {
        list_del(&t->wait_node);
        t->waiting_on = 0;
    }
    spinlock_unlock(wq->sp_lock);
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

static i32 t_create_ex(pcb* parent, task_priv priv, task_entry_t entry,
                       void* param, thread_state initial_state)
{
    tcb* thread = 0;
    static u32 tid = 0;

    if (!parent) {
        LOG("failed to create thread without parent process");
        return E_INVAL;
    }

    LOG("adding thread, tid %d", tid);

    thread = (tcb*)kmalloc(sizeof(tcb));
    if (!thread) {
        LOG("failed to alloc memory for tcb");
        return E_NOMEM;
    }

    thread->waiting_on = 0;
    list_init(&thread->wait_node);

    if (arch_task_context_init(&parent->vcb, &thread->context, entry, priv)) {
        LOG("failed to init thread context");
        kfree(thread);
        return E_THREAD_CREATE;
    }

    list_init(&thread->this_node);
    list_init(&thread->proc_node);
    list_init(&thread->irqs);
    thread->sp_lock = spinlock_alloc();
    if (!thread->sp_lock) {
        LOG("failed to alloc spin lock for tcb");
        arch_task_context_release(&parent->vcb, &thread->context);
        kfree(thread);
        return E_LIMIT;
    }

    thread->parent = parent;
    thread->param = param;
    thread->tid = tid++;
    thread->wake_pending = 0;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

    spinlock_lock(parent->sp_lock);
    list_add(&thread->proc_node, &parent->tcbs);
    spinlock_unlock(parent->sp_lock);

    list_add(&thread->this_node, &thread_head);
    thread->state = initial_state;

    if (!thread_run) {    // the first thread
        /* The boot thread is switched to directly and must be runnable,
         * even though the default creation state is now TS_PENDING. */
        thread->state = TS_READY;
        thread_run = thread;
        /*
         * The very first thread is switched to directly (no scheduler
         * round-trip), so its process's page directory must be loaded here.
         * Without this, a ring-3 first thread faults on its user stack: the
         * user stack is mapped only in the process's private page directory,
         * not in the kernel master PD, so on the kernel CR3 it is a
         * supervisor region that ring 3 cannot write -> #PF.
         */
        vmm_switch(&parent->vcb);
        arch_task_restore_context(&thread->context);
    }

    spinlock_unlock_irqrestore(schedule_lock, eflags);
    
#ifdef PROCESS_SUPPORT_MAILBOX
    thread->mailbox = alloc_mailbox(thread->parent->pid, thread->tid);
#endif

    LOG("add thread, tid %d", thread->tid);

    return thread->tid;
}

/*
 * t_create - create a thread in TS_PENDING (not scheduled).  The caller
 * must thread_unblock(tid) to let it run — explicit start semantics.
 */
static i32 t_create(pcb* parent, task_priv priv, task_entry_t entry, void* param)
{
    return t_create_ex(parent, priv, entry, param, TS_PENDING);
}

static void t_delete(i32 tid)
{
    if (!thread_run)
        return;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

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
        spinlock_unlock_irqrestore(schedule_lock, eflags);
        return;
    }

    if (target != thread_run) {
        /* deleting a non-running thread */
        spinlock_lock(target->sp_lock);
        arch_task_context_release(&target->parent->vcb, &target->context);
        spinlock_unlock(target->sp_lock);

        /* A blocked thread may still sit on a wait queue; detach it
         * before freeing so no waker can reach freed memory. */
        tcb_detach_wait(target);

        list_del(&target->this_node);

        /*
         * Keep schedule_lock held across proc_node removal so that
         * proc->tcbs is always modified under schedule_lock protection.
         * Lock ordering: schedule_lock -> parent->sp_lock.
         */
        spinlock_lock(target->parent->sp_lock);
        list_del(&target->proc_node);
        spinlock_unlock(target->parent->sp_lock);

        spinlock_unlock_irqrestore(schedule_lock, eflags);

        list_for_each_safe(n, next, &target->irqs) {
            irq* curr_irq = list_entry(n, irq, thread_node);
            list_del(n);
            irq_release(curr_irq);
        }

        spinlock_release(target->sp_lock);
#ifdef PROCESS_SUPPORT_MAILBOX
        release_mailbox(target->mailbox);
#endif
        kfree(target);
    } else {
        /* deleting the running thread: switch to next runnable first */
        tcb* next = find_next_runnable(thread_run);
        if (!next) {
            LOG("no more thread to run after deleting thread with tid %d", tid);
            spinlock_unlock_irqrestore(schedule_lock, eflags);
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
        arch_task_context_release(&old->parent->vcb, &old->context);
        tcb_detach_wait(old);
        list_del(&old->this_node);
        list_del(&old->proc_node);

        list_for_each_safe(n, next, &old->irqs) {
            irq* curr_irq = list_entry(n, irq, thread_node);
            list_del(n);
            irq_release(curr_irq);
        }

        spinlock_release(old->sp_lock);
#ifdef PROCESS_SUPPORT_MAILBOX
        release_mailbox(old->mailbox);
#endif
        kfree(old);

        spinlock_unlock_irqrestore(schedule_lock, eflags);
    }
}

static void t_block(i32 tid)
{
    if (!thread_run)
        return;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (!t || t->tid != tid)
            continue;

        spinlock_lock(t->sp_lock);
        if (t->wake_pending) {
            /* Already unblocked while still running: do not block. */
            t->wake_pending = 0;
            spinlock_unlock(t->sp_lock);
            break;
        }
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

    spinlock_unlock_irqrestore(schedule_lock, eflags);
}

static void t_unblock(i32 tid)
{
    if (!thread_run)
        return;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (!t || t->tid != tid)
            continue;

        spinlock_lock(t->sp_lock);
        t->wake_pending = 1;
        t->state = TS_READY;
        spinlock_unlock(t->sp_lock);
        break;
    }

    spinlock_unlock_irqrestore(schedule_lock, eflags);
}

static void t_yield(void)
{
    if (!thread_run)
        return;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

    tcb* next = find_next_runnable(thread_run);
    if (next) {
        tcb* old = thread_run;
        thread_run = next;

        /* Switch address space if we're moving to a different process */
        switch_address_space(old, next);

        arch_task_restore_context(&next->context);
    }

    spinlock_unlock_irqrestore(schedule_lock, eflags);
}

static int p_create_ex(proc_priv priv, task_entry_t main_thread_entry, void* param,
                       thread_state initial_state)
{
    static u32 pid = 0;
    int ret = 0;
    u32 eflags = 0;

    struct pcb* proc = (struct pcb*)kmalloc(sizeof(struct pcb));
    if (!proc) {
        LOG("failed to alloc memory for pcb");
        return E_NOMEM;
    }

    proc->sp_lock = spinlock_alloc();
    if (!proc->sp_lock) {
        LOG("failed to alloc spin lock for pcb");
        kfree(proc);
        return E_LIMIT;
    }
    proc->cap_lock = spinlock_alloc();
    if (!proc->cap_lock) {
        LOG("failed to alloc spin lock for pcb capabilities");
        spinlock_release(proc->sp_lock);
        kfree(proc);
        return E_LIMIT;
    }

    proc->pid = pid++;
    proc->state = PS_READY;
    proc->priv = priv;
    proc->param = param;

    /* Allocate a private page directory for user processes.
     * Kernel processes share the kernel's master page directory. */
    if (vmm_create(&proc->vcb, priv == PROC_PRIV_USER)) {
        LOG("failed to create address space for pid %d", proc->pid);
        spinlock_release(proc->cap_lock);
        spinlock_release(proc->sp_lock);
        kfree(proc);
        return E_NOMEM;
    }

    list_init(&proc->this_node);
    list_init(&proc->tcbs);
    list_init(&proc->capabilities);

    /* Inherit a copy of the parent process's capabilities so processes
     * spawned by a granted process (e.g. demo children) keep the I/O and
     * other grants they need.  No-op for the first kernel process. */
    pcb* parent_proc = thread_run ? thread_run->parent : 0;
    if (parent_proc && parent_proc != proc)
        cap_inherit_all(proc, parent_proc);

    eflags = spinlock_lock_irqsave(schedule_lock);
    list_add(&proc->this_node, &proc_head);
    spinlock_unlock_irqrestore(schedule_lock, eflags);

    ret = t_create_ex(proc, (task_priv)priv, main_thread_entry, proc->param,
                      initial_state);
    if (ret >= 0)
        return proc->pid;

    /* t_create failed: nothing was linked into proc->tcbs, so just roll
     * back the PCB (proc_head entry, address space, locks, memory). */
    eflags = spinlock_lock_irqsave(schedule_lock);
    list_del(&proc->this_node);
    spinlock_unlock_irqrestore(schedule_lock, eflags);
    vmm_destroy(&proc->vcb);
    spinlock_release(proc->cap_lock);
    spinlock_release(proc->sp_lock);
    kfree(proc);

    return ret;
}

/*
 * p_create - create a process whose main thread is born TS_PENDING.
 * The caller must proc_unblock(pid) to let it run — explicit start
 * semantics (the ELF loader maps the address space before unblocking).
 */
static int p_create(proc_priv priv, task_entry_t main_thread_entry, void* param)
{
    return p_create_ex(priv, main_thread_entry, param, TS_PENDING);
}

static void p_exit(i32 pid)
{
    struct pcb* found = 0;
    int self_in_proc = 0;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

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
                    LOG("no more thread to run during proc exit, pid %d", pid);
                    break;
                }

                tcb* old = thread_run;
                thread_run = next;

                /* Switch address space if needed */
                switch_address_space(old, next);

                arch_task_restore_context(&next->context);

                arch_task_context_release(&old->parent->vcb, &old->context);
                tcb_detach_wait(old);
                list_del(&old->this_node);
                list_del(&old->proc_node);
                spinlock_release(old->sp_lock);
                kfree(old);
            } else {
                spinlock_lock(thread->sp_lock);
                arch_task_context_release(&thread->parent->vcb, &thread->context);
                spinlock_unlock(thread->sp_lock);

                tcb_detach_wait(thread);
                list_del(&thread->this_node);
                list_del(&thread->proc_node);

                spinlock_release(thread->sp_lock);
                kfree(thread);
            }
        }

        list_del(&proc->this_node);
        break;
    }

    spinlock_unlock_irqrestore(schedule_lock, eflags);

    if (found) {
        cap_revoke_all(found);            /* free the process's capabilities */
        vmm_destroy(&found->vcb);
        spinlock_release(found->cap_lock);
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

static int p_block(i32 pid)
{
    u32 eflags = spinlock_lock_irqsave(schedule_lock);

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

    spinlock_unlock_irqrestore(schedule_lock, eflags);
    return 0;
}

static int p_unblock(i32 pid)
{
    u32 eflags = spinlock_lock_irqsave(schedule_lock);

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

    spinlock_unlock_irqrestore(schedule_lock, eflags);
    return 0;
}

int schedule_if_needed(void)
{
    tcb* next = find_next_runnable(thread_run);
    if (!next || next == thread_run)
        return E_NODEV;

    tcb* old = thread_run;
    thread_run = next;
    switch_address_space(old, next);
    arch_task_restore_context(&next->context);
    return 0;
}

/*
 * schedule_from_isr - scheduler kick for the threaded-irq gate exit.
 * Called from arch/i386/irq.S only when irq_defer_unmask is set (a
 * threaded irq was woken).  trylock: a ring-3 thread may hold
 * schedule_lock with interrupts unmasked (IOPL=0), and blocking here
 * would deadlock against the thread this ISR just preempted.
 * arch_task_restore_context() only re-points curr_task_ctx — the actual
 * switch happens at iret in irq.S — so this function always returns and
 * the lock is released normally.
 */
void schedule_from_isr(void)
{
    if (!thread_run)
        return;

    if (spinlock_trylock(schedule_lock) != 0)
        return;

    schedule_if_needed();
    spinlock_unlock(schedule_lock);
}

static void schedule_isr(void* p)
{
    (void)p;
    static u32 timeslice = 0;

    timeslice++;
    if (timeslice < 5)
        return;

    timeslice = 0;

    /*
     * Ring-3 threads run with IOPL=0 (see arch/i386/task.c), so a user
     * thread inside thread_get_by_tid()/get_process_by_pid() holds
     * schedule_lock WITHOUT interrupts masked.  A blocking acquisition
     * here could then deadlock: the ISR would spin forever on a lock whose
     * holder is exactly the thread this ISR preempted.  Trylock instead:
     * if the lock is busy, skip this scheduling tick — the holder releases
     * it promptly and the next tick preempts normally.  The ISR runs with
     * IF=0 (interrupt gate), so no EFLAGS save/restore is needed.
     */
    if (spinlock_trylock(schedule_lock) != 0)
        return;

    if (!thread_run) {
        spinlock_unlock(schedule_lock);
        return;
    }

    schedule_if_needed();
    spinlock_unlock(schedule_lock);
}

static int syscall_isr(void* data)
{
    proc_thread_ctrl_config *config = (proc_thread_ctrl_config*)data;
    tcb* cur;

    /*
     * Read thread_run into a local variable under schedule_lock to prevent
     * a race with schedule_isr on another CPU.
     */
    u32 eflags = spinlock_lock_irqsave(schedule_lock);
    cur = thread_run;
    spinlock_unlock_irqrestore(schedule_lock, eflags);

    switch (config->cmd) {
    case THREAD_CTRL_CREATE:
        if (cur)
            config->tid = t_create(cur->parent, config->priv, config->entry, config->param);
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
        config->pid = p_create((proc_priv)config->priv, config->entry, config->param);
        break;
    case PROC_CTRL_LOAD_FROM_ELF:
        config->pid = proc_load_from_elf(config->elf_start, config->elf_end, config->param);
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

    return 0;
}

static irq* schedule_irq = 0;
static i32 proc_scall_handle = -1;

static void proc_env_init(void)
{
    schedule_lock = spinlock_alloc();
    if (!schedule_lock) {
        LOG("failed to alloc spin lock for scheduler");
        return;
    }

    tss_init();
    irq_request(&schedule_irq, "proc_tmr", TIMER_IRQ_NO, 0, schedule_isr, 0);
    if (schedule_irq)
        irq_unmask(schedule_irq);

    proc_scall_handle = syscall_register(SYSCALL_PROC_THREAD, syscall_isr, sizeof(proc_thread_ctrl_config));

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

    syscall_unregister(proc_scall_handle);

#ifdef PROCESS_SUPPORT_MAILBOX
    mailbox_syscall_exit();
#endif
}

/*
 * Thread / process API.  The implementation is shared between user mode
 * and the kernel: CPL3 callers always trap through the syscall gate;
 * ring-0 callers run the t_xxx / p_xxx implementation directly only when
 * already inside a gate (see may_run_direct()).  Outside a gate the gate
 * must be used, because the actual context switch happens in the gate's
 * save/restore machinery.
 */
void thread_yield(void)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = THREAD_CTRL_YIELD;

        arch_syscall(proc_scall_handle, &config, sizeof(config));
        return;
    }

    /* Ring-0 inside a gate: yield directly; the gate exit performs the
     * switch to the next thread. */
    t_yield();
}

void thread_block(i32 tid)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = THREAD_CTRL_BLOCK;
        config.tid = tid;

        arch_syscall(proc_scall_handle, &config, sizeof(config));
        return;
    }

    /* Ring-0 inside a gate: block directly. */
    t_block(tid);
}

void thread_unblock(i32 tid)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = THREAD_CTRL_UNBLOCK;
        config.tid = tid;

        arch_syscall(proc_scall_handle, &config, sizeof(config));
        return;
    }

    /* Ring-0 inside a gate: unblock directly. */
    t_unblock(tid);
}

i32 thread_create(task_priv priv, task_entry_t entry, void* param)
{
    if (!may_run_direct()) {
        /*
         * A user (CPL3) process may only spawn kernel-privileged threads
         * if it has been granted the CAP_CREATE_KRNL_THREAD capability.
         * Creating plain user threads is always allowed.
         */
        if (arch_running_ring3() && priv == TASK_PRIV_KERNEL) {
            pcb* proc = get_current_process();
            if (!proc || cap_check(proc, CAP_CREATE_KRNL_THREAD, &(int){1}) != 0) {
                LOG("no create-kernel-thread capability for pid %d",
                    proc ? proc->pid : -1);
                return E_PERM;
            }
        }

        proc_thread_ctrl_config config = {0};
        config.cmd = THREAD_CTRL_CREATE;
        config.priv = priv;
        config.entry = entry;
        config.param = param;

        arch_syscall(proc_scall_handle, &config, sizeof(config));

        return config.tid;
    }

    /* Ring-0 inside a gate: create directly. */
    tcb* cur = thread_run;
    return cur ? t_create(cur->parent, priv, entry, param) : E_INVAL;
}

void thread_exit(i32 tid)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = THREAD_CTRL_DELETE;
        config.tid = tid;

        arch_syscall(proc_scall_handle, &config, sizeof(config));
        return;
    }

    /* Ring-0 inside a gate: delete directly. */
    t_delete(tid);
}

int thread_get_tid(void)
{
    tcb* cur = thread_run;
    return cur ? cur->tid : -1;
}

void* thread_get_param(void)
{
    tcb* cur = thread_run;
    return cur ? cur->param : 0;
}

tcb* thread_get_by_tid(i32 tid)
{
    tcb* target = 0;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (t->tid == tid) {
            target = t;
            break;
        }
    }

    spinlock_unlock_irqrestore(schedule_lock, eflags);
    return target;
}

i32 proc_create(proc_priv priv, task_entry_t entry, void* param)
{
    if (!may_run_direct()) {
        /*
         * A user (CPL3) process may only spawn kernel-privileged processes
         * if it has been granted the CAP_CREATE_KRNL_PROC capability.
         * Creating plain user processes is always allowed.
         */
        if (arch_running_ring3() && priv == PROC_PRIV_KERNEL) {
            pcb* proc = get_current_process();
            if (!proc || cap_check(proc, CAP_CREATE_KRNL_PROC, &(int){1}) != 0) {
                LOG("no create-kernel-proc capability for pid %d",
                    proc ? proc->pid : -1);
                return E_PERM;
            }
        }

        proc_thread_ctrl_config config = {0};
        config.cmd = PROC_CTRL_CREATE;
        config.priv = (task_priv)priv;
        config.entry = entry;
        config.param = param;
        arch_syscall(proc_scall_handle, &config, sizeof(config));

        return config.pid;
    }

    /* Ring-0 inside a gate: create directly. */
    return p_create(priv, entry, param);
}

i32 proc_load_from_elf(u8* elf_start, u8* elf_end, void* param)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = PROC_CTRL_LOAD_FROM_ELF;
        config.priv = (task_priv)PROC_PRIV_USER;
        config.elf_start = elf_start;
        config.elf_end = elf_end;
        arch_syscall(proc_scall_handle, &config, sizeof(config));

        return config.pid;
    }

    /*
     * Sanity-check the image range FIRST.  elf_end <= elf_start would
     * make (u32)(elf_end - elf_start) wrap to 0 or a huge value, which
     * would bypass the bounds checks inside elf_validate()/elf_load().
     */
    if (!elf_start || elf_end <= elf_start)
        return E_INVAL;

    u32 image_size = (u32)(elf_end - elf_start);
    if (image_size < sizeof(elf32_ehdr)) {
        LOG("user elf: image too small (%u)", image_size);
        return E_INVAL;
    }

    /*
     * The image may live in user memory (CPL3 caller through the gate)
     * or in the kernel's own .data (ring-0 caller with the embedded ELF).
     * copy_from_user() validates the whole range against the CURRENT
     * page table (CR3 = caller's while inside the gate) and copies it
     * atomically into a kernel buffer, so elf_validate()/elf_load() never
     * dereference an untrusted or unmapped pointer.  The embedded kernel
     * image (low identity map, PTE_USER) passes the same range check.
     */
    u8* image = kmalloc(image_size);
    if (!image)
        return E_NOMEM;

    if (copy_from_user(image, elf_start, image_size) != 0) {
        LOG("user elf: image not readable");
        kfree(image);
        return E_FAULT;
    }

    int ret = elf_validate((const elf32_ehdr*)image, image_size);
    if (ret) {
        LOG("user elf: validation failed (%d)", ret);
        kfree(image);
        return ret;
    }

    u32 entry = ((const elf32_ehdr*)image)->entry;
    if (entry >= USER_SPACE_TOP) {
        LOG("user elf: bad entry 0x%x", entry);
        kfree(image);
        return E_INVAL;
    }

    i32 pid = proc_create(PROC_PRIV_USER, (task_entry_t)(uptr)entry, param);
    if (pid < 0) {
        LOG("user elf: proc_create failed (%d)", pid);
        kfree(image);
        return pid;
    }

    pcb* proc = get_process_by_pid(pid);
    if (!proc) {
        kfree(image);
        proc_exit(pid);
        return E_NOTFOUND;
    }

    ret = elf_load(proc, image, image_size, &entry);
    kfree(image);
    if (ret) {
        LOG("user elf: load failed (%d)", ret);
        proc_exit(pid);
        return ret;
    }

    return pid;
}

void proc_exit(i32 pid)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = PROC_CTRL_EXIT;
        config.pid = pid;

        arch_syscall(proc_scall_handle, &config, sizeof(config));
        return;
    }

    /* Ring-0 inside a gate: exit directly. */
    p_exit(pid);
}

int proc_block(i32 pid)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = PROC_CTRL_BLOCK;
        config.pid = pid;

        arch_syscall(proc_scall_handle, &config, sizeof(config));

        return 0;
    }

    /* Ring-0 inside a gate: block directly. */
    return p_block(pid);
}

int proc_unblock(i32 pid)
{
    if (!may_run_direct()) {
        proc_thread_ctrl_config config = {0};
        config.cmd = PROC_CTRL_UNBLOCK;
        config.pid = pid;

        arch_syscall(proc_scall_handle, &config, sizeof(config));

        return 0;
    }

    /* Ring-0 inside a gate: unblock directly. */
    return p_unblock(pid);
}

int proc_get_pid(void)
{
    tcb* cur = thread_run;
    return cur ? cur->parent->pid : -1;
}

pcb* get_current_process(void)
{
    tcb* cur = thread_run;
    return cur ? cur->parent : 0;
}

pcb* get_process_by_pid(i32 pid)
{
    pcb* target = 0;

    u32 eflags = spinlock_lock_irqsave(schedule_lock);

    list_for_each(node, &proc_head) {
        pcb* p = list_entry(node, pcb, this_node);
        if (p->pid == pid) {
            target = p;
            break;
        }
    }

    spinlock_unlock_irqrestore(schedule_lock, eflags);
    return target;
}

module_init(proc_env_init);
module_exit(proc_env_exit);
