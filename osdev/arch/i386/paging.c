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
 *   5. Calls arch_set_kernel_pdir() to register the master PD.
 *
 * The caller (kernel_start or early boot code) must invoke
 * arch_paging_init() exactly once before any process creation.
 */

#include "mm/paging.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "sync/spinlock.h"
#include "drivers/log_driver.h"

/* ------------------------------------------------------------------ */
/* Extern: linker-defined PMM bitmap section                          */
/* ------------------------------------------------------------------ */
extern uint8_t __pmm_bitmap_start[];   /* defined in linker.ld */

static pde_t pdes[1024] __attribute__((aligned(PAGE_SIZE)));
static spinlock* paging_lock;

/* The kernel's master page directory (physical address).
 * All user processes clone kernel-space entries from here. */
static uint32_t kernel_pdir_phys = 0;

static inline pde_t* pdir_of(uint32_t pdir_phys)
{
    return (pde_t*)pdir_phys;
}

static inline pte_t* ptbl_of(uint32_t ptbl_phys)
{
    return (pte_t*)ptbl_phys;
}

static inline pte_t* ptbl_of_pde(pde_t* pde)
{
    return (pte_t*)(pde->paddr << 12);
}

/* Invalidate a single TLB entry */
static inline void tlb_invlpg(uint32_t vaddr)
{
    __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

void arch_map_4mb(void* cr3, void* va, void* pa, uint32_t flags)
{
    pde_t* pdes = (pde_t*)cr3;
    size_t pde_index = PD_INDEX(va);

    if (pde_index >= 1024) {
        KLOG("arch_map_4mb: virtual address out of range");
        return;
    }

    if (!pdes) {
        KLOG("arch_map_4mb: invalid cr3");
        return;
    }

    if (!IS_4MB_ALIGN(va) || !IS_4MB_ALIGN(pa)) {
        KLOG("arch_map_4mb: addresses must be 4MB-aligned");
        return;
    }

    spinlock_lock(paging_lock);
    pdes[pde_index].raw         = 0;
    pdes[pde_index].page_size   = 1;    /* 4MB page */
    pdes[pde_index].present     = (flags & PTE_PRESENT)  ? 1 : 0;
    pdes[pde_index].rw          = (flags & PTE_RW)       ? 1 : 0;
    pdes[pde_index].user        = (flags & PTE_USER)     ? 1 : 0;
    pdes[pde_index].pwt         = (flags & PTE_PWT)      ? 1 : 0;
    pdes[pde_index].pcd         = (flags & PTE_PCD)      ? 1 : 0;
    pdes[pde_index].global      = (flags & PTE_GLOBAL)   ? 1 : 0;
    pdes[pde_index].paddr       = ((uint32_t)pa) >> 12;
    spinlock_unlock(paging_lock);

    tlb_invlpg((uint32_t)va);
}

void arch_unmap_4mb(void* cr3, void* va)
{
    pde_t* pdes = (pde_t*)cr3;
    size_t pde_index = PD_INDEX(va);

    if (pde_index >= 1024) {
        KLOG("arch_unmap_4mb: virtual address out of range");
        return;
    }

    if (!pdes) {
        KLOG("arch_map_4mb: invalid cr3");
        return;
    }

    spinlock_lock(paging_lock);
    pdes[pde_index].raw = 0;
    spinlock_unlock(paging_lock);

    tlb_invlpg((uint32_t)va);
}

/*
 * split_4mb_pde — Split a 4MB PDE into 1024 4KB PTEs.
 *
 * Allocates a new page table and fills every PTE with the original
 * 4MB PDE's attributes, preserving the identity mapping (PA = VA).
 * Then replaces the 4MB PDE with a PDE pointing to the new page table.
 *
 * Caller MUST hold paging_lock.
 * Returns 0 on success, negative on failure.
 */
static inline int split_4mb_pde(uint32_t pde_index, uint32_t user_accessible)
{
    pde_t* pde = &pdes[pde_index];
    pte_t* ptl = 0;

    if (!pde->present || !pde->page_size) {
        KLOG("split_4mb_pde: PDE at index %u is not a 4MB page", pde_index);
        return -1;
    }

    ptl = (pte_t*)pmm_alloc_page();
    if (!ptl) {
        KLOG("split_4mb_pde: failed to allocate page table for PDE at index %u", pde_index);
        return E_NOMEM;
    }

    if (user_accessible && pde->user == 0)
        pde->user = 1;

    for (size_t i = 0; i < 1024; i++) {
        ptl[i].raw         = 0;
        ptl[i].present     = pde->present;
        ptl[i].rw          = pde->rw;
        ptl[i].user        = pde->user;
        ptl[i].pwt         = pde->pwt;
        ptl[i].pcd         = pde->pcd;
        ptl[i].global      = pde->global;
        ptl[i].paddr       = pde->paddr + i;
    }

    {
        pde_t new_pde;
        new_pde.raw = pde->raw;
        new_pde.present = 1;
        new_pde.rw = 1;         /* writable PTEs take effect */
        new_pde.page_size = 0;  /* 4KB page table */
        new_pde.paddr = (uint32_t)ptl >> 12;
        pde->raw = new_pde.raw;
    }

    return 0;
}

void arch_map_4kb(void* cr3, void* va, void* pa, uint32_t flags)
{
    pde_t* pdes = (pde_t*)cr3;
    size_t pde_index = PD_INDEX(va);
    size_t pte_index = PT_INDEX(va);

    if (pde_index >= 1024 || pte_index >= 1024) {
        KLOG("arch_map_4kb: virtual address out of range");
        return;
    }

    if (!pdes) {
        KLOG("arch_map_4kb: invalid cr3");
        return;
    }

    spinlock_lock(paging_lock);

    /*
     * If a 4MB page already covers this virtual address, split it
     * into 4KB page tables first so we can create a fine-grained
     * 4KB mapping alongside the existing identity map.
     */
    if (pdes[pde_index].present && pdes[pde_index].page_size) {
        if (split_4mb_pde(pde_index, flags & PTE_USER) != 0) {
            KLOG("arch_map_4kb: failed to split 4MB PDE at index %u", pde_index);
            spinlock_unlock(paging_lock);
            return;
        }
    }

    if (!pdes[pde_index].present) {
        uint32_t pt_pa = pmm_alloc_page();
        if (!pt_pa) {
            KLOG("arch_map_4kb: failed to allocate page table for vaddr 0x%x", va);
            spinlock_unlock(paging_lock);
            return;
        }

        pdes[pde_index].raw     = 0;
        pdes[pde_index].present = 1;
        pdes[pde_index].rw      = 1;
        pdes[pde_index].user    = (flags & PTE_USER) ? 1 : 0;
        pdes[pde_index].paddr   = pt_pa >> 12;

        memset((void*)pt_pa, 0, PAGE_SIZE);
    }

    pte_t* ptbl = ptbl_of_pde(&pdes[pde_index]);
    ptbl[pte_index].raw         = 0;
    ptbl[pte_index].present     = (flags & PTE_PRESENT) ? 1 : 0;
    ptbl[pte_index].rw          = (flags & PTE_RW)      ? 1 : 0;
    ptbl[pte_index].user        = (flags & PTE_USER)    ? 1 : 0;
    ptbl[pte_index].paddr       = ((uint32_t)pa) >> 12;
    spinlock_unlock(paging_lock);

    tlb_invlpg((uint32_t)va);
}

void arch_unmap_4kb(void* cr3, void* va)
{
    pde_t* pdes = (pde_t*)cr3;
    size_t pde_index = PD_INDEX(va);
    size_t pte_index = PT_INDEX(va);

    if (pde_index >= 1024 || pte_index >= 1024) {
        KLOG("arch_unmap_4kb: virtual address out of range");
        return;
    }

    if (!pdes) {
        KLOG("arch_map_4kb: invalid cr3");
        return;
    }

    spinlock_lock(paging_lock);
    if (!pdes[pde_index].present) {
        spinlock_unlock(paging_lock);
        return;
    }

    pte_t* ptbl = ptbl_of_pde(&pdes[pde_index]);
    if (!ptbl[pte_index].present) {
        spinlock_unlock(paging_lock);
        return;
    }

    // pmm_free_page(ptbl[pte_index].paddr << 12);
    ptbl[pte_index].raw = 0;
    spinlock_unlock(paging_lock);

    tlb_invlpg((uint32_t)va);
}

void arch_map_4mb_range(void* cr3, uint32_t start_pa, uint32_t end_pa, uint32_t flags)
{
    if (!cr3) {
        KLOG("arch_map_4mb_range: invalid cr3");
        return;
    }

    if (!IS_4MB_ALIGN(start_pa) || !IS_4MB_ALIGN(end_pa)) {
        KLOG("arch_map_4mb_range: addresses must be 4MB-aligned");
        return;
    }

    for (uint32_t pa = start_pa; pa < end_pa; pa += 0x400000) {
        arch_map_4mb(cr3, (void*)pa, (void*)pa, flags);
    }
}

void arch_map_4kb_range(void* cr3, uint32_t start_pa, uint32_t end_pa, uint32_t flags)
{
    if (!cr3) {
        KLOG("arch_map_4mb_range: invalid cr3");
        return;
    }

    if (!IS_4KB_ALIGN(start_pa) || !IS_4KB_ALIGN(end_pa)) {
        KLOG("arch_map_4kb_range: addresses must be 4KB-aligned");
        return;
    }

    for (uint32_t pa = start_pa; pa < end_pa; pa += PAGE_SIZE) {
        arch_map_4kb(cr3, (void*)pa, (void*)pa, flags);
    }
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */
void arch_paging_init(uint32_t total_memory, uint32_t reserved_end)
{
    uint32_t total_4mb_chunks;
    uint32_t hi_pd_idx;
    uint32_t i;
    (void)reserved_end;  /* currently unused — reserved_end is derived inside */

    paging_lock = spinlock_alloc();
    if (!paging_lock) {
        KLOG("arch_paging_init: failed to allocate paging spinlock");
        return;
    }

    KLOG("arch_paging_init: total_memory=%u MB, bitmap=0x%x",
         total_memory >> 20, (uint32_t)__pmm_bitmap_start);

    /* Step 1: Build the initial page directory.
     * Use PTE_USER_PAGE so that user-mode (ring-3) threads can
     * execute kernel code and access kernel data within the same
     * address space.  This is acceptable for a hobby / testing OS. */
    memset(pdes, 0, sizeof(pdes));
    arch_map_4mb_range((void*)pdes, 0x0, 0x1000000, PTE_USER_PAGE);  /* identity map first 16MB */
    arch_map_4mb((void*)pdes, (void*)0xC0000000, (void*)0x0, PTE_USER_PAGE);

    for (uint32_t addr = 0x1000000; addr < total_memory; addr += 0x400000) {
        if (addr == 0xC0000000)
            continue;  /* skip the higher-half slot */
        arch_map_4mb((void*)pdes, (void*)addr, (void*)addr, PTE_KERNEL);
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
    arch_load_cr3((uint32_t)pdes);
    arch_enable_paging();
    KLOG("Paging: enabled (CR0.PG=1), CR3=0x%x", (uint32_t)pdes);

    /* Step 4: Register the kernel master PD */
    kernel_pdir_phys = (uint32_t)pdes;

    /* Step 5: Initialise the physical memory manager */
    pmm_init(total_memory, __pmm_bitmap_start);
    KLOG("Paging: bootstrap complete, %u pages free", pmm_get_free_page_count());
}

void arch_load_cr3(uint32_t pdir_phys)
{
    /* Writing CR3 flushes the TLB for all non-global entries */
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pdir_phys) : "memory");
}

uint32_t arch_get_cr3(void)
{
    uint32_t cr3 = 0;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void arch_enable_paging(void)
{
    /*
     * Set PG bit (bit 31) in CR0.
     * The near jump flushes the prefetch queue so that all subsequent
     * instruction fetches go through the page-translation hardware.
     */
    __asm__ __volatile__(
        "mov    %%cr0,          %%eax\n\t"
        "or     $0x80000000,    %%eax\n\t"
        "mov    %%eax,          %%cr0\n\t"
        "jmp    1f\n\t"
        "1:     \n\t"
        : : : "eax", "memory"
    );
}

uint32_t arch_clone_kernel_pde(uint32_t pde_pa, int user_accessible)
{
    if (!pde_pa) {
        KLOG("VMM: failed to allocate page for PDE clone");
        return 0;
    }

    pde_t* new_pde = (pde_t*)pde_pa;
    pde_t* kern_pde = (pde_t*)kernel_pdir_phys;

    /* Clone all PDEs from the kernel master PD */
    if (user_accessible) {
        for (uint32_t i = 0; i < 768; i++)
            new_pde[i].raw = 0;
        for (uint32_t i = 768; i < 1024; i++)
            new_pde[i].raw = kern_pde[i].raw;
    } else {
        for (uint32_t i = 0; i < 1024; i++)
            new_pde[i].raw = kern_pde[i].raw;
    }
    return pde_pa;
}

void arch_destroy_address_space(uint32_t pdir_phys)
{
    if (!pdir_phys || pdir_phys == kernel_pdir_phys) {
        KLOG("VMM: cannot destroy kernel address space");
        return;
    }

    pde_t* pdes = pdir_of(pdir_phys);
    pde_t* kern_pdes = pdir_of(kernel_pdir_phys);

    /* only release user pages */
    for (uint32_t i = 0; i < 768; i++) {
        if (!pdes[i].present)
            continue;
        if (pdes[i].paddr == kern_pdes[i].paddr)
            continue;  /* skip kernel-shared page tables */

        pte_t* ptbl = ptbl_of_pde(&pdes[i]);
        for (uint32_t j = 0; j < 1024; j++) {
            if (ptbl[j].present) {
                pmm_free_page(ptbl[j].paddr << 12);
                ptbl[j].raw = 0;
            }
        }
        pmm_free_page(pdes[i].paddr << 12);
        pdes[i].raw = 0;
    }
}
