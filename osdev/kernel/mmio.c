#include "kernel/syscall.h"
#include "kernel/uapi.h"
#include "kernel/errno.h"
#include "arch_irq.h"
#include "kernel/process.h"
#include "kernel/capability.h"
#include "lib/module.h"
#include "lib/types.h"
#include "kernel/log.h"    /* LOG() diagnostic (COM1) */
#include "kernel/mmio.h"

static i32 mmio_scall_handle = -1;

static int mmio_exec(mmio_syscall_data* cfg)
{
    pcb* proc = get_current_process();

    if (!cfg || !proc)
        return E_INVAL;

    switch (cfg->cmd) {
    case MMIO_CTRL_MAP: {
        /*
         * Caller-chosen fixed VA in the high user area via vmm_map_fixed()
         * with own_phys = 0 (MMIO-safe: never handed back to the PMM).
         * vmm validates page alignment, CAP_MAP_MEM and rbtree overlap
         * (E_EXISTS vs the process's own ELF); here we only insist the VA
         * lies above the shared user-heap window and stays inside user
         * space, so the caller cannot alias the heap / kernel mappings.
         */
        if (cfg->va < USER_HEAP_END ||
            cfg->size == 0 || cfg->size > USER_SPACE_TOP - cfg->va)
            return E_INVAL;
        cfg->ret = vmm_map_fixed(proc, cfg->pa, (void*)(uptr)cfg->va,
                                 cfg->size, PTE_USER_PAGE, 0);
        if (cfg->ret == 0)
            LOG("[mmio] map pa=0x%x size=0x%x -> va=0x%x",
                cfg->pa, cfg->size, cfg->va);
        return cfg->ret;
    }
    case MMIO_CTRL_UNMAP:
        cfg->ret = vmm_unmap_fixed(proc, (void*)(uptr)cfg->va, cfg->size);
        return cfg->ret;
    default:
        return E_INVAL;
    }
}

static int mmio_syscall_isr(void* data)
{
    mmio_syscall_data* cfg = (mmio_syscall_data*)data;
    if (!cfg)
        return E_INVAL;

    return mmio_exec(cfg);
}

void mmio_syscall_init(void)
{
    mmio_scall_handle = syscall_register(SYSCALL_MMIO, mmio_syscall_isr,
                            sizeof(mmio_syscall_data));
}

void mmio_syscall_exit(void)
{
    syscall_unregister(mmio_scall_handle);
}

int mmio_map(u32 pa, u32 size, void* vaddr)
{
    mmio_syscall_data cfg = {0};
    cfg.cmd  = MMIO_CTRL_MAP;
    cfg.pa   = pa;
    cfg.size = size;
    cfg.va   = (u32)(uptr)vaddr;

    arch_syscall(mmio_scall_handle, &cfg, sizeof(cfg));

    return cfg.ret;
}

int mmio_unmap(void* vaddr, u32 size)
{
    mmio_syscall_data cfg = {0};
    cfg.cmd  = MMIO_CTRL_UNMAP;
    cfg.va   = (u32)(uptr)vaddr;
    cfg.size = size;

    arch_syscall(mmio_scall_handle, &cfg, sizeof(cfg));

    return cfg.ret;
}

module_init(mmio_syscall_init);
module_exit(mmio_syscall_exit);
