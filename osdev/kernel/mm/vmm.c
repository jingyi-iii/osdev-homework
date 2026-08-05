#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "drivers/log_driver.h"
#include "kernel/process.h"

static int vmm_rbtree_node_cmp(const rbnode* left, const rbnode* right)
{
    if (!left || !right)
        return -1;

    vmm_region* l = rb_entry(left, vmm_region, node);
    vmm_region* r = rb_entry(right, vmm_region, node);

    if ((uint32_t)l->start_va < (uint32_t)r->start_va)
        return -1;
    else if ((uint32_t)l->start_va > (uint32_t)r->start_va)
        return 1;
    else
        return 0;
}

void vmm_switch(vmm_control_block* vcb)
{
    if (!vcb || !vcb->cr3)
        return;

    arch_load_cr3(vcb->cr3);
}

int vmm_create(vmm_control_block* vcb, int user_accessible)
{
    uint32_t pa = 0;

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

    pa = pmm_alloc_page();
    if (!pa) {
        rbtree_destroy(vcb->tree);
        spinlock_release(vcb->lock);
        return ENOMEM;
    }

    vcb->cr3 = arch_clone_kernel_pde(pa, user_accessible);
    if (!vcb->cr3) {
        pmm_free_page(pa);
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

    if (vcb->tree) {
        spinlock_lock(vcb->lock);
        rbnode *pos, *n;
        rbtree_for_each_safe(pos, n, vcb->tree) {
            vmm_region* r = rb_entry(pos, vmm_region, node);
            for (uint32_t i = 0; i < r->size / PAGE_SIZE; i++)
                arch_unmap_4kb((void*)vcb->cr3,
                               (uint8_t*)r->start_va + i * PAGE_SIZE);
            if (r->own_phys)
                pmm_free_pages(r->pa, r->size / PAGE_SIZE);
            rbtree_delete(vcb->tree, pos);
            kfree(r);
        }
        rbtree_destroy(vcb->tree);
        vcb->tree = 0;
        spinlock_unlock(vcb->lock);
    }

    arch_destroy_address_space(vcb->cr3);
    pmm_free_page(vcb->cr3);
    vcb->cr3 = 0;
    spinlock_release(vcb->lock);
}

void* vmm_alloc_pages(vmm_control_block* vcb, uint32_t page_cnt, uint32_t flags)
{
    uint32_t pa = 0;
    uint32_t va = 0;
    vmm_region* region = 0;

    if (!vcb || !vcb->tree)
        return 0;

    if (spinlock_lock(vcb->lock) != 0)
        return 0;
    if (vcb->tree->root != vcb->tree->nil) {
        rbtree_for_each(node, vcb->tree) {
            region = rb_entry(node, vmm_region, node);

            rbnode* next_node = rbtree_next(vcb->tree, &region->node);
            if (next_node) {
                vmm_region* next_region = rb_entry(next_node, vmm_region, node);
                va = (uint32_t)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (page_cnt > (0xffffffff - va) / PAGE_SIZE) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }
                if (va + page_cnt * PAGE_SIZE <= (uint32_t)next_region->start_va)
                    break;
            } else {
                if ((uint32_t)region->start_va > (0xffffffff - region->size)) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }

                va = (uint32_t)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (va == 0 || page_cnt > (0xffffffff - va) / PAGE_SIZE) {
                    /* overflow after alignment */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }
                break;
            }
        }
    }

    pa = pmm_alloc_pages(page_cnt);
    if (!pa) {
        spinlock_unlock(vcb->lock);
        return 0;
    }

    /* tree is empty — fall back to identity mapping */
    if (vcb->tree->root == vcb->tree->nil)
        va = pa;

    region = kmalloc(sizeof(vmm_region));
    if (!region) {
        pmm_free_pages(pa, page_cnt);
        spinlock_unlock(vcb->lock);
        return 0;
    }

    region->start_va = (void*)va;
    region->size     = PAGE_SIZE * page_cnt;
    region->flags    = flags;
    region->pa       = pa;
    region->own_phys = 1;
    rbtree_insert(vcb->tree, &region->node, vmm_rbtree_node_cmp);

    for (uint32_t i = 0; i < region->size / PAGE_SIZE; i++) {
        if (arch_map_4kb((void*)vcb->cr3, (void*)(va + i * PAGE_SIZE),
                         (void*)(pa + i * PAGE_SIZE), flags) != 0) {
            /* roll back: unmap pages mapped so far, free phys, drop region */
            for (uint32_t j = 0; j < i; j++)
                arch_unmap_4kb((void*)vcb->cr3, (void*)(va + j * PAGE_SIZE));
            pmm_free_pages(pa, page_cnt);
            rbtree_delete(vcb->tree, &region->node);
            kfree(region);
            spinlock_unlock(vcb->lock);
            return 0;
        }
    }
    spinlock_unlock(vcb->lock);
    return (void*)va;
}

void vmm_free_pages(vmm_control_block* vcb, void* va)
{
    rbnode* node = 0;
    vmm_region* region = 0;
    int found = 0;

    if (!vcb || !vcb->tree)
        return;

    if (spinlock_lock(vcb->lock) != 0)
        return;

    rbtree_for_each(node, vcb->tree) {
        region = rb_entry(node, vmm_region, node);

        if (((uint32_t)region->start_va <= (uint32_t)va) && 
            ((uint32_t)region->start_va + region->size > (uint32_t)va) &&
            region->own_phys) {
            found = 1;
            break;
        }
    }

    if (found) {
        for (uint32_t i = 0; i < region->size / PAGE_SIZE; i++)
            arch_unmap_4kb((void*)vcb->cr3,
                           (uint8_t*)region->start_va + i * PAGE_SIZE);
        pmm_free_pages(region->pa, region->size / PAGE_SIZE);
        rbtree_delete(vcb->tree, node);
        kfree(region);
    }
    spinlock_unlock(vcb->lock);
}

static void* vmm_mmap_reserve(vmm_control_block* vcb, uint32_t pa, size_t size,
                                uint32_t flags, int* found)
{
    uint32_t va = 0;
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
                va = (uint32_t)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (size > 0xffffffff - (uint32_t)va) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }
                if (va + size <= (uint32_t)next_region->start_va)
                    break;
            } else {
                if ((uint32_t)region->start_va > (0xffffffff - region->size)) {
                    /* overflow */
                    spinlock_unlock(vcb->lock);
                    return 0;
                }

                va = (uint32_t)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (va == 0 || size > 0xffffffff - (uint32_t)va) {
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

static int vmm_mmap_release(vmm_control_block* vcb, void* va,
                              void** out_start, uint32_t* out_size)
{
    rbnode* node = 0;
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

        if (((uint32_t)region->start_va <= (uint32_t)va) &&
            ((uint32_t)region->start_va + region->size > (uint32_t)va)) {
            found = 1;
            break;
        }
    }

    if (found) {
        if (out_start)
            *out_start = region->start_va;
        if (out_size)
            *out_size = region->size;
        rbtree_delete(vcb->tree, node);
        kfree(region);
    }
    spinlock_unlock(vcb->lock);

    return found ? 0 : EINVAL;
}

void* vmm_map_memory(uint32_t phys_addr, size_t size, uint32_t flags)
{
    cap_mem mem = {phys_addr, size, flags};
    uint32_t aligned_pa = phys_addr & ~(PAGE_SIZE - 1);
    uint32_t offset = phys_addr - aligned_pa;
    size_t aligned_sz = (size + offset + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
    void* va = 0;
    int found = 0;

    pcb* proc = get_current_process();
    if (!proc)
        return (void*)(intptr_t)(EINVAL);

    if (cap_check(proc, CAP_MEM_MAP, &mem) != 0)
        return (void*)(intptr_t)(EPERM);

    va = vmm_mmap_reserve(&proc->vcb, aligned_pa, aligned_sz, flags, &found);
    if (!found)
        return (void*)(intptr_t)(ENOMEM);

    for (uint32_t i = 0; i < aligned_sz / PAGE_SIZE; i++) {
        if (arch_map_4kb((void*)proc->vcb.cr3, (void*)(va + i * PAGE_SIZE),
                         (void*)(aligned_pa + i * PAGE_SIZE), flags) != 0) {
            /* roll back: unmap pages mapped so far, drop the reserved region */
            for (uint32_t j = 0; j < i; j++)
                arch_unmap_4kb((void*)proc->vcb.cr3, (void*)(va + j * PAGE_SIZE));
            vmm_mmap_release(&proc->vcb, va, 0, 0);
            return (void*)(intptr_t)(ENOMEM);
        }
    }

    return (void*)((uint8_t*)va + offset);
}

int vmm_unmap_memory(void* virt_addr, size_t size)
{
    cap_mem mem = {(uint32_t)virt_addr, size};
    void* va = 0;
    uint32_t region_size = 0;

    pcb* proc = get_current_process();
    if (!proc)
        return EINVAL;

    if (cap_check(proc, CAP_MEM_MAP, &mem) != 0)
        return EPERM;

    /* only unmap when a matching mmap region actually exists */
    if (vmm_mmap_release(&proc->vcb, virt_addr, &va, &region_size) != 0)
        return EINVAL;

    for (uint32_t i = 0; i < region_size / PAGE_SIZE; i++)
        arch_unmap_4kb((void*)proc->vcb.cr3,
                       (uint8_t*)va + i * PAGE_SIZE);

    return 0;
}