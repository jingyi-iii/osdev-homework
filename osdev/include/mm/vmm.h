#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "mm/paging.h"
#include "lib/rbtree.h"

typedef struct vmm_control_block {
    rbtree*     tree;
    uint32_t    cr3;
} vmm_control_block;

void vmm_switch(vmm_control_block* vcb);
int vmm_create(vmm_control_block* vcb, int user_accessible);
void vmm_destroy(vmm_control_block* vcb);

#endif /* VMM_H */
