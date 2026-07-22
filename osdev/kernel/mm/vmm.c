#include "mm/vmm.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "drivers/log_driver.h"

static int vmm_rbtree_node_cmp(const rbnode* left, const rbnode* right)
{
    if (!left || !right)
        return 0;

    vmm_region* l = rb_entry(left, vmm_region, node);
    vmm_region* r = rb_entry(right, vmm_region, node);

    if (l->start_va < r->start_va)
        return -1;
    else if (l->start_va > r->start_va)
        return 1;
    else
        return 0;
}

static int vmm_rbtree_key_cmp(const void* key, const rbnode* node)
{
    if (!key || !node)
        return 0;

    vmm_region* region = rb_entry(node, vmm_region, node);
    void* va = (void*)key;

    if (va < region->start_va)
        return -1;
    else if (va > region->start_va)
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

    vcb->tree = rbtree_create();
    if (!vcb->tree)
        return ENOMEM;

    pa = pmm_alloc_page();
    if (!pa) {
        rbtree_destroy(vcb->tree);
        return ENOMEM;
    }

    vcb->cr3 = arch_clone_kernel_pde(pa, user_accessible);
    if (!vcb->cr3) {
        pmm_free_page(pa);
        rbtree_destroy(vcb->tree);
        return ENOMEM;
    }

    return 0;
}

void vmm_destroy(vmm_control_block* vcb)
{
    if (!vcb || !vcb->cr3)
        return;

    if (vcb->tree) {
        rbnode *pos, *n;
        rbtree_for_each_safe(pos, n, vcb->tree) {
            vmm_region* r = rb_entry(pos, vmm_region, node);
            arch_unmap_4kb(r->start_va);
            pmm_free_pages(r->pa, r->size / PAGE_SIZE);
            rbtree_delete(vcb->tree, pos);
            kfree(r);
        }
        rbtree_destroy(vcb->tree);
    }

    arch_destroy_address_space(vcb->cr3);
    pmm_free_pages(vcb->cr3, 1);
}

void* vmm_alloc_pages(vmm_control_block* vcb, uint32_t page_cnt, uint32_t flags)
{
    uint32_t pa = 0;
    uint32_t va = 0;
    vmm_region* region = 0;

    if (!vcb || !vcb->tree)
        return 0;

    if (vcb->tree->root != vcb->tree->nil) {
        rbtree_for_each(node, vcb->tree) {
            region = rb_entry(node, vmm_region, node);
            if (!region)
                continue;

            rbnode* next_node = rbtree_next(vcb->tree, &region->node);
            if (next_node != vcb->tree->nil) {
                vmm_region* next_region = rb_entry(next_node, vmm_region, node);
                va = (uint32_t)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                if (va + PAGE_SIZE <= (uint32_t)next_region->start_va)
                    break;
            } else {
                va = (uint32_t)region->start_va + region->size;
                va = (va + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                break;
            }
        }
    }

    pa = pmm_alloc_pages(page_cnt);
    if (!pa) {
        return 0;
    }

    /* tree is empty — fall back to identity mapping */
    if (va == 0)
        va = pa;

    region = kmalloc(sizeof(vmm_region));
    if (!region) {
        pmm_free_pages(pa, page_cnt);
        return 0;
    }

    region->start_va = (void*)va;
    region->size     = PAGE_SIZE * page_cnt;
    region->flags    = flags;
    region->pa       = pa;
    rbtree_insert(vcb->tree, &region->node, vmm_rbtree_node_cmp);

    arch_map_4kb((void*)va, (void*)pa, flags);
    return (void*)va;
}

int vmm_free_page(vmm_control_block* vcb, void* va)
{
    rbnode* node = 0;
    vmm_region* region = 0;

    if (!vcb || !vcb->tree)
        return E_INVAL;

    node = rbtree_search(vcb->tree, va, vmm_rbtree_key_cmp);
    if (node == vcb->tree->nil)
        return E_NOTFOUND;

    region = rb_entry(node, vmm_region, node);
    if (region) {
        arch_unmap_4kb(region->start_va);
        pmm_free_pages(region->pa, region->size / PAGE_SIZE);
        rbtree_delete(vcb->tree, node);
        kfree(region);
    }

    return 0;
}
