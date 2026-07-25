#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "drivers/log_driver.h"

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

static int vmm_rbtree_key_cmp(const void* key, const rbnode* node)
{
    if (!key || !node)
        return -1;

    vmm_region* region = rb_entry(node, vmm_region, node);
    void* va = (void*)key;

    if ((uint32_t)va < (uint32_t)region->start_va)
        return -1;
    else if ((uint32_t)va > (uint32_t)region->start_va)
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
            pmm_free_pages(r->pa, r->size / PAGE_SIZE);
            rbtree_delete(vcb->tree, pos);
            kfree(r);
        }
        rbtree_destroy(vcb->tree);
        vcb->tree = 0;
        spinlock_unlock(vcb->lock);
    }

    arch_destroy_address_space(vcb->cr3);
    pmm_free_pages(vcb->cr3, 1);
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
                if (va + page_cnt * PAGE_SIZE < va) {
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
                if (va == 0 || va + page_cnt * PAGE_SIZE < va) {
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
    if (va == 0)
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
    rbtree_insert(vcb->tree, &region->node, vmm_rbtree_node_cmp);

    for (uint32_t i = 0; i < region->size / PAGE_SIZE; i++)
        arch_map_4kb((void*)vcb->cr3, (void*)(va + i * PAGE_SIZE),
                     (void*)(pa + i * PAGE_SIZE), flags);
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
            ((uint32_t)region->start_va + region->size > (uint32_t)va)) {
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
