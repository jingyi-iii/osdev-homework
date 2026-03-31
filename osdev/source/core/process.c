#include "process.h"
#include "heap.h"
#include "arch_protm.h"
#include "core_api.h"
#include "module.h"
#include "spinlock.h"
#include "string.h"

extern uint8_t stack_top;
static spinlock* lock_dev;
static volatile tss_t tss = {0};
static process_t proc_tbl[MAX_PROCESS] = {0};
static volatile uint8_t avail_proc_nr = 0;
process_t *proc_run = 0;
// volatile regs_t save_regs = {0};

// TSS is only used to provide ss0 and esp0 when entering ring0
static int tss_init(void)
{
    uint64_t tss_desc = 0;
    uint32_t tss_sel = 0;
    uint16_t flags = 0;

    spinlock_lock(lock_dev);
    tss_sel = arch_get_sel(TSS);
    if (!tss_sel) {
        spinlock_unlock(lock_dev);
        return -1;
    }

    tss.ss0 = arch_get_sel(SYS_DATA);
    tss.esp0 = (uint32_t)&stack_top;
    tss.iobase = sizeof(tss_t);

    flags = 0;
    flags |= 0x89;           // 32bit TSS type(0x9), present(0x80)

    tss_desc = arch_gen_desc((uint32_t)&tss, sizeof(tss_t), flags);
    arch_set_desc(TSS, tss_desc);
    arch_reload_tss(tss_sel);
    spinlock_unlock(lock_dev);

    return 0;
}

static int ldt_reload(void)
{
    uint64_t ldt_desc = arch_gen_desc((uint32_t)proc_run->ldts, 2 * 8, 0x0082);
    arch_set_desc(LDT, ldt_desc);
    arch_reload_ldt(arch_get_sel(LDT));

    return 0;
}

int32_t create_proc(uint8_t ring, proc_entry_t entry)
{
    process_t* proc = 0;
    process_t* prev = 0;

    if (ring > 3)
        return -1;

    spinlock_lock(lock_dev);
    if ((avail_proc_nr + 1) >= MAX_PROCESS) {
        spinlock_unlock(lock_dev);
        return -1;
    }

    proc = &proc_tbl[avail_proc_nr++];
    // exec/read code segment, 0 ~ 0xfffff
    proc->ldts[0] = 0x0000ffff;
    proc->ldts[1] = 0x00cf9a00 | (ring << 13);
    // r/w data segment, 0 ~ 0xfffff
    proc->ldts[2] = 0x0000ffff;
    proc->ldts[3] = 0x00cf9200 | (ring << 13);

    proc->stack = kmalloc(0x1000);    // 4KB stack
    // proc->regs = (regs_t*)kmalloc(sizeof(regs_t));
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
    proc->pid = proc - proc_tbl;
    proc->ring = ring;

    if (!proc_run) {    // the first process
        proc_run = proc;
        proc_run->next = proc_run;
        if (proc->ring)
            tss.esp0 = (uint32_t)proc_run->regs + sizeof(regs_t);
        ldt_reload();
    } else {
        prev = proc_tbl;
        while (prev && prev->next != proc_tbl)
            prev = prev->next;

        prev->next = proc;
        proc->next = proc_tbl;
    }

    spinlock_unlock(lock_dev);
    return proc->pid;
}

static void schedule(struct irqdev* dev)
{
    (void)dev;

    static uint32_t timeslice = 0;

    spinlock_lock(lock_dev);
    timeslice++;
    if (timeslice < 5 || !proc_run || !proc_run->next) {
    // if (!proc_run || !proc_run->next) {
        spinlock_unlock(lock_dev);
        return;
    }
    timeslice = 0;

    // if (proc_run->ring)
    //     memcpy(proc_run->regs, (const void*)&save_regs, sizeof(regs_t));
    proc_run = proc_run->next;
    if (proc_run->ring)
        tss.esp0 = (uint32_t)proc_run->regs + sizeof(regs_t);
    // tss.esp0 = (uint32_t)&proc_run->regs + sizeof(regs_t);
    ldt_reload();

    spinlock_unlock(lock_dev);
}

static irqdev* pcb_irqdev = 0;
void process_evn_setup(void)
{
    uint32_t i = 0;

    lock_dev = spinlock_alloc();
    if (!lock_dev) {
        return;
    }

    tss_init();
    
    memset(proc_tbl, 0, sizeof(proc_tbl));
    for (i = 0; i < MAX_PROCESS; i++)
        proc_tbl[i].pid = -1;

    irqdev_init(&pcb_irqdev, "tmr", TIMER_IRQ_NO, schedule);
    if (pcb_irqdev) {
        pcb_irqdev->unmask(pcb_irqdev);
    }
}

module_init(process_evn_setup);
