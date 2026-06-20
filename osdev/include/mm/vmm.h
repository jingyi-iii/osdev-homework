#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include "mm/paging.h"

/**
 * vmm_get_kernel_pdir - Return the physical address of the kernel's
 * master page directory.
 */
uint32_t vmm_get_kernel_pdir(void);

/**
 * vmm_load_cr3 - Load a page directory into CR3.
 * This automatically flushes the TLB for non-global pages.
 */
void vmm_load_cr3(uint32_t pdir_phys);

/**
 * vmm_get_cr3 - Return the current CR3 value.
 */
uint32_t vmm_get_cr3(void);

/**
 * vmm_create_address_space - Create a new user address space.
 *
 * Allocates a new page directory and copies the kernel-space PDEs
 * (KERNEL_BASE_VADDR..4GB) from the kernel master page directory.
 * User-space PDEs (0..KERNEL_BASE_VADDR) are zero-initialised.
 *
 * Returns the physical address of the new page directory, or 0 on failure.
 */
uint32_t vmm_create_address_space(void);

uint32_t vmm_clone_kernel_pde(void);

/**
 * vmm_destroy_address_space - Destroy a user address space.
 *
 * Frees all user page tables and the pages they reference, then
 * frees the page directory itself.  Kernel-shared page tables are
 * left untouched.
 */
void vmm_destroy_address_space(uint32_t pdir_phys);

/**
 * vmm_map_page - Map a single virtual page to a physical page.
 *
 * @pdir_phys:  Physical address of the target page directory.
 * @vaddr:      Virtual address to map (will be page-aligned).
 * @paddr:      Physical address to map to (will be page-aligned).
 * @flags:      PTE flags (PTE_KERNEL, PTE_USER_PAGE, etc.).
 *
 * Returns 0 on success, -1 if a page table allocation fails.
 */
int vmm_map_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
                 uint32_t flags);

/**
 * vmm_unmap_page - Unmap a single virtual page (also frees the physical page).
 */
void vmm_unmap_page(uint32_t pdir_phys, uint32_t vaddr);

/**
 * vmm_map_range - Map a contiguous range.
 * Returns 0 on success, -1 on failure.
 */
int vmm_map_range(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
                  uint32_t size, uint32_t flags);

/**
 * vmm_virt_to_phys - Translate a virtual address to physical.
 * @pdir_phys: Page directory to use (0 = use current CR3).
 */
uint32_t vmm_virt_to_phys(uint32_t pdir_phys, uint32_t vaddr);

/**
 * vmm_alloc_user_page - Allocate a physical page and map it into the
 * given address space at the specified user virtual address.
 * Returns the virtual address on success, 0 on failure.
 */
void* vmm_alloc_user_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t flags);

/**
 * vmm_enable_paging - Set CR0.PG, enabling paging.
 * Must be called with the initial page directory already built and
 * loaded into CR3.
 */
void vmm_enable_paging(void);

/**
 * vmm_set_kernel_pdir - Register the kernel master page directory.
 * Called once by arch_paging_init() after the bootstrap PD is built.
 */
void vmm_set_kernel_pdir(uint32_t pdir_phys);

/**
 * arch_paging_init - Architecture-specific paging bootstrap.
 *
 * Builds the initial kernel page directory (identity-mapping all
 * physical memory + higher-half kernel mapping at 3GB), initialises
 * the PMM, and enables paging.
 *
 * @total_memory:  Total physical memory in bytes.
 * @reserved_end:  Physical address of the first byte AFTER all
 *                 bootstrap structures (PD, PT0, kernel BSS end).
 */
void arch_paging_init(uint32_t total_memory, uint32_t reserved_end);

#endif /* VMM_H */
