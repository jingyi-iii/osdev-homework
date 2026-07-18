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

uint32_t vmm_alloc_region(vmm_control_block* vcb, uint32_t size, uint32_t flags)
{
    if (!vcb || !vcb->tree || size == 0)
        return E_INVAL;

    return 0;
}

int vmm_free_region(vmm_control_block* vcb, uint32_t start_va)
{
    if (!vcb || !vcb->tree)
        return E_INVAL;

    return 0;
}
