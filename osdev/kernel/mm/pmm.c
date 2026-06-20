#include "mm/pmm.h"
#include "mm/paging.h"
#include "lib/string.h"
#include "drivers/log_driver.h"
#include "sync/spinlock.h"

/*
 * Physical Memory Manager — bitmap-based allocator.
 *
 * Physical memory layout (example with 64 MB):
 *   0x00000000 - 0x000FFFFF   Reserved (BIOS, IVT, BDA, EBDA, etc.)
 *   0x00100000 - 0x001?????   Kernel image (code + rodata + data + bss)
 *   0x00?????? - 0x00??????   Bootstrap page structures (PD + PT0)
 *   0x00?????? - 0x00??????   Bitmap (this allocator's metadata)
 *   0x00?????? - 0x03FFFFFF   Free pages
 */

/* One bit per 4KB page: 1 = used, 0 = free */
static uint8_t*  bitmap = 0;
static uint32_t  total_blocks = 0;
static uint32_t  free_blocks = 0;
static spinlock* pmm_lock = 0;
static int       pmm_initialized = 0;

static inline void bit_set(uint32_t block)
{
    bitmap[block / 8] |= (uint8_t)(1U << (block % 8));
}

static inline void bit_clear(uint32_t block)
{
    bitmap[block / 8] &= (uint8_t)(~(1U << (block % 8)));
}

static inline int bit_test(uint32_t block)
{
    return (bitmap[block / 8] >> (block % 8)) & 1;
}

void pmm_init(uint32_t total_memory, uint8_t* bitmap_pa)
{
    uint32_t first_free_block = 0;

    if (!bitmap_pa) {
        KLOG("pmm_init: bitmap_pa is NULL");
        return;
    }

    pmm_lock = spinlock_alloc();
    if (!pmm_lock) {
        KLOG("pmm_init: failed to allocate PMM spinlock");
        return;
    }

    spinlock_lock(pmm_lock);
    total_blocks = total_memory / PAGE_SIZE;
    bitmap = (uint8_t*)PAGE_ALIGN(bitmap_pa);

    /* Mark all blocks as used initially */
    memset(bitmap, 0xFF, (total_blocks + 7) / 8);

    free_blocks = 0;
    first_free_block = (uint32_t)bitmap / PAGE_SIZE;
    first_free_block += (((total_blocks + 7) / 8) + PAGE_SIZE - 1) / PAGE_SIZE;  /* skip bitmap blocks */
    
    for (size_t i = first_free_block; i < total_blocks; i++) {
        bit_clear(i);
        free_blocks++;
    }
    spinlock_unlock(pmm_lock);
    pmm_initialized = 1;

    KLOG("PMM: total %u pages (%u MB), %u pages free, bitmap at 0x%x",
         total_blocks, total_memory >> 20, free_blocks, (uint32_t)bitmap);
}

uint32_t pmm_alloc_page(void)
{
    uint32_t i = 0;

    if (!pmm_initialized)
        return 0;

    spinlock_lock(pmm_lock);
    for (i = 0; i < total_blocks; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            free_blocks--;
            /*
             * Zero the page.  The page is currently identity-mapped so we
             * can just use memset on the physical address directly.
             */
            memset((void*)(i * PAGE_SIZE), 0, PAGE_SIZE);
            spinlock_unlock(pmm_lock);
            return i * PAGE_SIZE;
        }
    }
    spinlock_unlock(pmm_lock);

    KLOG("PMM: out of memory!");
    return 0;
}

void pmm_free_page(uint32_t paddr)
{
    uint32_t block = paddr / PAGE_SIZE;
    if (!pmm_initialized || block >= total_blocks)
        return;

    spinlock_lock(pmm_lock);
    if (bit_test(block)) {
        bit_clear(block);
        free_blocks++;
    }
    spinlock_unlock(pmm_lock);
}

uint32_t pmm_get_free_page_count(void)
{
    uint32_t blocks = 0;

    if (!pmm_initialized)
        return 0;

    spinlock_lock(pmm_lock);
    blocks = free_blocks;
    spinlock_unlock(pmm_lock);

    return blocks;
}
