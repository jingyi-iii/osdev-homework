#include "mm/vmm.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "drivers/log_driver.h"

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

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

uint32_t vmm_get_kernel_pdir(void)
{
    return kernel_pdir_phys;
}

void vmm_load_cr3(uint32_t pdir_phys)
{
    /* Writing CR3 flushes the TLB for all non-global entries */
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pdir_phys) : "memory");
}

uint32_t vmm_get_cr3(void)
{
    uint32_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void vmm_enable_paging(void)
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

uint32_t vmm_clone_kernel_pde(void)
{
    uint32_t pde_pa = pmm_alloc_page();
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

// uint32_t vmm_create_address_space(void)
// {
//     uint32_t new_pdir_phys;
//     pde_t* new_pdir;
//     pde_t* kern_pdir;
//     uint32_t kernel_pd_start;
//     uint32_t i;

//     new_pdir_phys = pmm_alloc_page();
//     if (!new_pdir_phys) {
//         KLOG("VMM: failed to allocate page directory");
//         return 0;
//     }

//     new_pdir  = pdir_of(new_pdir_phys);
//     kern_pdir = pdir_of(kernel_pdir_phys);

//     /*
//      * Copy ALL PDEs from the kernel master page directory so that:
//      *   - Kernel identity-mapped physical memory (PDE[0..N]) stays
//      *     accessible while executing kernel code in this process.
//      *   - Higher-half mappings (PDE[768..1023]) are shared.
//      *
//      * User-space isolation is achieved by later mapping user pages
//      * (with U/S=1) over the appropriate PTEs.  Since kernel pages
//      * are marked supervisor (U/S=0), ring-3 code cannot access them
//      * even though their PDEs are present.
//      */
//     for (i = 0; i < 1024; i++)
//         new_pdir[i].raw = kern_pdir[i].raw;

//     return new_pdir_phys;
// }

void vmm_destroy_address_space(uint32_t pdir_phys)
{
    pde_t* pdir;
    pde_t* kern_pdir;
    uint32_t i, j;

    if (!pdir_phys || pdir_phys == kernel_pdir_phys)
        return;  /* never destroy the kernel master PD */

    pdir = pdir_of(pdir_phys);
    kern_pdir = pdir_of(kernel_pdir_phys);

    /*
     * Walk all PDEs.  If a PDE points to the same page table as the
     * kernel master PD, it is shared across all address spaces and
     * must NOT be freed.  Only free page tables that were privately
     * allocated for this address space.
     */
    for (i = 0; i < 1024; i++) {
        if (!pdir[i].present)
            continue;

        /* Skip kernel-shared page tables (same physical address as kernel PD) */
        if (pdir[i].paddr == kern_pdir[i].paddr)
            continue;

        pte_t* ptbl = ptbl_of_pde(&pdir[i]);

        /* Free every physical page referenced by this private PT */
        for (j = 0; j < 1024; j++) {
            if (ptbl[j].present) {
                pmm_free_page(ptbl[j].paddr << 12);
                ptbl[j].raw = 0;
            }
        }

        /* Free the page table itself */
        pmm_free_page(pdir[i].paddr << 12);
        pdir[i].raw = 0;
    }

    /* Free the page directory */
    pmm_free_page(pdir_phys);
}

int vmm_map_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
                 uint32_t flags)
{
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);
    pde_t* pdir;
    pte_t* ptbl;

    paddr &= PAGE_MASK;
    vaddr &= PAGE_MASK;

    pdir = pdir_of(pdir_phys);

    /* Allocate a new page table if this PDE is not yet present */
    if (!pdir[pd_idx].present) {
        uint32_t pt_phys = pmm_alloc_page();
        if (!pt_phys) {
            KLOG("VMM: failed to allocate page table for vaddr 0x%x", vaddr);
            return -1;
        }

        pdir[pd_idx].raw = 0;
        pdir[pd_idx].present = 1;
        pdir[pd_idx].rw      = 1;
        pdir[pd_idx].user    = (flags & PTE_USER) ? 1 : 0;
        pdir[pd_idx].paddr   = pt_phys >> 12;

        ptbl = ptbl_of_phys(pt_phys);
        memset(ptbl, 0, PAGE_SIZE);
    } else {
        ptbl = ptbl_of_pde(&pdir[pd_idx]);
    }

    /* Fill in the PTE */
    ptbl[pt_idx].raw = 0;
    ptbl[pt_idx].present = (flags & PTE_PRESENT) ? 1 : 0;
    ptbl[pt_idx].rw      = (flags & PTE_RW)      ? 1 : 0;
    ptbl[pt_idx].user    = (flags & PTE_USER)    ? 1 : 0;
    ptbl[pt_idx].paddr   = paddr >> 12;

    tlb_invlpg(vaddr);
    return 0;
}

void vmm_unmap_page(uint32_t pdir_phys, uint32_t vaddr)
{
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);
    pde_t* pdir;
    pte_t* ptbl;

    vaddr &= PAGE_MASK;
    pdir = pdir_of(pdir_phys);

    if (!pdir[pd_idx].present)
        return;

    ptbl = ptbl_of_pde(&pdir[pd_idx]);
    if (!ptbl[pt_idx].present)
        return;

    pmm_free_page(ptbl[pt_idx].paddr << 12);
    ptbl[pt_idx].raw = 0;
    tlb_invlpg(vaddr);
}

int vmm_map_range(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
                  uint32_t size, uint32_t flags)
{
    uint32_t off;
    for (off = 0; off < size; off += PAGE_SIZE) {
        if (vmm_map_page(pdir_phys, vaddr + off, paddr + off, flags))
            return -1;
    }
    return 0;
}

uint32_t vmm_virt_to_phys(uint32_t pdir_phys, uint32_t vaddr)
{
    uint32_t pd_idx = PD_INDEX(vaddr);
    uint32_t pt_idx = PT_INDEX(vaddr);
    pde_t* pdir;

    if (!pdir_phys)
        pdir_phys = vmm_get_cr3();

    pdir = pdir_of(pdir_phys);
    if (!pdir[pd_idx].present)
        return 0;

    pte_t* ptbl = ptbl_of_pde(&pdir[pd_idx]);
    if (!ptbl[pt_idx].present)
        return 0;

    return (ptbl[pt_idx].paddr << 12) | PAGE_OFFSET(vaddr);
}

void* vmm_alloc_user_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t flags)
{
    uint32_t paddr;

    vaddr &= PAGE_MASK;

    /* If already mapped, unmap first */
    uint32_t old = vmm_virt_to_phys(pdir_phys, vaddr);
    if (old) {
        vmm_unmap_page(pdir_phys, vaddr);
    }

    paddr = pmm_alloc_page();
    if (!paddr)
        return 0;

    if (vmm_map_page(pdir_phys, vaddr, paddr, flags)) {
        pmm_free_page(paddr);
        return 0;
    }

    return (void*)vaddr;
}

/*
 * Register the kernel master page directory.
 * Called by arch_paging_init() once the bootstrap PD is built.
 */
void vmm_set_kernel_pdir(uint32_t pdir_phys)
{
    kernel_pdir_phys = pdir_phys;
    KLOG("VMM: kernel page directory at physical 0x%x", pdir_phys);
}
