#include "kernel/process.h"
#include "mm/heap.h"
#include "arch_protm.h"
#include "lib/module.h"
#include "lib/string.h"
#include "drivers/log_driver.h"
#include "kernel/irq.h"

enum proc_ctrl {
    PROC_CTRL_CREATE = 0,
    PROC_CTRL_DELETE,
    PROC_CTRL_YIELD,
    PROC_CTRL_BLOCK,
    PROC_CTRL_UNBLOCK,
};

static pcb *proc_run = 0;

static int32_t create(proc_priv priv, proc_entry_t entry)
{
    pcb* proc = 0;
    static uint32_t pid = 0;

    KLOG("adding process, pid %d", pid);

    proc = (pcb*)kmalloc(sizeof(pcb));
    if (!proc) {
        KLOG("failed to alloc memory for pcb");
        return -1;
    }

    if (arch_proc_context_init(&proc->context, entry, priv)) {
        KLOG("failed to init process context");
        kfree(proc);
        return -22;
    }
    proc->pid = pid++;
    proc->state = PS_READY_TO_RUN;

    list_init(&proc->pcb_node);
    proc->sp_lock = spinlock_alloc();
    if (!proc->sp_lock) {
        KLOG("failed to alloc spin lock for pcb");
        arch_proc_context_release(&proc->context);
        kfree(proc);
        return -1;
    }

    if (!proc_run) {    // the first process
        spinlock_lock(proc->sp_lock);
        proc_run = proc;
        arch_proc_restore_context(&proc_run->context);
        spinlock_unlock(proc->sp_lock);
    } else {
        spinlock_lock(proc_run->sp_lock);
        list_add(&proc->pcb_node, &proc_run->pcb_node);
        spinlock_unlock(proc_run->sp_lock);
    }

    KLOG("add process, pid %d", proc->pid);

    return proc->pid;
}

static void delete(int32_t pid)
{
    pcb* proc = 0;

    if (!proc_run)
        return;
    
    if (proc_run->pid != pid) {
        list_for_each(node, &proc_run->pcb_node) {
            pcb* proc = list_entry(node, pcb, pcb_node);
            if (!proc)
                continue;

            if (proc->pid == pid) {
                spinlock_lock(proc->sp_lock);
                arch_proc_context_release(&proc->context);
                spinlock_unlock(proc->sp_lock);
                list_del(&proc->pcb_node);
                spinlock_release(proc->sp_lock);
                kfree(proc);
                break;
            }
        }
    } else {
        proc = list_entry(list_next(&proc_run->pcb_node), pcb, pcb_node);
        if (proc == proc_run) {
            KLOG("no more process to run after deleting proc with pid %d", pid);
            return;
        }

        spinlock_lock(proc_run->sp_lock);
        arch_proc_context_release(&proc_run->context);
        list_del(&proc_run->pcb_node);
        spinlock_unlock(proc_run->sp_lock);

        pcb* old = proc_run;
        proc_run = proc;
        arch_proc_restore_context(&proc->context);
        spinlock_release(old->sp_lock);
        kfree(old);
        return;
    }
}

static void schedule_isr(void* p)
{
    (void)p;
    static uint32_t timeslice = 0;

    timeslice++;
    if (timeslice < 5 || !proc_run)
        return;
    timeslice = 0;

    pcb* first = proc_run;
    while (1) {
        proc_run = list_entry(list_next(&proc_run->pcb_node), pcb, pcb_node);
        if (proc_run->state == PS_READY_TO_RUN)
            break;
        if (proc_run == first)
            return;
    }
    spinlock_lock(proc_run->sp_lock);
    arch_proc_restore_context(&proc_run->context);
    spinlock_unlock(proc_run->sp_lock);
}

static void syscall_isr(void* data)
{
    proc_ctrl_config *config = (proc_ctrl_config*)data;

    switch (config->cmd) {
    case PROC_CTRL_CREATE:
        config->pid = create(config->priv, config->entry);
        break;
    case PROC_CTRL_DELETE:
        delete(config->pid);
        break;
    case PROC_CTRL_YIELD: {
        pcb* next = proc_run;
        do {
            next = list_entry(list_next(&next->pcb_node), pcb, pcb_node);
        } while (next != proc_run && next->state != PS_READY_TO_RUN);

        if (next != proc_run && next->state == PS_READY_TO_RUN) {
            spinlock_lock(next->sp_lock);
            arch_proc_restore_context(&next->context);
            spinlock_unlock(next->sp_lock);
            proc_run = next;
        }
        break;
    }
    case PROC_CTRL_BLOCK:
        list_for_each(node, &proc_run->pcb_node) {
            pcb* p = list_entry(node, pcb, pcb_node);
            if (!p || p->pid != config->pid)
                continue;

            spinlock_lock(p->sp_lock);
            p->state = PS_PENDING;
            spinlock_unlock(p->sp_lock);

            if (p == proc_run) {
                pcb* next = proc_run;
                do {
                    next = list_entry(list_next(&next->pcb_node), pcb, pcb_node);
                } while (next != proc_run && next->state != PS_READY_TO_RUN);

                if (next != proc_run && next->state == PS_READY_TO_RUN) {
                    spinlock_lock(next->sp_lock);
                    arch_proc_restore_context(&next->context);
                    spinlock_unlock(next->sp_lock);
                    proc_run = next;
                }
            }
            break;
            spinlock_unlock(p->sp_lock);
        }
        break;
    case PROC_CTRL_UNBLOCK:
        list_for_each(node, &proc_run->pcb_node) {
            pcb* p = list_entry(node, pcb, pcb_node);
            if (!p || p->pid != config->pid)
                continue;

            spinlock_lock(p->sp_lock);
            if (p->pid == config->pid) {
                p->state = PS_READY_TO_RUN;
                spinlock_unlock(p->sp_lock);
                break;
            }
            spinlock_unlock(p->sp_lock);
        }
        break;
    default:
        break;
    }
}

void proc_yield(void)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_YIELD;

    arch_syscall(0, &config);
}

void proc_block(int32_t pid)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_BLOCK;
    config.pid = pid;

    arch_syscall(0, &config);  
}

void proc_unblock(int32_t pid)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_UNBLOCK;
    config.pid = pid;

    arch_syscall(0, &config);
}

int32_t proc_create(proc_priv priv, proc_entry_t entry)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_CREATE;
    config.priv = priv;
    config.entry = entry;

    arch_syscall(0, &config);

    return config.pid;
}

void proc_exit(int32_t pid)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_DELETE;
    config.pid = pid;

    arch_syscall(0, &config);
}

static irq* pcb_irq = 0;
static irq* pcb_scall = 0;

static void process_evn_init(void)
{
    tss_init();
    irq_request(&pcb_irq, "tmr", TIMER_IRQ_NO, 0, schedule_isr, 0);
    if (pcb_irq)
        irq_unmask(pcb_irq);

    irq_request(&pcb_scall, "proc_syscall", 100, 0, syscall_isr, 0);
    if (pcb_scall)
        irq_unmask(pcb_scall);
}

static void process_evn_exit(void)
{
    if (pcb_irq) {
        irq_mask(pcb_irq);
        irq_release(pcb_irq);
    }

    if (pcb_scall) {
        irq_mask(pcb_scall);
        irq_release(pcb_scall);
    }
}

module_init(process_evn_init);
module_exit(process_evn_exit);
