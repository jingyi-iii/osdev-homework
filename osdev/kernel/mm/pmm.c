#include "mm/pmm.h"
#include "mm/paging.h"
#include "lib/string.h"
#include "drivers/log_driver.h"

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

/* Helpers */
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

void pmm_init(uint32_t total_memory, uint32_t kernel_end_phys,
              uint32_t kernel_pd_phys, uint32_t kernel_pt0_phys)
{
    uint32_t bitmap_start_phys;
    uint32_t bitmap_end_phys;
    uint32_t reserved_top;
    uint32_t i;

    total_blocks = total_memory / PAGE_SIZE;

    /* Round kernel_end_phys up to page boundary */
    kernel_end_phys = PAGE_ALIGN(kernel_end_phys);

    /*
     * Find the highest reserved address among the bootstrap structures.
     * Everything up to this point must not be handed out.
     */
    reserved_top = kernel_end_phys;
    if (kernel_pd_phys + PAGE_SIZE > reserved_top)
        reserved_top = kernel_pd_phys + PAGE_SIZE;
    if (kernel_pt0_phys + PAGE_SIZE > reserved_top)
        reserved_top = kernel_pt0_phys + PAGE_SIZE;
    reserved_top = PAGE_ALIGN(reserved_top);

    /*
     * Place the bitmap right after the highest reserved region.
     * Bitmap size = total_blocks / 8 bytes, page-aligned.
     */
    bitmap_start_phys = reserved_top;
    bitmap = (uint8_t*)bitmap_start_phys;

    uint32_t bitmap_bytes = (total_blocks + 7) / 8;
    bitmap_bytes = PAGE_ALIGN(bitmap_bytes);
    bitmap_end_phys = bitmap_start_phys + bitmap_bytes;

    /* Mark EVERYTHING as used first... */
    memset(bitmap, 0xFF, bitmap_bytes);

    /*
     * Then free blocks from bitmap_end_phys up to total_memory.
     * Blocks below bitmap_end_phys stay marked used (kernel + bootstrap).
     */
    uint32_t first_free_block = bitmap_end_phys / PAGE_SIZE;
    free_blocks = 0;
    for (i = first_free_block; i < total_blocks; i++) {
        bit_clear(i);
        free_blocks++;
    }

    KLOG("PMM: total %u pages (%u MB), %u pages free, bitmap at 0x%x",
         total_blocks, total_memory >> 20, free_blocks, bitmap_start_phys);
}

uint32_t pmm_alloc_page(void)
{
    uint32_t i;
    for (i = 0; i < total_blocks; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            free_blocks--;
            /*
             * Zero the page.  The page is currently identity-mapped so we
             * can just use memset on the physical address directly.
             */
            memset((void*)(i * PAGE_SIZE), 0, PAGE_SIZE);
            return i * PAGE_SIZE;
        }
    }
    KLOG("PMM: out of memory!");
    return 0;
}

void pmm_free_page(uint32_t paddr)
{
    uint32_t block = paddr / PAGE_SIZE;
    if (block >= total_blocks)
        return;
    if (bit_test(block)) {
        bit_clear(block);
        free_blocks++;
    }
}

void pmm_mark_used_range(uint32_t paddr, uint32_t size)
{
    uint32_t start_block = paddr / PAGE_SIZE;
    uint32_t num_blocks = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t i;

    for (i = 0; i < num_blocks; i++) {
        uint32_t block = start_block + i;
        if (block >= total_blocks)
            break;
        if (!bit_test(block)) {
            bit_set(block);
            free_blocks--;
        }
    }
}

uint32_t pmm_free_page_count(void)
{
    return free_blocks;
}
