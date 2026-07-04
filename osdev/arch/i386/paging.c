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
/* Extern: linker-defined end-of-kernel symbol                         */
/* ------------------------------------------------------------------ */
extern uint8_t __kernel_end[];   /* defined in linker.ld */

static pde_t pdes[1024] __attribute__((aligned(PAGE_SIZE)));
static spinlock* paging_lock;

/* The kernel's master page directory (physical address).
 * All user processes clone kernel-space entries from here. */
static uint32_t kernel_pdir_phys = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */
/*
 * Access a page directory by its physical address.
 * Since the entire physical memory is identity-mapped, we can use the
 * physical address as a virtual pointer directly.
 */
static inline pde_t* pdir_of(uint32_t pdir_phys)
{
    return (pde_t*)pdir_phys;
}

/*
 * Given a PDE that points to a page table, return a pointer to that
 * page table (physical address = PDE.paddr << 12).
 */
static inline pte_t* ptbl_of_pde(pde_t* pde)
{
    return (pte_t*)(pde->paddr << 12);
}

/*
 * Given a raw physical address of a page table, return a pointer.
 */
static inline pte_t* ptbl_of_phys(uint32_t pt_phys)
{
    return (pte_t*)pt_phys;
}

/* Invalidate a single TLB entry */
static inline void tlb_invlpg(uint32_t vaddr)
{
    __asm__ __volatile__("invlpg (%0)" : : "r"(vaddr) : "memory");
}

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
    arch_load_cr3((uint32_t)pdes);
    arch_enable_paging();
    KLOG("Paging: enabled (CR0.PG=1), CR3=0x%x", (uint32_t)pdes);

    /* Step 4: Register the kernel master PD */
    // arch_set_kernel_pdir((uint32_t)boot_pdir);
    arch_set_kernel_pdir((uint32_t)pdes);

    /* Step 5: Initialise the physical memory manager */
    pmm_init(total_memory, __kernel_end);
    KLOG("Paging: bootstrap complete, %u pages free", pmm_get_free_page_count());
}

uint32_t arch_get_kernel_pdir(void)
{
    return kernel_pdir_phys;
}

void arch_load_cr3(uint32_t pdir_phys)
{
    /* Writing CR3 flushes the TLB for all non-global entries */
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pdir_phys) : "memory");
}

uint32_t arch_get_cr3(void)
{
    uint32_t cr3;
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
        "mov %%cr0, %%eax\n\t"
        "or  $0x80000000, %%eax\n\t"
        "mov %%eax, %%cr0\n\t"
        "jmp 1f\n\t"
        "1:\n\t"
        : : : "eax", "memory"
    );
}

uint32_t arch_clone_kernel_pde(uint32_t pde_pa)
{
    if (!pde_pa) {
        KLOG("VMM: failed to allocate page for PDE clone");
        return 0;
    }

    pde_t* new_pde = pdir_of(pde_pa);
    pde_t* kern_pde = pdir_of(kernel_pdir_phys);

    /* Clone all PDEs from the kernel master PD */
    for (uint32_t i = 0; i < 1024; i++)
        new_pde[i].raw = kern_pde[i].raw;

    return pde_pa;
}

void arch_destroy_address_space(uint32_t pdir_phys)
{
    // pde_t* pdir;
    // pde_t* kern_pdir;
    // uint32_t i, j;

    // if (!pdir_phys || pdir_phys == kernel_pdir_phys)
    //     return;  /* never destroy the kernel master PD */

    // pdir = pdir_of(pdir_phys);
    // kern_pdir = pdir_of(kernel_pdir_phys);

    // /*
    //  * Walk all PDEs.  If a PDE points to the same page table as the
    //  * kernel master PD, it is shared across all address spaces and
    //  * must NOT be freed.  Only free page tables that were privately
    //  * allocated for this address space.
    //  */
    // for (i = 0; i < 1024; i++) {
    //     if (!pdir[i].present)
    //         continue;

    //     /*
    //      * 4MB pages map a large physical region directly; they have no
    //      * page table to walk or free.  Skip them — only process 4KB PDEs.
    //      */
    //     if (pdir[i].page_size)
    //         continue;

    //     /* Skip kernel-shared page tables (same physical address as kernel PD) */
    //     if (pdir[i].paddr == kern_pdir[i].paddr)
    //         continue;

    //     pte_t* ptbl = ptbl_of_pde(&pdir[i]);

    //     /* Free every physical page referenced by this private PT */
    //     for (j = 0; j < 1024; j++) {
    //         if (ptbl[j].present) {
    //             pmm_free_page(ptbl[j].paddr << 12);
    //             ptbl[j].raw = 0;
    //         }
    //     }

    //     /* Free the page table itself */
    //     pmm_free_page(pdir[i].paddr << 12);
    //     pdir[i].raw = 0;
    // }

    // /* Free the page directory */
    // pmm_free_page(pdir_phys);
}

// int arch_map_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
//                  uint32_t flags)
// {
//     uint32_t pd_idx = PD_INDEX(vaddr);
//     uint32_t pt_idx = PT_INDEX(vaddr);
//     pde_t* pdir;
//     pte_t* ptbl;

//     paddr &= PAGE_MASK;
//     vaddr &= PAGE_MASK;

//     pdir = pdir_of(pdir_phys);

//     /* Allocate a new page table if this PDE is not yet present */
//     if (!pdir[pd_idx].present) {
//         uint32_t pt_phys = pmm_alloc_page();
//         if (!pt_phys) {
//             KLOG("VMM: failed to allocate page table for vaddr 0x%x", vaddr);
//             return -1;
//         }

//         pdir[pd_idx].raw = 0;
//         pdir[pd_idx].present = 1;
//         pdir[pd_idx].rw      = 1;
//         pdir[pd_idx].user    = (flags & PTE_USER) ? 1 : 0;
//         pdir[pd_idx].paddr   = pt_phys >> 12;

//         ptbl = ptbl_of_phys(pt_phys);
//         memset(ptbl, 0, PAGE_SIZE);
//     } else {
//         ptbl = ptbl_of_pde(&pdir[pd_idx]);
//     }

//     /* Fill in the PTE */
//     ptbl[pt_idx].raw = 0;
//     ptbl[pt_idx].present = (flags & PTE_PRESENT) ? 1 : 0;
//     ptbl[pt_idx].rw      = (flags & PTE_RW)      ? 1 : 0;
//     ptbl[pt_idx].user    = (flags & PTE_USER)    ? 1 : 0;
//     ptbl[pt_idx].paddr   = paddr >> 12;

//     tlb_invlpg(vaddr);
//     return 0;
// }

// void arch_unmap_page(uint32_t pdir_phys, uint32_t vaddr)
// {
//     uint32_t pd_idx = PD_INDEX(vaddr);
//     uint32_t pt_idx = PT_INDEX(vaddr);
//     pde_t* pdir;
//     pte_t* ptbl;

//     vaddr &= PAGE_MASK;
//     pdir = pdir_of(pdir_phys);

//     if (!pdir[pd_idx].present)
//         return;

//     ptbl = ptbl_of_pde(&pdir[pd_idx]);
//     if (!ptbl[pt_idx].present)
//         return;

//     pmm_free_page(ptbl[pt_idx].paddr << 12);
//     ptbl[pt_idx].raw = 0;
//     tlb_invlpg(vaddr);
// }

// int arch_map_range(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
//                   uint32_t size, uint32_t flags)
// {
//     uint32_t off;
//     for (off = 0; off < size; off += PAGE_SIZE) {
//         if (arch_map_page(pdir_phys, vaddr + off, paddr + off, flags))
//             return -1;
//     }
//     return 0;
// }

// uint32_t arch_virt_to_phys(uint32_t pdir_phys, uint32_t vaddr)
// {
//     uint32_t pd_idx = PD_INDEX(vaddr);
//     uint32_t pt_idx = PT_INDEX(vaddr);
//     pde_t* pdir;

//     if (!pdir_phys)
//         pdir_phys = arch_get_cr3();

//     pdir = pdir_of(pdir_phys);
//     if (!pdir[pd_idx].present)
//         return 0;

//     pte_t* ptbl = ptbl_of_pde(&pdir[pd_idx]);
//     if (!ptbl[pt_idx].present)
//         return 0;

//     return (ptbl[pt_idx].paddr << 12) | PAGE_OFFSET(vaddr);
// }

// void* arch_alloc_user_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t flags)
// {
//     uint32_t paddr;

//     vaddr &= PAGE_MASK;

//     /* If already mapped, unmap first */
//     uint32_t old = arch_virt_to_phys(pdir_phys, vaddr);
//     if (old) {
//         arch_unmap_page(pdir_phys, vaddr);
//     }

//     paddr = pmm_alloc_page();
//     if (!paddr)
//         return 0;

//     if (arch_map_page(pdir_phys, vaddr, paddr, flags)) {
//         pmm_free_page(paddr);
//         return 0;
//     }

//     return (void*)vaddr;
// }

/*
 * Register the kernel master page directory.
 * Called by arch_paging_init() once the bootstrap PD is built.
 */
void arch_set_kernel_pdir(uint32_t pdir_phys)
{
    kernel_pdir_phys = pdir_phys;
    KLOG("VMM: kernel page directory at physical 0x%x", pdir_phys);
}
