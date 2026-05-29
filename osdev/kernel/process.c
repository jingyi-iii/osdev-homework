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

static tcb *thread_run = 0;
static struct pcb *proc_head = 0;

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

    if (!thread_run) {    // the first thread
        spinlock_lock(thread->sp_lock);
        thread_run = thread;
        arch_thread_restore_context(&thread_run->context);
        spinlock_unlock(thread->sp_lock);
    } else {
        spinlock_lock(thread_run->sp_lock);
        list_add(&thread->this_node, &thread_run->this_node);
        spinlock_unlock(thread_run->sp_lock);
    }

    thread->tid = tid++;
    thread->state = TS_READY;

    KLOG("add thread, tid %d", thread->tid);

    return thread->tid;
}

static void t_delete(int32_t tid)
{
    tcb* thread = 0;

    if (!thread_run)
        return;
    
    if (thread_run->tid != tid) {
        list_for_each(node, &thread_run->this_node) {
            tcb* thread = list_entry(node, tcb, this_node);
            if (!thread)
                continue;

            if (thread->tid == tid) {
                spinlock_lock(thread->sp_lock);
                arch_thread_context_release(&thread->context);
                spinlock_unlock(thread->sp_lock);
                list_del(&thread->this_node);
                list_del(&thread->proc_node);
                spinlock_release(thread->sp_lock);
                kfree(thread);
                break;
            }
        }
    } else {
        thread = list_entry(list_next(&thread_run->this_node), tcb, this_node);
        if (thread == thread_run) {
            KLOG("no more thread to run after deleting thread with tid %d", tid);
            return;
        }

        spinlock_lock(thread_run->sp_lock);
        arch_thread_context_release(&thread_run->context);
        list_del(&thread_run->this_node);
        list_del(&thread_run->proc_node);
        spinlock_unlock(thread_run->sp_lock);

        tcb* old = thread_run;
        thread_run = thread;
        arch_thread_restore_context(&thread->context);
        spinlock_release(old->sp_lock);
        kfree(old);
        return;
    }
}

static void t_block(int32_t tid)
{
    if (!thread_run)
        return;

    list_for_each(node, &thread_run->this_node) {
        tcb* t = list_entry(node, tcb, this_node);
        if (!t || t->tid != tid)
            continue;

        spinlock_lock(t->sp_lock);
        t->state = TS_PENDING;
        spinlock_unlock(t->sp_lock);

        if (t == thread_run) {
            tcb* next = thread_run;
            do {
                next = list_entry(list_next(&next->this_node), tcb, this_node);
            } while (next != thread_run && next->state != TS_READY);

            if (next != thread_run && next->state == TS_READY) {
                spinlock_lock(next->sp_lock);
                arch_thread_restore_context(&next->context);
                spinlock_unlock(next->sp_lock);
                thread_run = next;
            }
        }
        break;
        spinlock_unlock(t->sp_lock);
    }
}

static void t_unblock(int32_t tid)
{
    if (!thread_run)
        return;

    list_for_each(node, &thread_run->this_node) {
        tcb* t = list_entry(node, tcb, this_node);
        if (!t || t->tid != tid)
            continue;

        spinlock_lock(t->sp_lock);
        if (t->tid == tid) {
            t->state = TS_READY;
            spinlock_unlock(t->sp_lock);
            break;
        }
        spinlock_unlock(t->sp_lock);
    }
}

static void t_yield(void)
{
    if (!thread_run)
        return;

    tcb* next = thread_run;
    do {
        next = list_entry(list_next(&next->this_node), tcb, this_node);
    } while (next != thread_run && next->state != TS_READY);

    if (next != thread_run && next->state == TS_READY) {
        spinlock_lock(next->sp_lock);
        arch_thread_restore_context(&next->context);
        spinlock_unlock(next->sp_lock);
        thread_run = next;
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

    if (!proc_head) {
        proc_head = proc;
    } else {
        list_add(&proc->this_node, &proc_head->this_node);
    }

    return t_create(proc, (thread_priv)priv, main_thread_entry);
}

static void p_exit(int32_t pid)
{
    if (!proc_head)
        return;

    list_for_each(node, &proc_head->this_node) {
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
    if (!proc_head)
        return -1;

    list_for_each(node, &proc_head->this_node) {
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
    if (!proc_head)
        return -1;

    list_for_each(node, &proc_head->this_node) {
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

    tcb* first = thread_run;
    while (1) {
        thread_run = list_entry(list_next(&thread_run->this_node), tcb, this_node);
        if (thread_run->state == TS_READY)
            break;
        if (thread_run == first)
            return;
    }
    spinlock_lock(thread_run->sp_lock);
    arch_thread_restore_context(&thread_run->context);
    spinlock_unlock(thread_run->sp_lock);
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
    irq_request(&schedule_irq, "tmr", TIMER_IRQ_NO, 0, schedule_isr, 0);
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

module_init(proc_env_init);
module_exit(proc_env_exit);
