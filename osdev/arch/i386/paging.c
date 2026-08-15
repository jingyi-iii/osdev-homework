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
#include "drivers/log_server.h"

/* ------------------------------------------------------------------ */
/* Extern: linker-defined PMM bitmap section                          */
/* ------------------------------------------------------------------ */
extern u8 __pmm_bitmap_start[];   /* defined in linker.ld */

static pde pdes[1024] __attribute__((aligned(PAGE_SIZE)));
static spinlock* paging_lock;

/* The kernel's master page directory (physical address).
 * All user processes clone kernel-space entries from here. */
static u32 kernel_pdir_phys = 0;

/* ------------------------------------------------------------------ */
/* Paging-structures pool                                              */
/*                                                                     */
/* Page directories AND page tables are accessed through their         */
/* identity VA (pdir_of / ptbl_of_pde).  To guarantee they can never   */
/* numerically collide with a user VA (also identity-mapped), both are */
/* carved from a fixed linker-reserved region (linker.ld .page_tables) */
/* that the PMM never hands out — the free pool starts after           */
/* __page_table_end.                                                   */
/* ------------------------------------------------------------------ */
extern u32 __page_table_base[];
extern u32 __page_table_end[];

#define VMM_PDE_ALLOC_BASE  ((u32)__page_table_base)
#define VMM_PDE_ALLOC_SIZE  (8u * 1024 * 1024)
#define VMM_PDE_ALLOC_END   ((u32)__page_table_end)

/* one 4 KB slot per page table / directory; 8 MB pool => 2048 slots */
#define PAGING_POOL_SLOTS (VMM_PDE_ALLOC_SIZE / PAGE_SIZE)

static spinlock* paging_pool_lock = 0;
/* one bit per slot: 1 = in use */
static u32 paging_pool_used[(PAGING_POOL_SLOTS + 31) / 32];

void* arch_paging_pool_alloc(void)
{
    if (!paging_pool_lock)
        return 0;

    spinlock_lock(paging_pool_lock);
    for (size_t i = 0; i < PAGING_POOL_SLOTS; i++) {
        if (!(paging_pool_used[i / 32] & (1u << (i % 32)))) {
            paging_pool_used[i / 32] |= (1u << (i % 32));
            u32 pa = VMM_PDE_ALLOC_BASE + i * PAGE_SIZE;
            memset((void*)pa, 0, PAGE_SIZE);
            spinlock_unlock(paging_pool_lock);
            return (void*)pa;
        }
    }
    spinlock_unlock(paging_pool_lock);

    LOG("arch_paging_pool_alloc: paging-structures pool exhausted");
    return 0;
}

void arch_paging_pool_free(u32 pa)
{
    if (!paging_pool_lock)
        return;

    /* ignore addresses outside the pool (e.g. 4 MB chunk PDEs) */
    if (pa < VMM_PDE_ALLOC_BASE || pa >= VMM_PDE_ALLOC_END)
        return;

    size_t i = (pa - VMM_PDE_ALLOC_BASE) / PAGE_SIZE;

    spinlock_lock(paging_pool_lock);
    paging_pool_used[i / 32] &= ~(1u << (i % 32));
    spinlock_unlock(paging_pool_lock);
}

static inline pde* pdir_of(u32 pdir_phys)
{
    return (pde*)pdir_phys;
}

static inline pte* ptbl_of(u32 ptbl_phys)
{
    return (pte*)ptbl_phys;
}

static inline pte* ptbl_of_pde(pde* pde)
{
    return (pte*)(pde->paddr << 12);
}

/* Invalidate a single TLB entry */
void arch_tlb_invlpg(u32 vaddr)
{
    __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

void arch_map_4mb(void* cr3, void* va, void* pa, u32 flags)
{
    pde* pdes = (pde*)cr3;
    size_t pde_index = PD_INDEX(va);

    if (pde_index >= 1024) {
        LOG("arch_map_4mb: virtual address out of range");
        return;
    }

    if (!pdes) {
        LOG("arch_map_4mb: invalid cr3");
        return;
    }

    if (!IS_4MB_ALIGN(va) || !IS_4MB_ALIGN(pa)) {
        LOG("arch_map_4mb: addresses must be 4MB-aligned");
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
    pdes[pde_index].paddr       = ((u32)pa) >> 12;
    spinlock_unlock(paging_lock);

    arch_tlb_invlpg((u32)va);
}

void arch_unmap_4mb(void* cr3, void* va)
{
    pde* pdes = (pde*)cr3;
    size_t pde_index = PD_INDEX(va);

    if (pde_index >= 1024) {
        LOG("arch_unmap_4mb: virtual address out of range");
        return;
    }

    if (!pdes) {
        LOG("arch_map_4mb: invalid cr3");
        return;
    }

    spinlock_lock(paging_lock);
    pdes[pde_index].raw = 0;
    spinlock_unlock(paging_lock);

    arch_tlb_invlpg((u32)va);
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
static inline int split_4mb_pde(pde* p, u32 user_accessible)
{
    pte* ptl = 0;

    if (!p) {
        LOG("split_4mb_pde: invalid pde");
        return -1;
    }

    if (!p->present || !p->page_size) {
        LOG("split_4mb_pde: PDE is not a 4MB page");
        return -1;
    }

    ptl = (pte*)arch_paging_pool_alloc();
    if (!ptl) {
        LOG("split_4mb_pde: failed to allocate page table");
        return E_NOMEM;
    }

    if (user_accessible && p->user == 0)
        p->user = 1;

    for (size_t i = 0; i < 1024; i++) {
        ptl[i].raw         = 0;
        ptl[i].present     = p->present;
        ptl[i].rw          = p->rw;
        ptl[i].user        = p->user;
        ptl[i].pwt         = p->pwt;
        ptl[i].pcd         = p->pcd;
        ptl[i].global      = p->global;
        ptl[i].paddr       = p->paddr + i;
    }

    {
        pde new_pde;
        new_pde.raw = p->raw;
        new_pde.present = 1;
        new_pde.rw = 1;         /* writable PTEs take effect */
        new_pde.page_size = 0;  /* 4KB page table */
        new_pde.paddr = (u32)ptl >> 12;
        p->raw = new_pde.raw;
    }

    return 0;
}

int arch_map_4kb(void* cr3, void* va, void* pa, u32 flags)
{
    pde* pdes = (pde*)cr3;
    size_t pde_index = PD_INDEX(va);
    size_t pte_index = PT_INDEX(va);

    if (pde_index >= 1024 || pte_index >= 1024) {
        LOG("arch_map_4kb: virtual address out of range");
        return E_INVAL;
    }

    if (!pdes) {
        LOG("arch_map_4kb: invalid cr3");
        return E_INVAL;
    }

    spinlock_lock(paging_lock);

    /*
     * If a 4MB page already covers this virtual address, split it
     * into 4KB page tables first so we can create a fine-grained
     * 4KB mapping alongside the existing identity map.
     */
    if (pdes[pde_index].present && pdes[pde_index].page_size) {
        if (split_4mb_pde(&pdes[pde_index], flags & PTE_USER) != 0) {
            LOG("arch_map_4kb: failed to split 4MB PDE at index %u", pde_index);
            spinlock_unlock(paging_lock);
            return E_NOMEM;
        }
    }

    if (!pdes[pde_index].present) {
        u32 pt_pa = (u32)arch_paging_pool_alloc();
        if (!pt_pa) {
            LOG("arch_map_4kb: failed to allocate page table for vaddr 0x%x", va);
            spinlock_unlock(paging_lock);
            return E_NOMEM;
        }

        pdes[pde_index].raw     = 0;
        pdes[pde_index].present = 1;
        pdes[pde_index].rw      = 1;
        pdes[pde_index].user    = (flags & PTE_USER) ? 1 : 0;
        pdes[pde_index].paddr   = pt_pa >> 12;

        memset((void*)pt_pa, 0, PAGE_SIZE);
    }

    pte* ptbl = ptbl_of_pde(&pdes[pde_index]);
    ptbl[pte_index].raw         = 0;
    ptbl[pte_index].present     = (flags & PTE_PRESENT) ? 1 : 0;
    ptbl[pte_index].rw          = (flags & PTE_RW)      ? 1 : 0;
    ptbl[pte_index].user        = (flags & PTE_USER)    ? 1 : 0;
    ptbl[pte_index].paddr       = ((u32)pa) >> 12;
    spinlock_unlock(paging_lock);

    arch_tlb_invlpg((u32)va);
    return 0;
}

void arch_unmap_4kb(void* cr3, void* va)
{
    pde* pdes = (pde*)cr3;
    size_t pde_index = PD_INDEX(va);
    size_t pte_index = PT_INDEX(va);

    if (pde_index >= 1024 || pte_index >= 1024) {
        LOG("arch_unmap_4kb: virtual address out of range");
        return;
    }

    if (!pdes) {
        LOG("arch_map_4kb: invalid cr3");
        return;
    }

    spinlock_lock(paging_lock);
    if (!pdes[pde_index].present) {
        spinlock_unlock(paging_lock);
        return;
    }

    pte* ptbl = ptbl_of_pde(&pdes[pde_index]);
    if (!ptbl[pte_index].present) {
        spinlock_unlock(paging_lock);
        return;
    }

    // pmm_free_page(ptbl[pte_index].paddr << 12);
    ptbl[pte_index].raw = 0;
    spinlock_unlock(paging_lock);

    arch_tlb_invlpg((u32)va);
}

void arch_map_4mb_range(void* cr3, u32 start_pa, u32 end_pa, u32 flags)
{
    if (!cr3) {
        LOG("arch_map_4mb_range: invalid cr3");
        return;
    }

    if (!IS_4MB_ALIGN(start_pa) || !IS_4MB_ALIGN(end_pa)) {
        LOG("arch_map_4mb_range: addresses must be 4MB-aligned");
        return;
    }

    for (u32 pa = start_pa; pa < end_pa; pa += 0x400000) {
        arch_map_4mb(cr3, (void*)pa, (void*)pa, flags);
    }
}

void arch_map_4kb_range(void* cr3, u32 start_pa, u32 end_pa, u32 flags)
{
    if (!cr3) {
        LOG("arch_map_4kb_range: invalid cr3");
        return;
    }

    if (!IS_4KB_ALIGN(start_pa) || !IS_4KB_ALIGN(end_pa)) {
        LOG("arch_map_4kb_range: addresses must be 4KB-aligned");
        return;
    }

    for (u32 pa = start_pa; pa < end_pa; pa += PAGE_SIZE) {
        arch_map_4kb(cr3, (void*)pa, (void*)pa, flags);
    }
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                  */
/* ------------------------------------------------------------------ */
void arch_paging_init(u32 total_memory, u32 reserved_end)
{
    u32 total_4mb_chunks;
    u32 hi_pd_idx;
    u32 i;
    (void)reserved_end;  /* currently unused — the paging pool sits below
                          * the PMM bitmap, so pmm_init() keeps it reserved */

    paging_lock = spinlock_alloc();
    if (!paging_lock) {
        LOG("arch_paging_init: failed to allocate paging spinlock");
        return;
    }

    paging_pool_lock = spinlock_alloc();
    if (!paging_pool_lock) {
        LOG("arch_paging_init: failed to allocate paging pool spinlock");
        return;
    }

    LOG("arch_paging_init: total_memory=%u MB, bitmap=0x%x",
         total_memory >> 20, (u32)__pmm_bitmap_start);

    /* Step 1: Build the initial page directory.
     * Use PTE_USER_PAGE so that user-mode (ring-3) threads can
     * execute kernel code and access kernel data within the same
     * address space.  This is acceptable for a hobby / testing OS. */
    memset(pdes, 0, sizeof(pdes));
    arch_map_4mb_range((void*)pdes, 0x0, 0x1000000, PTE_USER_PAGE);  /* identity map first 16MB */
    arch_map_4mb((void*)pdes, (void*)0xC0000000, (void*)0x0, PTE_USER_PAGE);

    for (u32 addr = 0x1000000; addr < total_memory; addr += 0x400000) {
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
    LOG("Paging: PSE enabled (4MB pages)");

    /* Step 3: Enable paging */
    arch_load_cr3((u32)pdes);
    arch_enable_paging();
    LOG("Paging: enabled (CR0.PG=1), CR3=0x%x", (u32)pdes);

    /* Step 4: Register the kernel master PD */
    kernel_pdir_phys = (u32)pdes;

    /* Step 5: Initialise the physical memory manager.  The paging pool
     * sits below the PMM bitmap, so pmm_init() keeps it reserved and
     * user VAs (identity-mapped) can never collide with its PAs. */
    pmm_init(total_memory, __pmm_bitmap_start);
    LOG("Paging: paging-structures pool 0x%x-0x%x (%u slots)",
         VMM_PDE_ALLOC_BASE, VMM_PDE_ALLOC_END, PAGING_POOL_SLOTS);
    LOG("Paging: bootstrap complete, %u pages free", pmm_get_free_page_count());
}

void arch_load_cr3(u32 pdir_phys)
{
    /* Writing CR3 flushes the TLB for all non-global entries */
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pdir_phys) : "memory");
}

u32 arch_get_cr3(void)
{
    u32 cr3 = 0;
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

u32 arch_clone_kernel_pde(u32 pde_pa, int user_accessible)
{
    if (!pde_pa) {
        LOG("VMM: failed to allocate page for PDE clone");
        return 0;
    }

    pde* new_pde = (pde*)pde_pa;
    pde* kern_pde = (pde*)kernel_pdir_phys;

    /*
     * Clone ALL PDEs from the kernel master PD, including the low
     * identity map.  The kernel is linked at 1MB (identity mapped),
     * so a user process MUST share the kernel's low identity map to
     * be able to execute any kernel code (the first 16MB are mapped
     * with PTE_USER_PAGE for exactly this purpose — see
     * arch_paging_init()).  For this hobby OS, user and kernel
     * processes therefore live in the same address space.
     */
    (void)user_accessible;
    for (u32 i = 0; i < 1024; i++)
        new_pde[i].raw = kern_pde[i].raw;

    return pde_pa;
}

void arch_destroy_address_space(u32 pdir_phys)
{
    if (!pdir_phys || pdir_phys == kernel_pdir_phys) {
        LOG("VMM: cannot destroy kernel address space");
        return;
    }

    pde* pdes = pdir_of(pdir_phys);
    pde* kern_pdes = pdir_of(kernel_pdir_phys);

    /* only release user pages */
    for (u32 i = 0; i < 768; i++) {
        if (!pdes[i].present)
            continue;
        if (pdes[i].paddr == kern_pdes[i].paddr)
            continue;  /* skip kernel-shared page tables */

        arch_paging_pool_free(pdes[i].paddr << 12);
        pdes[i].raw = 0;
    }
}
