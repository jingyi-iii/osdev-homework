/**
 * arch/i386/paging.c — Bootstrap paging initialization.
 *
 * This file:
 *   1. Declares the initial page directory and page table as static BSS
 *      arrays (page-aligned, part of the kernel image).
 *   2. Identity-maps the first N megabytes so the kernel can keep
 *      running after paging is enabled.
 *   3. Also maps the kernel at KERNEL_BASE_VADDR (higher-half).
 *   4. Initialises the PMM with the remaining free physical memory.
 *   5. Calls vmm_set_kernel_pdir() to register the master PD.
 *
 * The caller (kernel_start or early boot code) must invoke
 * arch_paging_init() exactly once before any process creation.
 */

#include "mm/paging.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "lib/string.h"
#include "sync/spinlock.h"
#include "drivers/log_driver.h"

/* ------------------------------------------------------------------ */
/* Extern: linker-defined end-of-kernel symbol                         */
/* ------------------------------------------------------------------ */
extern uint8_t __kernel_end[];   /* defined in linker.ld */

static pde_t pdes[1024] __attribute__((aligned(PAGE_SIZE)));
static spinlock* paging_lock;

static void identity_map_4mb(void* va, void* pa, uint32_t flags)
{
    size_t pde_index = PD_INDEX(va);

    if (pde_index >= 1024) {
        KLOG("identity_map_4mb: virtual address out of range");
        return;
    }

    if (!IS_4MB_ALIGN(va) || !IS_4MB_ALIGN(pa)) {
        KLOG("identity_map_4mb: addresses must be 4MB-aligned");
        return;
    }

    spinlock_lock(paging_lock);
    pdes[pde_index].raw         = 0;
    pdes[pde_index].page_size   = 1;    /* 4MB page */
    pdes[pde_index].present     = 1;
    pdes[pde_index].rw          = 1;
    pdes[pde_index].paddr       = ((uint32_t)pa) >> 12;
    spinlock_unlock(paging_lock);
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */
void arch_paging_init(uint32_t total_memory, uint32_t reserved_end)
{
    uint32_t kernel_end_phys;
    uint32_t total_4mb_chunks;
    uint32_t hi_pd_idx;
    uint32_t i;
    (void)reserved_end;  /* currently unused — reserved_end is derived inside */

    paging_lock = spinlock_alloc();
    if (!paging_lock) {
        KLOG("arch_paging_init: failed to allocate paging spinlock");
        return;
    }

    KLOG("arch_paging_init: total_memory=%u MB, __kernel_end=0x%x",
         total_memory >> 20, (uint32_t)__kernel_end);

    /* Determine the physical address of the first free byte */
    kernel_end_phys = (uint32_t)__kernel_end;
    kernel_end_phys = PAGE_ALIGN(kernel_end_phys);

    /* Step 1: Build the initial page directory */
    memset(pdes, 0, sizeof(pdes));
    identity_map_4mb((void*)0x0,        (void*)0x0, PTE_KERNEL);
    identity_map_4mb((void*)0xC0000000, (void*)0x0, PTE_KERNEL);

    for (uint32_t addr = 0x400000; addr < total_memory; addr += 0x400000) {
        if (addr == 0xC0000000)
            continue;  /* skip the higher-half slot */
        identity_map_4mb((void*)addr, (void*)addr, PTE_KERNEL);
    }

    /* Step 2: Set PSE (Page Size Extension) in CR4 */
    __asm__ __volatile__(
        "mov    %%cr4,          %%eax\n\t"
        "or     $0x00000010,    %%eax\n\t"   /* CR4.PSE = bit 4 */
        "mov    %%eax,          %%cr4\n\t"
        : : : "eax", "memory"
    );
    KLOG("Paging: PSE enabled (4MB pages)");

    /* Step 3: Enable paging */
    vmm_load_cr3((uint32_t)pdes);
    vmm_enable_paging();
    KLOG("Paging: enabled (CR0.PG=1), CR3=0x%x", (uint32_t)pdes);

    /* Step 4: Register the kernel master PD */
    // vmm_set_kernel_pdir((uint32_t)boot_pdir);
    vmm_set_kernel_pdir((uint32_t)pdes);

    /* Step 5: Initialise the physical memory manager */
    pmm_init(total_memory, __kernel_end);
    KLOG("Paging: bootstrap complete, %u pages free", pmm_get_free_page_count());
}
