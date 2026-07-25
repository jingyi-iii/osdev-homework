#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "mm/paging.h"
#include "lib/rbtree.h"
#include "sync/spinlock.h"

typedef struct vmm_control_block {
    rbtree*     tree;
    uint32_t    cr3;
    spinlock*   lock;
} vmm_control_block;

typedef struct vmm_region {
    rbnode      node;
    void*       start_va;
    uint32_t    size;
    uint32_t    flags;
    uint32_t    pa;             /* physical address for pmm_free_page */
} vmm_region;

void vmm_switch(vmm_control_block* vcb);
int vmm_create(vmm_control_block* vcb, int user_accessible);
void vmm_destroy(vmm_control_block* vcb);

void* vmm_alloc_pages(vmm_control_block* vcb, uint32_t page_cnt, uint32_t flags);
void vmm_free_pages(vmm_control_block* vcb, void* va);

#endif /* VMM_H */
