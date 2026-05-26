#include "kernel/process.h"
#include "drivers/log_driver.h"
#include "kernel/thread.h"

static struct pcb *proc_head = 0;

int32_t create(struct pcb* parent, thread_priv priv, thread_entry_t entry);
void delete(int32_t tid);

int proc_create(proc_priv priv, thread_entry_t main_thread_entry)
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

    return create(proc, (thread_priv)priv, main_thread_entry);
}

void proc_exit(int32_t pid)
{
    if (!proc_head)
        return;

    list_for_each(node, &proc_head->this_node) {
        struct pcb* proc = list_entry(node, struct pcb, this_node);
        if (!proc || proc->pid != pid)
            continue;

        spinlock_lock(proc->sp_lock);
        list_for_each(tcb_node, &proc->tcbs) {
            struct tcb* thread = list_entry(tcb_node, struct tcb, proc_node);
            if (!thread)
                continue;

            spinlock_lock(thread->sp_lock);
            delete(thread->tid);
            spinlock_unlock(thread->sp_lock);
        }
        spinlock_unlock(proc->sp_lock);
    }
}

int proc_block(int32_t pid)
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

int proc_unblock(int32_t pid)
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