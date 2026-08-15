#include "mm/pmm.h"
#include "mm/paging.h"
#include "lib/string.h"
#include "drivers/log_server.h"
#include "sync/spinlock.h"

/*
 * Physical Memory Manager — bitmap-based allocator.
 *
 * Physical memory layout (example with 64 MB):
 *   0x00000000 - 0x000FFFFF   Reserved (BIOS, IVT, BDA, EBDA, etc.)
 *   0x00100000 - 0x001?????   Kernel image (code + rodata + data + bss)
 *   0x00?????? - 0x00??????   Bootstrap page structures (PD + PT0)
 *   0x00?????? - 0x00??????   Paging-structures pool (page directories
 *                             + page tables; linker-reserved, never
 *                             handed out — see linker.ld .page_tables)
 *   0x00?????? - 0x00??????   Bitmap (this allocator's metadata)
 *   0x00?????? - 0x03FFFFFF   Free pages
 */

#define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))

/* One bit per 4KB page: 1 = used, 0 = free */
static const u32   block_size      = 4096;  /* 4KB pages */
static u8*         bitmap_4k       = 0;
static u32         total_blocks    = 0;
static u32         free_blocks     = 0;
static spinlock*        pmm_lock        = 0;
static int              pmm_initialized = 0;

static inline void bitmap_set(u32 block)
{
    /*
     * 8 blocks per byte, so divide by 8 to get the byte index,
     * and use modulo 8 to get the bit index within that byte.
     */
    bitmap_4k[block / 8] |= (u8)(1U << (block % 8));
}

static inline void bitmap_clear(u32 block)
{
    bitmap_4k[block / 8] &= (u8)(~(1U << (block % 8)));
}

static inline int bitmap_test(u32 block)
{
    return (bitmap_4k[block / 8] >> (block % 8)) & 1;
}

void pmm_init(u32 total_memory, u8* bitmap_pa)
{
    u32 reserve_blocks = 0;
    u32 first_bitmap_block = 0;

    if (pmm_initialized)
        return;

    if (!bitmap_pa) {
        LOG("pmm_init: bitmap_pa is NULL");
        return;
    }

    pmm_lock = spinlock_alloc();
    if (!pmm_lock) {
        LOG("pmm_init: failed to allocate PMM spinlock");
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
    bitmap_4k = (u8*)PAGE_ALIGN(bitmap_pa);

    /* Mark all blocks as used */
    memset(bitmap_4k, 0xFF, DIV_ROUND_UP(total_blocks, 8));

    /* First free block = block after the end of the bitmap */
    first_bitmap_block = (u32)bitmap_4k / block_size;
    reserve_blocks = first_bitmap_block
                   + DIV_ROUND_UP(DIV_ROUND_UP(total_blocks, 8), block_size);

    /*
     * Everything below the bitmap is reserved: BIOS, kernel image, the
     * linker-reserved paging-structures pool (linker.ld .page_tables)
     * and the bitmap itself.  Only pages after the bitmap are freed.
     */
    free_blocks = 0;
    for (size_t i = reserve_blocks; i < total_blocks; i++) {
        bitmap_clear(i);
        free_blocks++;
    }

    pmm_initialized = 1;
    spinlock_unlock(pmm_lock);

    LOG("PMM: total %u pages (%u MB), %u pages free, bitmap at 0x%x",
         total_blocks, total_memory >> 20, free_blocks, (u32)bitmap_4k);
}

u32 pmm_alloc_page(void)
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

    LOG("PMM: out of memory!");
    return 0;
}

void pmm_free_page(u32 paddr)
{
    u32 block = paddr / block_size;
    if (!pmm_initialized || block >= total_blocks)
        return;

    spinlock_lock(pmm_lock);
    if (bitmap_test(block)) {
        bitmap_clear(block);
        free_blocks++;
    }
    spinlock_unlock(pmm_lock);
}

u32 pmm_get_free_page_count(void)
{
    u32 blocks = 0;

    if (!pmm_initialized)
        return 0;

    spinlock_lock(pmm_lock);
    blocks = free_blocks;
    spinlock_unlock(pmm_lock);

    return blocks;
}

u32 pmm_alloc_pages(u32 num_pages)
{
    if (!pmm_initialized || num_pages == 0)
        return 0;

    spinlock_lock(pmm_lock);

    u32 start_block = 0;
    u32 found_blocks = 0;

    for (size_t i = 0; i < total_blocks; i++) {
        if (!bitmap_test(i)) {
            if (found_blocks == 0) {
                start_block = i;
            }
            found_blocks++;
            if (found_blocks == num_pages) {
                for (size_t j = start_block; j < start_block + num_pages; j++) {
                    bitmap_set(j);
                }
                free_blocks -= num_pages;
                memset((void*)(start_block * block_size), 0, num_pages * block_size);
                spinlock_unlock(pmm_lock);
                return start_block * block_size;
            }
        } else {
            found_blocks = 0;
        }
    }

    spinlock_unlock(pmm_lock);
    LOG("PMM: out of memory for %u pages!", num_pages);
    return 0;
}

void pmm_free_pages(u32 paddr, u32 num_pages)
{
    if (!pmm_initialized || num_pages == 0)
        return;

    u32 start_block = paddr / block_size;

    spinlock_lock(pmm_lock);
    for (size_t i = start_block; i < start_block + num_pages; i++) {
        if (i < total_blocks && bitmap_test(i)) {
            bitmap_clear(i);
            free_blocks++;
        }
    }
    spinlock_unlock(pmm_lock);
}
