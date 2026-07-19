#include "mm/vmm.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "drivers/log_driver.h"

void vmm_switch(vmm_control_block* vcb)
{
    if (!vcb || !vcb->cr3)
        return;

    arch_load_cr3(vcb->cr3);
}

int vmm_create(vmm_control_block* vcb, int user_accessible)
{
    if (!vcb)
        return E_INVAL;

    vcb->tree = rbtree_create();
    if (!vcb->tree)
        return ENOMEM;

    vcb->cr3 = arch_clone_kernel_pde(pmm_alloc_page(), user_accessible);
    if (!vcb->cr3)
        return ENOMEM;

    return 0;
}

void vmm_destroy(vmm_control_block* vcb)
{
    if (!vcb || !vcb->cr3)
        return;

    arch_destroy_address_space(vcb->cr3);
    pmm_free_page(vcb->cr3);
}

void* vmm_alloc_page(uint32_t flags)
{
    uint32_t pa = 0;
    uint32_t va = 0;

    // if (!vcb || !vcb->tree)
    //     return E_INVAL;

    pa = pmm_alloc_page();
    if (!pa)
        return 0;

    va = pa;
    arch_map_4kb((void*)va, (void*)pa, flags);
    return (void*)va;
}

int vmm_free_page(void* va)
{
    // if (!vcb || !vcb->tree)
    //     return E_INVAL;

    arch_unmap_4kb((void*)va);
    return 0;
}
