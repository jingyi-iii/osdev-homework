#ifndef PMM_H
#define PMM_H

#include <stdint.h>

/**
 * pmm_init - Initialize the physical memory manager.
 *
 * @total_memory:       Total physical memory in bytes.
 * @kernel_end_phys:    Physical address just after the kernel image (BSS end).
 *                      Everything below this is marked as used.
 * @kernel_pd_phys:     Physical address of the kernel's master page directory
 *                      (must be preserved, not handed out by the allocator).
 * @kernel_pt0_phys:    Physical address of the kernel's first page table
 *                      (must be preserved as well).
 */
void pmm_init(uint32_t total_memory, uint32_t kernel_end_phys,
              uint32_t kernel_pd_phys, uint32_t kernel_pt0_phys);

/**
 * pmm_alloc_page - Allocate a single zero-filled 4KB physical page.
 * Returns the physical address, or 0 on failure.
 */
uint32_t pmm_alloc_page(void);

/**
 * pmm_free_page - Free a single 4KB physical page.
 */
void pmm_free_page(uint32_t paddr);

/**
 * pmm_mark_used_range - Mark a physical range as used (never return to pool).
 */
void pmm_mark_used_range(uint32_t paddr, uint32_t size);

/**
 * pmm_free_page_count - Return the number of free pages (for debugging).
 */
uint32_t pmm_free_page_count(void);

#endif /* PMM_H */
