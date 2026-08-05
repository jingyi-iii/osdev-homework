#include "arch_task.h"
#include "lib/string.h"
#include "kernel/errno.h"
#include "mm/vmm.h"
#include "mm/pmm.h"

#ifndef KLOG
#define KLOG(x) 
#endif

static volatile tss_t tss = {0};
volatile arch_task_context* curr_task_ctx = 0;

// TSS is only used to provide ss0 and esp0 when entering ring0 from non-ring0
int tss_init(void)
{
    uint64_t tss_desc = 0;
    uint32_t tss_sel = 0;
    uint16_t flags = 0;

    tss_sel = arch_get_sel(TSS);
    if (!tss_sel) {
        KLOG("failed to get TSS selector");
        return E_INTERNAL;
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

static inline void ldt_reload(arch_task_context* context)
{
    if (!context)
        return;

    uint64_t ldt_desc = arch_gen_desc((uint32_t)context->ldts, 2 * 8, 0x0082);
    arch_set_desc(LDT, ldt_desc);
    arch_reload_ldt(arch_get_sel(LDT));
}

int arch_task_context_init(vmm_control_block* vcb, arch_task_context* context, task_entry_t entry, task_priv priv)
{
    uint8_t ring = priv == TASK_PRIV_KERNEL ? 0 : 3;

    if (!context || !vcb) {
        KLOG("task context or vcb is null");
        return E_INVAL;
    }

    // exec/read code segment, 0 ~ 0xfffff
    context->ldts[0] = 0x0000ffff;
    context->ldts[1] = 0x00cf9a00 | (ring << 13);
    // r/w data segment, 0 ~ 0xfffff
    context->ldts[2] = 0x0000ffff;
    context->ldts[3] = 0x00cf9200 | (ring << 13);

    if (ring) {
        context->stack = vmm_alloc_pages(vcb, 1, PTE_USER_PAGE);    // user stack
    } else {
        context->stack = kmalloc(0x1000);    // 4KB stack
    }
    if (!context->stack) {
        KLOG("failed to alloc task stack");
        return E_NOMEM;
    }
    context->regs = (regs_t*)((uint8_t*)context->stack + 0x1000 - sizeof(regs_t));
    context->regs->cs = 0x0 | 0x4 | ring;
    context->regs->gs = 0x8 | 0x4 | ring;
    context->regs->fs = 0x8 | 0x4 | ring;
    context->regs->es = 0x8 | 0x4 | ring;
    context->regs->ds = 0x8 | 0x4 | ring;
    context->regs->ss = 0x8 | 0x4 | ring;
    context->regs->eip = (uint32_t)entry;
    context->regs->esp = (uint32_t)context->stack + 0x1000 - sizeof(regs_t);
    context->regs->eflags = 0x0202;
    context->ring = ring;

    return 0;
}

void arch_task_context_release(vmm_control_block* vcb, arch_task_context* context)
{
    if (!context || !vcb)
        return;

    if (context->stack) {
        if (context->ring) {
            vmm_free_pages(vcb, context->stack);
        } else {
            kfree(context->stack);            /* kernel stack: heap free */
        }
    }

    memset(context, 0, sizeof(arch_task_context));
}

int arch_task_restore_context(arch_task_context* context)
{
    if (!context)
        return E_INVAL;

    if (context->ring)
        tss.esp0 = (uint32_t)context->regs + sizeof(regs_t);
    curr_task_ctx = context;
    ldt_reload(curr_task_ctx);
    return 0;
}