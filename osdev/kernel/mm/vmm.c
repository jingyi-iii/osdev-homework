#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "kernel/process.h"
#include "kernel/capability.h"
#include "kernel/irq.h"
#include "arch_irq.h"
#include "lib/module.h"

/*
 * PA allocator:
 *   - vmm_create: page directory from the reserved paging pool
 *   - vmm_alloc_pages: user pages from the PMM
 * PA deallocator:
 *   - vmm_destroy: page directory back to the paging pool
 *   - vmm_free_pages: user pages back to the PMM
 *
 * VA allocator:
 *   - vmm_alloc_pages: for user pages
 *   - vmm_mmap_reserve: for mmap
 *
 * VA deallocator:
 *   - vmm_free_pages: for user pages
 *   - vmm_mmap_release: for mmap
 */

static int vmm_rbtree_node_cmp(const rbnode* left, const rbnode* right)
{
    if (!left || !right)
        return -1;

    vmm_region* l = rb_entry(left, vmm_region, node);
    vmm_region* r = rb_entry(right, vmm_region, node);

    if ((u32)l->start_va < (u32)r->start_va)
        return -1;
    else if ((u32)l->start_va > (u32)r->start_va)
        return 1;
    else
        return 0;
}

static int vmm_rbtree_key_cmp(const void* key, const rbnode* node)
{
    if (!node || !key)
        return -1;

    vmm_region* r = rb_entry(node, vmm_region, node);
    if (r->start_va <= key && (u32)key < (u32)r->start_va + r->size)
        return 0;
    else if ((u32)key < (u32)r->start_va)
        return -1;
    else
        return 1;
}

void vmm_switch(vmm_control_block* vcb)
{
    if (!vcb || !vcb->cr3)
        return;

    arch_load_cr3(vcb->cr3);
}

int vmm_create(vmm_control_block* vcb, int user_accessible)
{
    u32 pa = 0;

    if (!vcb)
        return E_INVAL;

    vcb->lock = spinlock_alloc();
    if (!vcb->lock)
        return ENOMEM;

    vcb->tree = rbtree_create();
    if (!vcb->tree) {
        spinlock_release(vcb->lock);
        return ENOMEM;
    }

    /* the page directory comes from the reserved paging pool so its
     * physical address can never collide with an identity-mapped user VA */
    pa = (u32)arch_paging_pool_alloc();
    if (!pa) {
        rbtree_destroy(vcb->tree);
        spinlock_release(vcb->lock);
        return ENOMEM;
    }

    vcb->cr3 = arch_clone_kernel_pde(pa, user_accessible);
    if (!vcb->cr3) {
        arch_paging_pool_free(pa);
        rbtree_destroy(vcb->tree);
        spinlock_release(vcb->lock);
        return ENOMEM;
    }

    return 0;
}

void vmm_destroy(vmm_control_block* vcb)
{
    if (!vcb || !vcb->cr3)
        return;

    spinlock_lock(vcb->lock);
    if (vcb->tree) {
        rbnode *pos, *n;
        rbtree_for_each_safe(pos, n, vcb->tree) {
            vmm_region* r = rb_entry(pos, vmm_region, node);
            for (u32 i = 0; i < r->size / PAGE_SIZE; i++)
                arch_unmap_4kb((void*)vcb->cr3,
                               (u8*)r->start_va + i * PAGE_SIZE);
            if (r->own_phys)
                pmm_free_pages(r->pa, r->size / PAGE_SIZE);
            rbtree_delete(vcb->tree, pos);
            kfree(r);
        }
        rbtree_destroy(vcb->tree);
        vcb->tree = 0;
    }

    arch_destroy_address_space(vcb->cr3);
    arch_paging_pool_free(vcb->cr3);
    vcb->cr3 = 0;
    spinlock_unlock(vcb->lock);
    spinlock_release(vcb->lock);
}

void* vmm_alloc_pages(vmm_control_block* vcb, u32 page_cnt, u32 flags)
{
    /* RING3 caller: route through the syscall gate (ring 0 executes the
     * privileged invlpg inside arch_map_4kb). */
    if (arch_running_ring3()) {
        vmm_syscall_data data = {0};
        data.cmd = VMM_CTRL_ALLOC_PAGES;
        data.vcb = vcb;
        data.page_cnt = page_cnt;
        data.flags = flags;
        arch_syscall(VMM_SYSCALL_MINOR, &data);
        return data.ret_va;
    }

    u32 pa = 0;
    u32 va = 0;
    vmm_region* region = 0;
    vmm_region* cur = 0;

    if (!vcb || !vcb->tree || page_cnt == 0)
        return 0;

    /* Allocate physical pages and the bookkeeping region up front,
     * outside the VCB lock (pmm/heap take their own locks). */
    pa = pmm_alloc_pages(page_cnt);
    if (!pa)
        return 0;

    region = kmalloc(sizeof(vmm_region));
    if (!region) {
        pmm_free_pages(pa, page_cnt);
        return 0;
    }

    if (spinlock_lock(vcb->lock) != 0) {
        pmm_free_pages(pa, page_cnt);
        kfree(region);
        return 0;
    }

    /* pick a free VA and reserve it atomically with the insert below */
    if (vcb->tree->root != vcb->tree->nil) {
        rbtree_for_each(node, vcb->tree) {
            cur = rb_entry(node, vmm_region, node);

            rbnode* next_node = rbtree_next(vcb->tree, &cur->node);
            if (next_node) {
                vmm_region* next_region = rb_entry(next_node, vmm_region, node);
                va = (u32)cur->start_va + cur->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (page_cnt > (0xffffffff - va) / PAGE_SIZE) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    pmm_free_pages(pa, page_cnt);
                    kfree(region);
                    return 0;
                }
                if (va + page_cnt * PAGE_SIZE <= (u32)next_region->start_va)
                    break;
            } else {
                if ((u32)cur->start_va > (0xffffffff - cur->size)) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    pmm_free_pages(pa, page_cnt);
                    kfree(region);
                    return 0;
                }

                va = (u32)cur->start_va + cur->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (va == 0 || page_cnt > (0xffffffff - va) / PAGE_SIZE) {
                    /* overflow after alignment */
                    spinlock_unlock(vcb->lock);
                    pmm_free_pages(pa, page_cnt);
                    kfree(region);
                    return 0;
                }
                break;
            }
        }
    }

    /* tree is empty — fall back to identity mapping */
    if (vcb->tree->root == vcb->tree->nil)
        va = pa;

    region->start_va = (void*)va;
    region->size     = PAGE_SIZE * page_cnt;
    region->flags    = flags;
    region->pa       = pa;
    region->own_phys = 1;
    rbtree_insert(vcb->tree, &region->node, vmm_rbtree_node_cmp);
    spinlock_unlock(vcb->lock);

    /* map pages after releasing the lock (arch_map_4kb takes paging_lock) */
    for (u32 i = 0; i < region->size / PAGE_SIZE; i++) {
        if (arch_map_4kb((void*)vcb->cr3, (void*)(va + i * PAGE_SIZE),
                         (void*)(pa + i * PAGE_SIZE), flags) != 0) {
            /* roll back: unmap pages mapped so far, free phys, drop region */
            for (u32 j = 0; j < i; j++)
                arch_unmap_4kb((void*)vcb->cr3, (void*)(va + j * PAGE_SIZE));
            spinlock_lock(vcb->lock);
            rbtree_delete(vcb->tree, &region->node);
            spinlock_unlock(vcb->lock);
            pmm_free_pages(pa, page_cnt);
            kfree(region);
            return 0;
        }
    }
    return (void*)va;
}

void vmm_free_pages(vmm_control_block* vcb, void* va)
{
    /* RING3 caller: route through the syscall gate (privileged invlpg). */
    if (arch_running_ring3()) {
        vmm_syscall_data data = {0};
        data.cmd = VMM_CTRL_FREE_PAGES;
        data.vcb = vcb;
        data.va = va;
        arch_syscall(VMM_SYSCALL_MINOR, &data);
        return;
    }

    rbnode* del_node = 0;   /* node captured inside the loop; the loop-scoped
                             * `node` goes out of scope after rbtree_for_each */
    vmm_region* region = 0;
    int found = 0;

    if (!vcb || !vcb->tree)
        return;

    if (spinlock_lock(vcb->lock) != 0)
        return;

    rbtree_for_each(node, vcb->tree) {
        region = rb_entry(node, vmm_region, node);

        if (((u32)region->start_va <= (u32)va) && 
            ((u32)region->start_va + region->size > (u32)va) &&
            region->own_phys) {
            found = 1;
            del_node = node;
            break;
        }
    }

    if (found) {
        for (u32 i = 0; i < region->size / PAGE_SIZE; i++)
            arch_unmap_4kb((void*)vcb->cr3,
                           (u8*)region->start_va + i * PAGE_SIZE);
        pmm_free_pages(region->pa, region->size / PAGE_SIZE);
        rbtree_delete(vcb->tree, del_node);
        kfree(region);
    }
    spinlock_unlock(vcb->lock);
}

static void* vmm_mmap_reserve(vmm_control_block* vcb, u32 pa, size_t size,
                                u32 flags, int* found)
{
    u32 va = 0;
    vmm_region* region = 0;

    if (!vcb || !vcb->tree || !found)
        return 0;

    if (spinlock_lock(vcb->lock) != 0)
        return 0;
    *found = 0;

    if (vcb->tree->root != vcb->tree->nil) {
        rbtree_for_each(node, vcb->tree) {
            region = rb_entry(node, vmm_region, node);

            rbnode* next_node = rbtree_next(vcb->tree, &region->node);
            if (next_node) {
                vmm_region* next_region = rb_entry(next_node, vmm_region, node);
                va = (u32)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (size > 0xffffffff - (u32)va) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }
                if (va + size <= (u32)next_region->start_va)
                    break;
            } else {
                if ((u32)region->start_va > (0xffffffff - region->size)) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }

                va = (u32)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (va == 0 || size > 0xffffffff - (u32)va) {
                    /* overflow after alignment */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }
                break;
            }
        }
    }

    if (vcb->tree->root == vcb->tree->nil)
        va = pa;

    region = kmalloc(sizeof(vmm_region));
    if (!region) {
        spinlock_unlock(vcb->lock);
        return 0;
    }

    region->start_va = (void*)va;
    region->size     = size;
    region->flags    = flags;
    region->pa       = pa;
    rbtree_insert(vcb->tree, &region->node, vmm_rbtree_node_cmp);
    *found = 1;
    spinlock_unlock(vcb->lock);

    return (void*)va;
}

static int vmm_mmap_release(vmm_control_block* vcb, void* va, u32 size,
                              void** out_start, u32* out_size)
{
    rbnode* del_node = 0;   /* node captured inside the loop; the loop-scoped
                             * `node` goes out of scope after rbtree_for_each */
    vmm_region* region = 0;
    int found = 0;

    if (!vcb || !vcb->tree)
        return EINVAL;

    if (spinlock_lock(vcb->lock) != 0)
        return EINVAL;

    rbtree_for_each(node, vcb->tree) {
        region = rb_entry(node, vmm_region, node);
        if (region->own_phys)
            continue;

        if (((u32)region->start_va <= (u32)va) &&
            ((u32)region->start_va + region->size > (u32)va)) {
            found = 1;
            del_node = node;
            break;
        }
    }

    if (found) {
        /*
         * A mapping is released as a whole.  Reject requests that extend
         * beyond the mapped region (or wrap around 32-bit), so a caller
         * can never silently unmap more than it asked for.
         */
        if (size > region->size ||
            (u32)va + size < (u32)va ||
            (u32)va + size > (u32)region->start_va + region->size) {
            spinlock_unlock(vcb->lock);
            return EINVAL;
        }

        if (out_start)
            *out_start = region->start_va;
        if (out_size)
            *out_size = region->size;
        rbtree_delete(vcb->tree, del_node);
        kfree(region);
    }
    spinlock_unlock(vcb->lock);

    return found ? 0 : EINVAL;
}

int vmm_lookup_region(pcb* proc, u32 va, u32* out_pa, u32* out_pa_size)
{
    vmm_region* region = 0;
    int found = 0;

    if (!proc || !out_pa || !out_pa_size)
        return EINVAL;

    if (spinlock_lock(proc->vcb.lock) != 0)
        return EINVAL;

    rbtree_for_each(node, proc->vcb.tree) {
        region = rb_entry(node, vmm_region, node);

        if (((u32)region->start_va <= (u32)va) &&
            ((u32)region->start_va + region->size > (u32)va)) {
            found = 1;
            *out_pa = region->pa;
            *out_pa_size = region->size;
            break;
        }
    }
    spinlock_unlock(proc->vcb.lock);

    return found ? 0 : EINVAL;
}

void* vmm_map_memory(pcb* proc, u32 phys_addr, size_t size, u32 flags)
{
    /* RING3 caller: route through the syscall gate (privileged invlpg). */
    if (arch_running_ring3()) {
        vmm_syscall_data data = {0};
        data.cmd = VMM_CTRL_MAP_MEMORY;
        data.proc = proc;
        data.phys_addr = phys_addr;
        data.size = size;
        data.flags = flags;
        arch_syscall(VMM_SYSCALL_MINOR, &data);
        return data.ret_va;
    }

    cap_mem mem = {phys_addr, size, flags};
    u32 aligned_pa = phys_addr & ~(PAGE_SIZE - 1);
    u32 offset = phys_addr - aligned_pa;
    size_t aligned_sz = 0;
    void* va = 0;
    int found = 0;

    if (!proc)
        return VMM_ERR_PTR(EINVAL);

    if (cap_check(proc, CAP_MAP_MEM, &mem) != 0)
        return VMM_ERR_PTR(EPERM);

    /* reject empty mappings and 32-bit overflow of the aligned size */
    if (size == 0 || size > (size_t)-1 - offset - (PAGE_SIZE - 1))
        return VMM_ERR_PTR(EINVAL);

    aligned_sz = (size + offset + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);

    va = vmm_mmap_reserve(&proc->vcb, aligned_pa, aligned_sz, flags, &found);
    if (!found)
        return VMM_ERR_PTR(ENOMEM);

    for (u32 i = 0; i < aligned_sz / PAGE_SIZE; i++) {
        if (arch_map_4kb((void*)proc->vcb.cr3, (void*)(va + i * PAGE_SIZE),
                         (void*)(aligned_pa + i * PAGE_SIZE), flags) != 0) {
            /* roll back: unmap pages mapped so far, drop the reserved region */
            for (u32 j = 0; j < i; j++)
                arch_unmap_4kb((void*)proc->vcb.cr3, (void*)(va + j * PAGE_SIZE));
            vmm_mmap_release(&proc->vcb, va, 0, 0, 0);
            return VMM_ERR_PTR(ENOMEM);
        }
    }

    return (void*)((u8*)va + offset);
}

int vmm_unmap_memory(pcb* proc, void* virt_addr, size_t size)
{
    /* RING3 caller: route through the syscall gate (privileged invlpg). */
    if (arch_running_ring3()) {
        vmm_syscall_data data = {0};
        data.cmd = VMM_CTRL_UNMAP_MEMORY;
        data.proc = proc;
        data.va = virt_addr;
        data.size = size;
        arch_syscall(VMM_SYSCALL_MINOR, &data);
        return data.ret;
    }

    cap_mem mem = {0};
    u32 pa = 0;
    u32 pa_size = 0;
    u32 region_size = 0;
    void* va = 0;
    int ret = 0;

    if (!proc)
        return EINVAL;

    ret = vmm_lookup_region(proc, (u32)virt_addr, &pa, &pa_size);
    if (ret)
        return E_NOTFOUND;

    mem.base = pa;
    mem.size = pa_size;
    mem.flags = 0;
    if (cap_check(proc, CAP_MAP_MEM, &mem) != 0)
        return EPERM;

    /*
     * Only unmap when a matching mmap region actually exists.  The
     * requested range is validated against the region inside
     * vmm_mmap_release so a bad size cannot silently unmap more.
     */
    if (vmm_mmap_release(&proc->vcb, virt_addr, (u32)size,
                         &va, &region_size) != 0)
        return EINVAL;

    for (u32 i = 0; i < region_size / PAGE_SIZE; i++)
        arch_unmap_4kb((void*)proc->vcb.cr3,
                       (u8*)va + i * PAGE_SIZE);

    return 0;
}

u32 vmm_va_to_pa(pcb* proc, u32 va)
{
    u32 pa = 0;
    if (!proc || !proc->vcb.tree)
        return 0;

    spinlock_lock(proc->vcb.lock);
    rbnode* node = rbtree_search(proc->vcb.tree, (void*)va, vmm_rbtree_key_cmp);
    if (node) {
        vmm_region* region = rb_entry(node, vmm_region, node);
        pa = region->pa + ((u32)va - (u32)region->start_va);
    }
    spinlock_unlock(proc->vcb.lock);

    return pa;
}

/*
 * ============================================================================
 * VMM syscall layer (RING3)
 *
 * vmm_alloc_pages / vmm_free_pages / vmm_map_memory / vmm_unmap_memory are
 * routed through this gate (major 100, minor VMM_SYSCALL_MINOR) whenever the
 * caller runs in user mode (CPL3).  The handler runs in kernel context, so
 * the privileged invlpg inside arch_map_4kb / arch_unmap_4kb is executed at
 * ring 0, and the capability checks inside the kernel implementations still
 * apply to the calling process.
 * ============================================================================
 */
static irq* vmm_scall = 0;

static void vmm_syscall_isr(void* context)
{
    vmm_syscall_data* data = (vmm_syscall_data*)context;
    if (!data)
        return;

    switch (data->cmd) {
    case VMM_CTRL_ALLOC_PAGES: {
        vmm_control_block* vcb = data->vcb;
        if (!vcb) {
            pcb* proc = get_current_process();
            if (!proc) { data->ret = EINVAL; break; }
            vcb = &proc->vcb;
        }
        data->ret_va = vmm_alloc_pages(vcb, data->page_cnt, data->flags);
        data->ret = data->ret_va ? 0 : ENOMEM;
        break;
    }
    case VMM_CTRL_FREE_PAGES: {
        vmm_control_block* vcb = data->vcb;
        if (!vcb) {
            pcb* proc = get_current_process();
            if (!proc) { data->ret = EINVAL; break; }
            vcb = &proc->vcb;
        }
        vmm_free_pages(vcb, data->va);
        data->ret = 0;
        break;
    }
    case VMM_CTRL_MAP_MEMORY: {
        pcb* proc = data->proc;
        if (!proc)
            proc = get_current_process();
        data->ret_va = vmm_map_memory(proc, data->phys_addr, data->size,
                                      data->flags);
        data->ret = VMM_IS_ERR(data->ret_va) ? VMM_PTR_ERR(data->ret_va) : 0;
        break;
    }
    case VMM_CTRL_UNMAP_MEMORY: {
        pcb* proc = data->proc;
        if (!proc)
            proc = get_current_process();
        data->ret = vmm_unmap_memory(proc, data->va, data->size);
        break;
    }
    default:
        data->ret = EINVAL;
        break;
    }
}

void vmm_syscall_init(void)
{
    int ret = irq_request(&vmm_scall, "vmm_syscall", 100,
                          VMM_SYSCALL_MINOR, vmm_syscall_isr, 0);
    if (ret == 0 && vmm_scall)
        irq_unmask(vmm_scall);
}

void vmm_syscall_exit(void)
{
    if (vmm_scall) {
        irq_mask(vmm_scall);
        irq_release(vmm_scall);
        vmm_scall = 0;
    }
}

module_init(vmm_syscall_init);
module_exit(vmm_syscall_exit);