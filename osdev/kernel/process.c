#include "kernel/process.h"
#include "mm/heap.h"
#include "arch_protm.h"
#include "lib/module.h"
#include "lib/string.h"
#include "drivers/log_driver.h"
#include "kernel/irq.h"

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
static DECLARE_HEAD_NODE(thread_head);
static tcb *thread_run = 0;

static tcb* find_next_runnable(tcb* current)
{
    if (!current)
        return 0;

    /* search from current->next to end of list */
    for (list_node* pos = list_next(&current->this_node); pos != &thread_head; pos = pos->next) {
        tcb* t = list_entry(pos, tcb, this_node);
        if (t->state == TS_READY)
            return t;
    }

    /* wrap around: from list head to current */
    for (list_node* pos = thread_head.next; pos != &current->this_node; pos = pos->next) {
        tcb* t = list_entry(pos, tcb, this_node);
        if (t->state == TS_READY)
            return t;
    }

    return 0;
}

static int32_t t_create(pcb* parent, thread_priv priv, thread_entry_t entry)
{
    tcb* thread = 0;
    static uint32_t tid = 0;

    if (!parent) {
        KLOG("failed to create thread without parent process");
        return -1;
    }

    KLOG("adding thread, tid %d", tid);

    thread = (tcb*)kmalloc(sizeof(tcb));
    if (!thread) {
        KLOG("failed to alloc memory for tcb");
        return -1;
    }

    if (arch_thread_context_init(&thread->context, entry, priv)) {
        KLOG("failed to init thread context");
        kfree(thread);
        return -22;
    }

    list_init(&thread->this_node);
    list_init(&thread->proc_node);
    thread->sp_lock = spinlock_alloc();
    if (!thread->sp_lock) {
        KLOG("failed to alloc spin lock for tcb");
        arch_thread_context_release(&thread->context);
        kfree(thread);
        return -1;
    }

    thread->parent = parent;
    list_add(&thread->proc_node, &parent->tcbs);
    list_add(&thread->this_node, &thread_head);

    thread->tid = tid++;
    thread->state = TS_READY;

    if (!thread_run) {    // the first thread
        spinlock_lock(thread->sp_lock);
        thread_run = thread;
        arch_thread_restore_context(&thread->context);
        spinlock_unlock(thread->sp_lock);
    }

    KLOG("add thread, tid %d", thread->tid);

    return thread->tid;
}

static void t_delete(int32_t tid)
{
    if (!thread_run)
        return;

    /* find the target thread */
    tcb* target = 0;
    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (t->tid == tid) {
            target = t;
            break;
        }
    }

    if (!target)
        return;

    if (target != thread_run) {
        /* deleting a non-running thread */
        spinlock_lock(target->sp_lock);
        arch_thread_context_release(&target->context);
        spinlock_unlock(target->sp_lock);
        list_del(&target->this_node);
        list_del(&target->proc_node);
        spinlock_release(target->sp_lock);
        kfree(target);
    } else {
        /* deleting the running thread: switch to next runnable first */
        tcb* next = find_next_runnable(thread_run);
        if (!next) {
            KLOG("no more thread to run after deleting thread with tid %d", tid);
            return;
        }

        spinlock_lock(thread_run->sp_lock);
        arch_thread_context_release(&thread_run->context);
        list_del(&thread_run->this_node);
        list_del(&thread_run->proc_node);
        spinlock_unlock(thread_run->sp_lock);

        tcb* old = thread_run;
        thread_run = next;
        arch_thread_restore_context(&next->context);
        spinlock_release(old->sp_lock);
        kfree(old);
    }
}

static void t_block(int32_t tid)
{
    if (!thread_run)
        return;

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
                spinlock_lock(next->sp_lock);
                thread_run = next;
                arch_thread_restore_context(&next->context);
                spinlock_unlock(next->sp_lock);
            }
        }
        break;
    }
}

static void t_unblock(int32_t tid)
{
    if (!thread_run)
        return;

    list_for_each(node, &thread_head) {
        tcb* t = list_entry(node, tcb, this_node);
        if (!t || t->tid != tid)
            continue;

        spinlock_lock(t->sp_lock);
        t->state = TS_READY;
        spinlock_unlock(t->sp_lock);
        break;
    }
}

static void t_yield(void)
{
    if (!thread_run)
        return;

    tcb* next = find_next_runnable(thread_run);
    if (next) {
        spinlock_lock(next->sp_lock);
        thread_run = next;
        arch_thread_restore_context(&next->context);
        spinlock_unlock(next->sp_lock);
    }
}

static int p_create(proc_priv priv, thread_entry_t main_thread_entry)
{
    static uint32_t pid = 0;

    struct pcb* proc = (struct pcb*)kmalloc(sizeof(struct pcb));
    if (!proc) {
        KLOG("failed to alloc memory for pcb");
        return -1;
    }

    proc->sp_lock = spinlock_alloc();
    if (!proc->sp_lock) {
        KLOG("failed to alloc spin lock for pcb");
        kfree(proc);
        return -1;
    }

    proc->pid = pid++;
    proc->state = PS_READY;
    proc->priv = priv;
    list_init(&proc->this_node);
    list_init(&proc->tcbs);

    list_add(&proc->this_node, &proc_head);
    return t_create(proc, (thread_priv)priv, main_thread_entry);
}

static void p_exit(int32_t pid)
{
    list_for_each(node, &proc_head) {
        struct pcb* proc = list_entry(node, struct pcb, this_node);
        if (!proc || proc->pid != pid)
            continue;

        spinlock_lock(proc->sp_lock);
        while (!list_empty(&proc->tcbs)) {
            list_node* pos = proc->tcbs.next;
            struct tcb* thread = list_entry(pos, struct tcb, proc_node);
            t_delete(thread->tid);
        }
        spinlock_unlock(proc->sp_lock);

        list_del(&proc->this_node);
        spinlock_release(proc->sp_lock);
        kfree(proc);
        break;
    }
}

static int p_block(int32_t pid)
{
    list_for_each(node, &proc_head) {
        struct pcb* proc = list_entry(node, struct pcb, this_node);
        if (!proc || proc->pid != pid)
            continue;

        spinlock_lock(proc->sp_lock);
        proc->state = PS_PENDING;
        spinlock_unlock(proc->sp_lock);

        list_for_each(tcb_node, &proc->tcbs) {
            struct tcb* thread = list_entry(tcb_node, struct tcb, proc_node);
            if (!thread)
                continue;

            spinlock_lock(thread->sp_lock);
            thread->state = TS_PENDING;
            spinlock_unlock(thread->sp_lock);
        }
    }

    return 0;
}

static int p_unblock(int32_t pid)
{
    list_for_each(node, &proc_head) {
        struct pcb* proc = list_entry(node, struct pcb, this_node);
        if (!proc || proc->pid != pid)
            continue;

        spinlock_lock(proc->sp_lock);
        proc->state = PS_READY;
        spinlock_unlock(proc->sp_lock);

        list_for_each(tcb_node, &proc->tcbs) {
            struct tcb* thread = list_entry(tcb_node, struct tcb, proc_node);
            if (!thread)
                continue;

            spinlock_lock(thread->sp_lock);
            thread->state = TS_READY;
            spinlock_unlock(thread->sp_lock);
        }
    }

    return 0;
}

static void schedule_isr(void* p)
{
    (void)p;
    static uint32_t timeslice = 0;

    timeslice++;
    if (timeslice < 5 || !thread_run)
        return;
    timeslice = 0;

    tcb* next = find_next_runnable(thread_run);
    if (next) {
        spinlock_lock(next->sp_lock);
        thread_run = next;
        arch_thread_restore_context(&next->context);
        spinlock_unlock(next->sp_lock);
    }
}

static void syscall_isr(void* data)
{
    proc_thread_ctrl_config *config = (proc_thread_ctrl_config*)data;

    switch (config->cmd) {
    case THREAD_CTRL_CREATE:
        config->tid = t_create(thread_run->parent, config->priv, config->entry);
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
    tss_init();
    irq_request(&schedule_irq, "proc_tmr", TIMER_IRQ_NO, 0, schedule_isr, 0);
    if (schedule_irq)
        irq_unmask(schedule_irq);

    irq_request(&syscall_irq, "proc_syscall", 100, 0, syscall_isr, 0);
    if (syscall_irq)
        irq_unmask(syscall_irq);
}

static void proc_env_exit(void)
{
    if (schedule_irq) {
        irq_mask(schedule_irq);
        irq_release(schedule_irq);
    }

    if (syscall_irq) {
        irq_mask(syscall_irq);
        irq_release(syscall_irq);
    }
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

int32_t thread_create(thread_priv priv, thread_entry_t entry)
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

void proc_create(proc_priv priv, thread_entry_t entry)
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
    return thread_run ? thread_run->parent->pid : -1;
}

module_init(proc_env_init);
module_exit(proc_env_exit);
