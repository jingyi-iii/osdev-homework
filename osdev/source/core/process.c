#include "process.h"
#include "heap.h"
#include "core_api.h"
#include "iodev_api.h"
#include "arch_protm.h"
#include "module.h"
#include "string.h"

static volatile tss_t tss = {0};
pcb *proc_run = 0;

// TSS is only used to provide ss0 and esp0 when entering ring0 from non-ring0
static int tss_init(void)
{
    uint64_t tss_desc = 0;
    uint32_t tss_sel = 0;
    uint16_t flags = 0;

    tss_sel = arch_get_sel(TSS);
    if (!tss_sel) {
        KLOG("failed to get TSS selector");
        return -1;
    }

    tss.ss0 = arch_get_sel(SYS_DATA);
    tss.esp0 = 0; // need to set value later
    tss.iobase = sizeof(tss_t);

    flags = 0;
    flags |= 0x89;           // 32bit TSS type(0x9), present(0x80)

    tss_desc = arch_gen_desc((uint32_t)&tss, sizeof(tss_t), flags);
    arch_set_desc(TSS, tss_desc);
    arch_reload_tss(tss_sel);

    return 0;
}

static inline void ldt_reload(void)
{
    uint64_t ldt_desc = arch_gen_desc((uint32_t)proc_run->ldts, 2 * 8, 0x0082);
    arch_set_desc(LDT, ldt_desc);
    arch_reload_ldt(arch_get_sel(LDT));
}

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

    // exec/read code segment, 0 ~ 0xfffff
    proc->ldts[0] = 0x0000ffff;
    proc->ldts[1] = 0x00cf9a00 | (ring << 13);
    // r/w data segment, 0 ~ 0xfffff
    proc->ldts[2] = 0x0000ffff;
    proc->ldts[3] = 0x00cf9200 | (ring << 13);

    proc->stack = kmalloc(0x1000);    // 4KB stack
    proc->regs = (regs_t*)((uint8_t*)proc->stack + 0x1000 - sizeof(regs_t));
    proc->regs->cs = 0x0 | 0x4 | ring;
    proc->regs->gs = 0x8 | 0x4 | ring;
    proc->regs->fs = 0x8 | 0x4 | ring;
    proc->regs->es = 0x8 | 0x4 | ring;
    proc->regs->ds = 0x8 | 0x4 | ring;
    proc->regs->ss = 0x8 | 0x4 | ring;
    proc->regs->eip = (uint32_t)entry;
    proc->regs->esp = (uint32_t)proc->stack + 0x1000;
    proc->regs->eflags = 0x0202;
    proc->pid = pid++;
    proc->ring = ring;
    list_init(&proc->pcb_node);
    proc->sp_lock = spinlock_alloc();
    if (!proc->sp_lock) {
        KLOG("failed to alloc spin lock for pcb");
        return -1;
    }

    if (!proc_run) {    // the first process
        proc_run = proc;
        if (proc->ring)
            tss.esp0 = (uint32_t)proc_run->regs + sizeof(regs_t);
        ldt_reload();
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

    proc_run = list_entry(list_next(&proc_run->pcb_node), pcb, pcb_node);

    spinlock_lock(proc_run->sp_lock);
    if (proc_run->ring)
        tss.esp0 = (uint32_t)proc_run->regs + sizeof(regs_t);
    ldt_reload();
    spinlock_unlock(proc_run->sp_lock);
}

static irqdev* pcb_irqdev = 0;
void process_evn_setup(void)
{
    uint32_t i = 0;

    tss_init();
    irqdev_init(&pcb_irqdev, "tmr", TIMER_IRQ_NO, schedule);
    if (pcb_irqdev) {
        pcb_irqdev->unmask(pcb_irqdev);
    }
}

module_init(process_evn_setup);
