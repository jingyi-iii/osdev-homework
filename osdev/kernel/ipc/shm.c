#include "ipc/shm.h"
#include "kernel/capability.h"

int shm_share(int32_t pid, void* va, size_t size, void** out_va)
{
    pcb* curr = get_current_process();
    pcb* target = get_process_by_pid(pid);
    uint32_t pa = 0;
    uint32_t src_pa = 0;
    uint32_t src_size = 0;
    void* target_va = 0;
    cap_mem cmem = {0};
    uint32_t aligned_pa = 0;
    uint32_t aligned_pa_size = 0;
    int ret = 0;

    if (!curr || !target)
        return E_NOTFOUND;
    if (size == 0)
        return E_INVAL;

    pa = vmm_va_to_pa(curr, (uint32_t)va);
    if (!pa)
        return E_INVAL;

    aligned_pa = pa & ~(PAGE_SIZE - 1);  // aligned PA
    aligned_pa_size = (size + (pa - aligned_pa) + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);   // aligned size

    if (vmm_lookup_region(curr, (uint32_t)va, &src_pa, &src_size) != 0)
        return E_INVAL;
    if (aligned_pa < src_pa ||
        aligned_pa_size > src_size - (aligned_pa - src_pa))
        return E_INVAL;

    cmem.base = aligned_pa;
    cmem.size = aligned_pa_size;
    cmem.flags = PTE_USER_PAGE;
    ret = cap_grant(target, CAP_MEM_MAP, &cmem);
    if (ret)
        return ret;

    target_va = vmm_map_memory(target, pa, size, PTE_USER_PAGE);
    if (VMM_IS_ERR(target_va)) {
        cap_revoke(target, CAP_MEM_MAP, &cmem);
        return E_LIMIT;
    }

    if (out_va)
        *out_va = target_va;
    return 0;
}

int shm_unshare(int32_t pid, void* va)
{
    pcb* curr = get_current_process();
    pcb* target = get_process_by_pid(pid);
    uint32_t pa = 0;
    uint32_t pa_size = 0;
    cap_mem cmem = {0};

    if (!curr || !target)
        return E_NOTFOUND;

    if (vmm_lookup_region(target, (uint32_t)va, &pa, &pa_size))
        return E_NOTFOUND;
    if (vmm_unmap_memory(target, va, pa_size))
        return E_LIMIT;

    cmem.base = pa;
    cmem.size = pa_size;
    cmem.flags = PTE_USER_PAGE;
    return cap_revoke(target, CAP_MEM_MAP, &cmem);
}
