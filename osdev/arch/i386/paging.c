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
#include "drivers/log_driver.h"

/* ------------------------------------------------------------------ */
/* Bootstrap page structures — allocated in BSS, page-aligned          */
/* ------------------------------------------------------------------ */

/*
 * boot_pdir[0]             -> identity maps first 4MB (virtual 0)
 * boot_pdir[PD_INDEX(KERNEL_BASE_VADDR)] -> kernel higher-half (virtual 0xC0000000)
 * boot_pdir[1..767]        -> identity maps remaining physical memory
 *                              (created dynamically if total_memory > 4MB)
 *
 * At minimum we need the first 4MB identity-mapped plus the
 * higher-half kernel mapping sharing the same page table.
 */

static pde_t boot_pdir[1024] __attribute__((aligned(PAGE_SIZE)));
static pte_t boot_pt0[1024]  __attribute__((aligned(PAGE_SIZE)));

/* ------------------------------------------------------------------ */
/* Extern: linker-defined end-of-kernel symbol                         */
/* ------------------------------------------------------------------ */
extern uint8_t __kernel_end[];   /* defined in linker.ld */

/*
 * Initialise a single 4MB identity-mapping PDE.
 *
 * Uses a 4KB page table (1024 × 4KB pages = 4MB).
 * Each PTE maps pfn i to physical page i (identity).
 */
static void identity_map_4mb(pde_t* pde, pte_t* pt, uint32_t flags)
{
    uint32_t i;

    memset(pt, 0, PAGE_SIZE);
    for (i = 0; i < 1024; i++) {
        pt[i].raw      = 0;
        pt[i].present  = 1;
        pt[i].rw       = (flags & PTE_RW)   ? 1 : 0;
        pt[i].user     = (flags & PTE_USER) ? 1 : 0;
        pt[i].paddr    = i;
    }

    pde->raw      = 0;
    pde->present  = 1;
    pde->rw       = 1;
    pde->paddr    = ((uint32_t)pt) >> 12;
}

#define IS_4KB_ALIGN(x) \
    (((uint32_t)(x) & (PAGE_SIZE - 1)) == 0)

static pde_t pdes[1024] __attribute__((aligned(PAGE_SIZE)));
static pte_t ptes[16][1024]  __attribute__((aligned(PAGE_SIZE)));

static void map_4mb(void* va, void* pa, uint32_t flags)
{
    static size_t pte_index = 0;
    size_t pde_index = PD_INDEX(va);

    if (!IS_4KB_ALIGN(va) || !IS_4KB_ALIGN(pa)) {
        KLOG("map_4mb: addresses must be 4KB-aligned");
        return;
    }

    for (uint32_t i = 0; i < 1024; i++) {
        ptes[pte_index][i].raw      = 0;
        ptes[pte_index][i].present  = 1;
        ptes[pte_index][i].rw       = (flags & PTE_RW)   ? 1 : 0;
        ptes[pte_index][i].user     = (flags & PTE_USER) ? 1 : 0;
        ptes[pte_index][i].paddr    = ((uint32_t)pa >> 12) + i;
    }

    pdes[pde_index].raw      = 0;
    pdes[pde_index].present  = 1;
    pdes[pde_index].rw       = 1;
    pdes[pde_index].paddr    = ((uint32_t)ptes[pte_index]) >> 12;

    pte_index++;
    if (pte_index >= 16) {
        KLOG("map_4mb: out of page tables");
        pte_index = 0;  /* wrap around, but this is a bug */
    }
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

    KLOG("arch_paging_init: total_memory=%u MB, __kernel_end=0x%x",
         total_memory >> 20, (uint32_t)__kernel_end);

    /* Determine the physical address of the first free byte */
    kernel_end_phys = (uint32_t)__kernel_end;
    kernel_end_phys = PAGE_ALIGN(kernel_end_phys);

    /*
     * The bootstrap PD and PT0 are inside the kernel BSS, so they sit
     * below kernel_end_phys and are automatically protected from the
     * PMM (everything below kernel_end_phys is marked used).
     */

    /* Step 1: Build the initial page directory */
    memset(boot_pdir, 0, sizeof(boot_pdir));

    /* PDE 0: identity-map first 4MB using boot_pt0 */
    identity_map_4mb(&boot_pdir[0], boot_pt0, PTE_KERNEL);

    /*
     * PDE KERNEL_BASE_VADDR/4MB (index 768): reuse boot_pt0 so
     * virtual 0xC0000000-0xC03FFFFF maps to physical 0-4MB.
     * This gives the kernel a higher-half window.
     */
    hi_pd_idx = PD_INDEX(KERNEL_BASE_VADDR);
    boot_pdir[hi_pd_idx].raw      = 0;
    boot_pdir[hi_pd_idx].present  = 1;
    boot_pdir[hi_pd_idx].rw       = 1;
    boot_pdir[hi_pd_idx].paddr    = ((uint32_t)boot_pt0) >> 12;

    /*
     * Identity-map the rest of physical memory so the kernel can
     * access all RAM directly.  For each additional 4MB chunk we need
     * a new page table, allocated via... well, we can't use kmalloc
     * yet.  We'll allocate from the top of physical memory and mark
     * it used in PMM after initialisation.
     *
     * Strategy: keep it simple — identity-map the first 256 PDEs
     * (1GB) using 4MB pages (PSE).  This avoids needing many page
     * tables and is sufficient for most setups.
     *
     * BUT: PSE requires CR4.PSE=1 which we'll set.
     */
    total_4mb_chunks = (total_memory + 0x3FFFFF) >> 22;
    if (total_4mb_chunks > 1024)
        total_4mb_chunks = 1024;

    for (i = 1; i < total_4mb_chunks; i++) {
        if (i == hi_pd_idx)
            continue;  /* skip the higher-half slot */
        boot_pdir[i].raw        = 0;
        boot_pdir[i].present    = 1;
        boot_pdir[i].rw         = 1;
        boot_pdir[i].page_size  = 1;         /* 4MB page */
        /*
         * For a 4MB page, paddr holds bits [31:22] of the physical
         * address (4MB-aligned).  Frame i starts at physical i*4MB,
         * so the pfn (>> 12) is i * 1024 = i << 10.
         */
        boot_pdir[i].paddr      = i * 1024;  /* physical 4MB frame i */
    }

    /* Step 2: Set PSE (Page Size Extension) in CR4 */
    __asm__ __volatile__(
        "mov %%cr4, %%eax\n\t"
        "or  $0x00000010, %%eax\n\t"   /* CR4.PSE = bit 4 */
        "mov %%eax, %%cr4\n\t"
        : : : "eax", "memory"
    );
    KLOG("Paging: PSE enabled (4MB pages)");

    /* Step 3: Enable paging */
    vmm_load_cr3((uint32_t)boot_pdir);
    vmm_enable_paging();
    KLOG("Paging: enabled (CR0.PG=1), CR3=0x%x", (uint32_t)boot_pdir);

    /* Step 4: Register the kernel master PD */
    vmm_set_kernel_pdir((uint32_t)boot_pdir);

    /* Step 5: Initialise the physical memory manager */
    pmm_init(total_memory, kernel_end_phys,
             (uint32_t)boot_pdir, (uint32_t)boot_pt0);
    KLOG("Paging: bootstrap complete, %u pages free", pmm_free_page_count());
}
