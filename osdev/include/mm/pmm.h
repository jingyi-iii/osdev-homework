#ifndef PMM_H
#define PMM_H

#include "lib/types.h"

void pmm_init(u32 total_memory, u8* bitmap_pa);

/**
 * pmm_mark_used - Reserve a physical range so the allocator never hands
 * it out (e.g. GRUB multiboot module images loaded above the PMM bitmap).
 */
void pmm_mark_used(u32 paddr, u32 size);

/**
 * pmm_alloc_page - Allocate a single zero-filled 4KB physical page.
 * Returns the physical address, or 0 on failure.
 */
u32 pmm_alloc_page(void);
u32 pmm_alloc_pages(u32 num_pages);

/**
 * pmm_free_page - Free a single 4KB physical page.
 */
void pmm_free_page(u32 paddr);
void pmm_free_pages(u32 paddr, u32 num_pages);

/**
 * pmm_get_free_page_count - Return the number of free pages (for debugging).
 */
u32 pmm_get_free_page_count(void);

#endif /* PMM_H */
