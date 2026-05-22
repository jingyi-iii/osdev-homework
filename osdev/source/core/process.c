#include "process.h"
#include "heap.h"
#include "arch_protm.h"
#include "module.h"
#include "string.h"
#include "log_driver.h"
#include "arch_irq.h"

#define PROC_CTRL_SCHEDULE  (1)
#define PROC_CTRL_BLOCK     (2)
#define PROC_CTRL_UNBLOCK   (3)

static pcb *proc_run = 0;

int32_t create_proc(proc_priv priv, proc_entry_t entry)
{
    pcb* proc = 0;
    static uint32_t pid = 0;

    proc = (pcb*)kmalloc(sizeof(pcb));
    if (!proc) {
        KLOG("failed to alloc memory for pcb");
        return -1;
    }

    if (proc_context_init(&proc->context, entry, priv)) {
        KLOG("failed to init process context");
        return -22;
    }
    proc->pid = pid++;
    proc->state = PS_READY_TO_RUN;

    list_init(&proc->pcb_node);
    proc->sp_lock = spinlock_alloc();
    if (!proc->sp_lock) {
        KLOG("failed to alloc spin lock for pcb");
        return -1;
    }

    if (!proc_run) {    // the first process
        spinlock_lock(proc->sp_lock);
        proc_run = proc;
        proc_restore_context(&proc_run->context);
        spinlock_unlock(proc->sp_lock);
    } else {
        spinlock_lock(proc_run->sp_lock);
        list_add(&proc->pcb_node, &proc_run->pcb_node);
        spinlock_unlock(proc_run->sp_lock);
    }

    KLOG("add process, pid %d", proc->pid);

    return proc->pid;
}

static void schedule_isr(void* dev)
{
    (void)dev;
    static uint32_t timeslice = 0;

    timeslice++;
    if (timeslice < 5 || !proc_run)
        return;
    timeslice = 0;

    while (1) {
        proc_run = list_entry(list_next(&proc_run->pcb_node), pcb, pcb_node);
        if (proc_run->state == PS_READY_TO_RUN)
            break;
    }
    spinlock_lock(proc_run->sp_lock);
    proc_restore_context(&proc_run->context);
    spinlock_unlock(proc_run->sp_lock);
}

static void syscall_isr(void* data)
{
    proc_ctrl_config *config = (proc_ctrl_config*)data;

    switch (config->cmd) {
    case PROC_CTRL_SCHEDULE:
        proc_run = list_entry(list_next(&proc_run->pcb_node), pcb, pcb_node);
        spinlock_lock(proc_run->sp_lock);
        proc_restore_context(&proc_run->context);
        spinlock_unlock(proc_run->sp_lock);
        break;
    case PROC_CTRL_BLOCK:
        list_for_each(node, &proc_run->pcb_node) {
            pcb* dev = list_entry(node, pcb, pcb_node);
            if (!dev)
                continue;
            spinlock_lock(dev->sp_lock);
            if (dev->pid == config->pid) {
                dev->state = PS_PENDING;
                spinlock_unlock(dev->sp_lock);
                break;
            }
            spinlock_unlock(dev->sp_lock);
        }
        break;
    case PROC_CTRL_UNBLOCK:
        list_for_each(node, &proc_run->pcb_node) {
            pcb* dev = list_entry(node, pcb, pcb_node);
            if (!dev)
                continue;
            spinlock_lock(dev->sp_lock);
            if (dev->pid == config->pid) {
                dev->state = PS_READY_TO_RUN;
                spinlock_unlock(dev->sp_lock);
                break;
            }
            spinlock_unlock(dev->sp_lock);
        }
        break;
    default:
        break;
    }
}

void schedule(void)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_SCHEDULE;

    arch_syscall(0, &config);
}

void block(int32_t pid)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_BLOCK;
    config.pid = pid;

    arch_syscall(0, &config);  
}

void unblock(int32_t pid)
{
    proc_ctrl_config config = {0};
    config.cmd = PROC_CTRL_UNBLOCK;
    config.pid = pid;

    arch_syscall(0, &config);
}

void exit(int32_t pid)
{

}

void yield(int32_t pid)
{

}


static irq* pcb_irq = 0;
static irq* pcb_scall = 0;
void process_evn_setup(void)
{
    tss_init();
    irq_request(&pcb_irq, "tmr", TIMER_IRQ_NO, 0, schedule_isr, 0);
    if (pcb_irq) {
        irq_unmask(pcb_irq);
    }

    irq_request(&pcb_scall, "proc_syscall", 100, 0, syscall_isr, 0);
    if (pcb_scall) {
        irq_unmask(pcb_scall);
    }
}
