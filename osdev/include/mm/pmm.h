#ifndef PMM_H
#define PMM_H

#include <stdint.h>

void pmm_init(uint32_t total_memory, uint8_t* bitmap_pa);

/**
 * pmm_alloc_page - Allocate a single zero-filled 4KB physical page.
 * Returns the physical address, or 0 on failure.
 */
uint32_t pmm_alloc_page(void);
uint32_t pmm_alloc_pages(uint32_t num_pages);

/**
 * pmm_free_page - Free a single 4KB physical page.
 */
void pmm_free_page(uint32_t paddr);
void pmm_free_pages(uint32_t paddr, uint32_t num_pages);

/**
 * pmm_get_free_page_count - Return the number of free pages (for debugging).
 */
uint32_t pmm_get_free_page_count(void);

#endif /* PMM_H */
