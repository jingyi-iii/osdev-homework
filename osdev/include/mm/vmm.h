#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "mm/paging.h"
#include "lib/rbtree.h"

typedef struct vmm_control_block {
    rbtree*     tree;
    uint32_t    cr3;
} vmm_control_block;

typedef struct vmm_region {
    rbnode      node;
    void*       start_va;
    uint32_t    size;
    uint32_t    flags;
} vmm_region;

void vmm_switch(vmm_control_block* vcb);
int vmm_create(vmm_control_block* vcb, int user_accessible);
void vmm_destroy(vmm_control_block* vcb);

uint32_t vmm_alloc_region(vmm_control_block* vcb, uint32_t size, uint32_t flags);
int vmm_free_region(vmm_control_block* vcb, uint32_t start_va);

#endif /* VMM_H */
