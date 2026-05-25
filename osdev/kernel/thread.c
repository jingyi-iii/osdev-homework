#include "kernel/thread.h"
#include "mm/heap.h"
#include "arch_protm.h"
#include "lib/module.h"
#include "lib/string.h"
#include "drivers/log_driver.h"
#include "kernel/irq.h"

enum thread_ctrl {
    THREAD_CTRL_CREATE = 0,
    THREAD_CTRL_DELETE,
    THREAD_CTRL_YIELD,
    THREAD_CTRL_BLOCK,
    THREAD_CTRL_UNBLOCK,
};

static tcb *thread_run = 0;

int32_t create(pcb* parent, thread_priv priv, thread_entry_t entry)
{
    tcb* thread = 0;
    static uint32_t tid = 0;

    // if (!parent) {
    //     KLOG("failed to create thread without parent process");
    //     return -1;
    // }

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
    thread->sp_lock = spinlock_alloc();
    if (!thread->sp_lock) {
        KLOG("failed to alloc spin lock for tcb");
        arch_thread_context_release(&thread->context);
        kfree(thread);
        return -1;
    }

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
    // thread->parent = parent;
    // list_add(&thread->this_node, &parent->tcbs);

    KLOG("add thread, tid %d", thread->tid);

    return thread->tid;
}

void delete(int32_t tid)
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
        spinlock_unlock(thread_run->sp_lock);

        tcb* old = thread_run;
        thread_run = thread;
        arch_thread_restore_context(&thread->context);
        spinlock_release(old->sp_lock);
        kfree(old);
        return;
    }
}

void block(int32_t tid)
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

void unblock(int32_t tid)
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

void yield(void)
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
    thread_ctrl_config *config = (thread_ctrl_config*)data;

    switch (config->cmd) {
    case THREAD_CTRL_CREATE:
        config->tid = create(0, config->priv, config->entry);
        break;
    case THREAD_CTRL_DELETE:
        delete(config->tid);
        break;
    case THREAD_CTRL_YIELD:
        yield();
        break;
    case THREAD_CTRL_BLOCK:
        block(config->tid);
        break;
    case THREAD_CTRL_UNBLOCK:
        unblock(config->tid);
        break;
    default:
        break;
    }
}

void thread_yield(void)
{
    thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_YIELD;

    arch_syscall(0, &config);
}

void thread_block(int32_t tid)
{
    thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_BLOCK;
    config.tid = tid;

    arch_syscall(0, &config);  
}

void thread_unblock(int32_t tid)
{
    thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_UNBLOCK;
    config.tid = tid;

    arch_syscall(0, &config);
}

int32_t thread_create(thread_priv priv, thread_entry_t entry)
{
    thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_CREATE;
    config.priv = priv;
    config.entry = entry;

    arch_syscall(0, &config);

    return config.tid;
}

void thread_exit(int32_t tid)
{
    thread_ctrl_config config = {0};
    config.cmd = THREAD_CTRL_DELETE;
    config.tid = tid;

    arch_syscall(0, &config);
}

static irq* tcb_irq = 0;
static irq* tcb_scall = 0;

static void thread_evn_init(void)
{
    tss_init();
    irq_request(&tcb_irq, "tmr", TIMER_IRQ_NO, 0, schedule_isr, 0);
    if (tcb_irq)
        irq_unmask(tcb_irq);

    irq_request(&tcb_scall, "thread_syscall", 100, 0, syscall_isr, 0);
    if (tcb_scall)
        irq_unmask(tcb_scall);
}

static void thread_evn_exit(void)
{
    if (tcb_irq) {
        irq_mask(tcb_irq);
        irq_release(tcb_irq);
    }

    if (tcb_scall) {
        irq_mask(tcb_scall);
        irq_release(tcb_scall);
    }
}

module_init(thread_evn_init);
module_exit(thread_evn_exit);
