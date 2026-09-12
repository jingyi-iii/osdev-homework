#include "arch_task.h"
#include "lib/string.h"
#include "kernel/errno.h"
#include "mm/vmm.h"

#ifndef LOG
#define LOG(x) 
#endif

static volatile tss g_tss = {0};
volatile arch_task_context* curr_task_ctx = 0;

// TSS is only used to provide ss0 and esp0 when entering ring0 from non-ring0
int tss_init(void)
{
    u64 tss_desc = 0;
    u32 tss_sel = 0;
    u16 flags = 0;

    tss_sel = arch_get_sel(TSS);
    if (!tss_sel) {
        LOG("failed to get TSS selector");
        return E_INTERNAL;
    }

    g_tss.ss0 = arch_get_sel(SYS_DATA);
    g_tss.esp0 = 0; // need to set value later
    g_tss.iobase = sizeof(tss);

    flags = 0;
    flags |= 0x89;           // 32bit TSS type(0x9), present(0x80)

    tss_desc = arch_gen_desc((u32)&g_tss, sizeof(tss), flags);
    arch_set_desc(TSS, tss_desc);
    arch_reload_tss(tss_sel);

    return 0;
}

static inline void ldt_reload(volatile arch_task_context* context)
{
    if (!context)
        return;

    u64 ldt_desc = arch_gen_desc((u32)context->ldts, 2 * 8, 0x0082);
    arch_set_desc(LDT, ldt_desc);
    arch_reload_ldt(arch_get_sel(LDT));
}

int arch_task_context_init(vmm_control_block* vcb, arch_task_context* context, task_entry_t entry, task_priv priv)
{
    u8 ring = priv == TASK_PRIV_KERNEL ? 0 : 3;

    if (!context || !vcb) {
        LOG("task context or vcb is null");
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
        LOG("failed to alloc task stack");
        return E_NOMEM;
    }
    context->regs = (regs*)((u8*)context->stack + 0x1000 - sizeof(regs));
    context->regs->cs = 0x0 | 0x4 | ring;
    context->regs->gs = 0x8 | 0x4 | ring;
    context->regs->fs = 0x8 | 0x4 | ring;
    context->regs->es = 0x8 | 0x4 | ring;
    context->regs->ds = 0x8 | 0x4 | ring;
    context->regs->ss = 0x8 | 0x4 | ring;
    context->regs->eip = (u32)entry;
    context->regs->esp = (u32)context->stack + 0x1000 - sizeof(regs);

    /* IOPL=0 (EFLAGS bits 12-13 clear): ring-3 threads may NO longer execute
     * privileged in/out (or cli/sti) directly.  All port I/O from user mode
     * is routed through the io syscall gate (kernel/io.c), which enforces
     * the CAP_ACCESS_IO capability.  Kernel threads (ring=0) may still run
     * in/out natively, so they do not need IOPL. */
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
        g_tss.esp0 = (u32)context->regs + sizeof(regs);
    curr_task_ctx = context;
    ldt_reload(curr_task_ctx);
    return 0;
}