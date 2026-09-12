#include "ipc/shm.h"
#include "kernel/capability.h"

int shm_share(i32 pid, void* va, size_t size, void** out_va)
{
    pcb* curr = get_current_process();
    pcb* target = get_process_by_pid(pid);
    u32 pa = 0;
    u32 src_pa = 0;
    u32 src_size = 0;
    void* target_va = 0;
    cap_mem cmem = {0};
    u32 aligned_pa = 0;
    u32 aligned_pa_size = 0;
    int ret = 0;

    if (!curr || !target)
        return E_NOTFOUND;
    if (size == 0)
        return E_INVAL;

    /* CAP_IPC gate: sharing memory with another process is an IPC
     * operation; the caller must hold a CAP_IPC grant.  Kernel processes
     * are trusted and skip the check. */
    if (curr->priv != PROC_PRIV_KERNEL) {
        int ipc_ok = 1;
        if (cap_check(curr, CAP_IPC, &ipc_ok) != 0)
            return E_PERM;
    }

    pa = vmm_va_to_pa(curr, (u32)va);
    if (!pa)
        return E_INVAL;

    aligned_pa = pa & ~(PAGE_SIZE - 1);  // aligned PA
    aligned_pa_size = (size + (pa - aligned_pa) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);   // aligned size

    if (vmm_lookup_region(curr, (u32)va, &src_pa, &src_size, 0) != 0)
        return E_INVAL;
    if (aligned_pa < src_pa ||
        aligned_pa_size > src_size - (aligned_pa - src_pa))
        return E_INVAL;

    cmem.base = aligned_pa;
    cmem.size = aligned_pa_size;
    cmem.flags = PTE_USER_PAGE;
    ret = cap_grant(target, CAP_MAP_MEM, &cmem);
    if (ret)
        return ret;

    target_va = vmm_map_memory(target, pa, size, PTE_USER_PAGE);
    if (VMM_IS_ERR(target_va)) {
        cap_revoke(target, CAP_MAP_MEM, &cmem);
        return E_LIMIT;
    }

    if (out_va)
        *out_va = target_va;
    return 0;
}

int shm_unshare(i32 pid, void* va)
{
    pcb* curr = get_current_process();
    pcb* target = get_process_by_pid(pid);
    u32 pa = 0;
    u32 pa_size = 0;
    void* region_va = 0;
    cap_mem cmem = {0};

    if (!curr || !target)
        return E_NOTFOUND;

    /* CAP_IPC gate: tearing down shared memory is an IPC operation; the
     * caller must hold a CAP_IPC grant.  Kernel processes are trusted. */
    if (curr->priv != PROC_PRIV_KERNEL) {
        int ipc_ok = 1;
        if (cap_check(curr, CAP_IPC, &ipc_ok) != 0)
            return E_PERM;
    }

    if (vmm_lookup_region(target, (u32)va, &pa, &pa_size, &region_va))
        return E_NOTFOUND;
    /* Unmap from the region's ALIGNED start with the full region size.
     * Unmapping from the (possibly unaligned) payload VA with the full
     * size would fail vmm_mmap_release's bounds check
     * (va + size > region end), leaking the mapping + its CAP_MAP_MEM
     * grant — which then makes a second share of the same page fail on
     * the duplicate cap grant (portal console prints hit this). */
    if (vmm_unmap_memory(target, region_va, pa_size))
        return E_LIMIT;

    cmem.base = pa;
    cmem.size = pa_size;
    cmem.flags = PTE_USER_PAGE;
    return cap_revoke(target, CAP_MAP_MEM, &cmem);
}
