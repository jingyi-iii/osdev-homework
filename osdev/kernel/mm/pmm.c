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

#define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))

/* One bit per 4KB page: 1 = used, 0 = free */
static const uint32_t   block_size      = 4096;  /* 4KB pages */
static uint8_t*         bitmap_4k       = 0;
static uint32_t         total_blocks    = 0;
static uint32_t         free_blocks     = 0;
static spinlock*        pmm_lock        = 0;
static int              pmm_initialized = 0;

static inline void bitmap_set(uint32_t block)
{
    /*
     * 8 blocks per byte, so divide by 8 to get the byte index,
     * and use modulo 8 to get the bit index within that byte.
     */
    bitmap_4k[block / 8] |= (uint8_t)(1U << (block % 8));
}

static inline void bitmap_clear(uint32_t block)
{
    bitmap_4k[block / 8] &= (uint8_t)(~(1U << (block % 8)));
}

static inline int bitmap_test(uint32_t block)
{
    return (bitmap_4k[block / 8] >> (block % 8)) & 1;
}

void pmm_init(uint32_t total_memory, uint8_t* bitmap_pa)
{
    uint32_t reserve_blocks = 0;
    uint32_t first_bitmap_block = 0;

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

    /*
     * Build bitmap for 4KB pages.
     * The bitmap itself is stored after the kernel image + page tables.
     * Strategy: mark ALL blocks as used first, then only release pages
     * after the bitmap. Everything before (BIOS, kernel, page tables,
     * bitmap itself) stays protected.
     */
    total_blocks = total_memory / block_size;
    bitmap_4k = (uint8_t*)PAGE_ALIGN(bitmap_pa);

    /* Mark all blocks as used */
    memset(bitmap_4k, 0xFF, DIV_ROUND_UP(total_blocks, 8));

    /* First free block = block after the end of the bitmap */
    first_bitmap_block = (uint32_t)bitmap_4k / block_size;
    reserve_blocks = first_bitmap_block
                   + DIV_ROUND_UP(DIV_ROUND_UP(total_blocks, 8), block_size);

    free_blocks = 0;
    for (size_t i = reserve_blocks; i < total_blocks; i++) {
        bitmap_clear(i);
        free_blocks++;
    }

    pmm_initialized = 1;
    spinlock_unlock(pmm_lock);

    KLOG("PMM: total %u pages (%u MB), %u pages free, bitmap at 0x%x",
         total_blocks, total_memory >> 20, free_blocks, (uint32_t)bitmap_4k);
}

uint32_t pmm_alloc_page(void)
{
    if (!pmm_initialized)
        return 0;

    spinlock_lock(pmm_lock);

    for (size_t i = 0; i < total_blocks; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_blocks--;

            memset((void*)(i * block_size), 0, block_size);
            spinlock_unlock(pmm_lock);
            return i * block_size;
        }
    }
    spinlock_unlock(pmm_lock);

    KLOG("PMM: out of memory!");
    return 0;
}

void pmm_free_page(uint32_t paddr)
{
    uint32_t block = paddr / block_size;
    if (!pmm_initialized || block >= total_blocks)
        return;

    spinlock_lock(pmm_lock);
    if (bitmap_test(block)) {
        bitmap_clear(block);
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
