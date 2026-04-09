#include "process.h"
#include "heap.h"
#include "core_api.h"
#include "iodev_api.h"
#include "arch_protm.h"
#include "module.h"
#include "string.h"

pcb *proc_run = 0;
list_node proc_head = {0};

int32_t create_proc(uint8_t ring, proc_entry_t entry)
{
    pcb* proc = 0;
    static uint32_t pid = 0;

    if (ring > 3)
        return -1;

    proc = (pcb*)kmalloc(sizeof(pcb));
    if (!proc) {
        KLOG("failed to alloc memory for pcb");
        return -1;
    }

    if (proc_context_init(&proc->context, entry, ring)) {
        KLOG("failed to init process context");
        return -22;
    }
    proc->pid = pid++;

    list_init(&proc->pcb_node);
    proc->sp_lock = spinlock_alloc();
    if (!proc->sp_lock) {
        KLOG("failed to alloc spin lock for pcb");
        return -1;
    }

    if (!proc_run) {    // the first process
        proc_run = proc;
        spinlock_lock(proc_run->sp_lock);
        proc_restore_context(&proc_run->context);
        spinlock_unlock(proc_run->sp_lock);
    } else {
        spinlock_lock(proc_run->sp_lock);
        list_add(&proc->pcb_node, &proc_run->pcb_node);
        spinlock_unlock(proc_run->sp_lock);
    }

    KLOG("add process, pid %d", proc->pid);

    return proc->pid;
}

static void schedule(struct irqdev* dev)
{
    (void)dev;
    static uint32_t timeslice = 0;

    timeslice++;
    if (timeslice < 5 || !proc_run)
        return;
    timeslice = 0;

    spinlock_lock(proc_run->sp_lock);
    proc_run = list_entry(list_next(&proc_run->pcb_node), pcb, pcb_node);
    proc_restore_context(&proc_run->context);
    spinlock_unlock(proc_run->sp_lock);
}

static irqdev* pcb_irqdev = 0;
void process_evn_setup(void)
{
    uint32_t i = 0;

    list_init(&proc_head);
    tss_init();
    irqdev_init(&pcb_irqdev, "tmr", TIMER_IRQ_NO, schedule);
    if (pcb_irqdev) {
        pcb_irqdev->unmask(pcb_irqdev);
    }
}

module_init(process_evn_setup);
