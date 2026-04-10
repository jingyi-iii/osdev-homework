#include "arch_process.h"
#include "iodev_api.h"

static volatile tss_t tss = {0};
volatile proc_context* curr_context = 0;

// TSS is only used to provide ss0 and esp0 when entering ring0 from non-ring0
int tss_init(void)
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

static inline void ldt_reload(proc_context* context)
{
    if (!context)
        return;

    uint64_t ldt_desc = arch_gen_desc((uint32_t)context->ldts, 2 * 8, 0x0082);
    arch_set_desc(LDT, ldt_desc);
    arch_reload_ldt(arch_get_sel(LDT));
}

int proc_context_init(proc_context* context, proc_entry_t entry, proc_priv priv)
{
    uint8_t ring = priv == PROC_PRIV_KERNEL ? 0 : 3;

    if (!context) {
        KLOG("process context is null");
        return -22;
    }

    // exec/read code segment, 0 ~ 0xfffff
    context->ldts[0] = 0x0000ffff;
    context->ldts[1] = 0x00cf9a00 | (ring << 13);
    // r/w data segment, 0 ~ 0xfffff
    context->ldts[2] = 0x0000ffff;
    context->ldts[3] = 0x00cf9200 | (ring << 13);

    context->stack = kmalloc(0x1000);    // 4KB stack
    context->regs = (regs_t*)((uint8_t*)context->stack + 0x1000 - sizeof(regs_t));
    context->regs->cs = 0x0 | 0x4 | ring;
    context->regs->gs = 0x8 | 0x4 | ring;
    context->regs->fs = 0x8 | 0x4 | ring;
    context->regs->es = 0x8 | 0x4 | ring;
    context->regs->ds = 0x8 | 0x4 | ring;
    context->regs->ss = 0x8 | 0x4 | ring;
    context->regs->eip = (uint32_t)entry;
    context->regs->esp = (uint32_t)context->stack + 0x1000;
    context->regs->eflags = 0x0202;
    context->ring = ring;

    return 0;
}

int proc_restore_context(proc_context* context)
{
    if (!context)
        return -22;

    if (context->ring)
        tss.esp0 = (uint32_t)context->regs + sizeof(regs_t);
    curr_context = context;
    ldt_reload(curr_context);
}