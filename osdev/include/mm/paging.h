#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE           4096
#define PAGE_MASK           0xFFFFF000

/* Page Directory Entry (PDE) */
typedef union pde {
    uint32_t raw;
    struct {
        uint32_t present    : 1;   /* 0: Page is present in memory */
        uint32_t rw         : 1;   /* 1: 0=Read-only, 1=Read/Write */
        uint32_t user       : 1;   /* 2: 0=Supervisor, 1=User */
        uint32_t pwt        : 1;   /* 3: Page-level Write-Through */
        uint32_t pcd        : 1;   /* 4: Page-level Cache Disable */
        uint32_t accessed   : 1;   /* 5: Accessed */
        uint32_t dirty      : 1;   /* 6: Dirty (4MB pages only, zero otherwise) */
        uint32_t page_size  : 1;   /* 7: 0=4KB page, 1=4MB page */
        uint32_t global     : 1;   /* 8: Global (ignored for 4KB PTs) */
        uint32_t avail      : 3;   /* 9-11: Available for OS use */
        uint32_t paddr      : 20;  /* 12-31: Physical address of page table (4KB aligned) */
    } __attribute__((packed));
} pde_t;

/* Page Table Entry (PTE) */
typedef union pte {
    uint32_t raw;
    struct {
        uint32_t present    : 1;   /* 0: Page is present */
        uint32_t rw         : 1;   /* 1: Read/Write */
        uint32_t user       : 1;   /* 2: User/Supervisor */
        uint32_t pwt        : 1;   /* 3: Page-level Write-Through */
        uint32_t pcd        : 1;   /* 4: Page-level Cache Disable */
        uint32_t accessed   : 1;   /* 5: Accessed */
        uint32_t dirty      : 1;   /* 6: Dirty */
        uint32_t pat        : 1;   /* 7: PAT (0 for 4KB pages) */
        uint32_t global     : 1;   /* 8: Global */
        uint32_t avail      : 3;   /* 9-11: Available for OS use */
        uint32_t paddr      : 20;  /* 12-31: Physical address of 4KB page */
    } __attribute__((packed));
} pte_t;

/* Page flags */
#define PTE_PRESENT     (1 << 0)
#define PTE_RW          (1 << 1)
#define PTE_USER        (1 << 2)
#define PTE_PWT         (1 << 3)
#define PTE_PCD         (1 << 4)
#define PTE_ACCESSED    (1 << 5)
#define PTE_DIRTY       (1 << 6)
#define PTE_GLOBAL      (1 << 8)

/* Common combinations */
#define PTE_KERNEL      (PTE_PRESENT | PTE_RW)             /* supervisor r/w */
#define PTE_KERNEL_RO   (PTE_PRESENT)                       /* supervisor r/o */
#define PTE_USER_PAGE   (PTE_PRESENT | PTE_RW | PTE_USER)  /* user r/w */
#define PTE_USER_RO     (PTE_PRESENT | PTE_USER)            /* user r/o */

/* Address decomposition */
#define PD_INDEX(vaddr)     (((uint32_t)(vaddr) >> 22) & 0x3FF)
#define PT_INDEX(vaddr)     (((uint32_t)(vaddr) >> 12) & 0x3FF)
#define PAGE_OFFSET(vaddr)  ((uint32_t)(vaddr) & 0xFFF)
#define PAGE_ALIGN(x)       (((uint32_t)(x) + PAGE_SIZE - 1) & PAGE_MASK)

#define IS_4MB_ALIGN(x) \
    (((uint32_t)(x) & (0x400000 - 1)) == 0)

#define IS_4KB_ALIGN(x) \
    (((uint32_t)(x) & (4096 - 1)) == 0)

#define IS_PAGE_ALIGNED(x) \
    (((uint32_t)(x) & (PAGE_SIZE - 1)) == 0)

/*
 * Kernel virtual address space layout (32-bit, 4GB total):
 *   0x00000000 - 0xBFFFFFFF  (0~3GB)    : User space
 *   0xC0000000 - 0xFFFFFFFF  (3GB~4GB)  : Kernel space (identity-mapped to physical 0~1GB)
 */
#define KERNEL_BASE_VADDR   0xC0000000
#define USER_SPACE_TOP      KERNEL_BASE_VADDR

/**
 * arch_load_cr3 - Load a page directory into CR3.
 * This automatically flushes the TLB for non-global pages.
 */
void arch_load_cr3(uint32_t pdir_phys);

/**
 * arch_get_cr3 - Return the current CR3 value.
 */
uint32_t arch_get_cr3(void);

/**
 * vmm_create - Create a new user address space.
 *
 * Allocates a new page directory and copies the kernel-space PDEs
 * (KERNEL_BASE_VADDR..4GB) from the kernel master page directory.
 * User-space PDEs (0..KERNEL_BASE_VADDR) are zero-initialised.
 *
 * Returns the physical address of the new page directory, or 0 on failure.
 */
// uint32_t vmm_create(void);

uint32_t arch_clone_kernel_pde(uint32_t pde_pa, int user_accessible);

/**
 * arch_destroy_address_space - Destroy a user address space.
 *
 * Frees all user page tables and the pages they reference, then
 * frees the page directory itself.  Kernel-shared page tables are
 * left untouched.
 */
void arch_destroy_address_space(uint32_t pdir_phys);

/**
 * arch_map_page - Map a single virtual page to a physical page.
 *
 * @pdir_phys:  Physical address of the target page directory.
 * @vaddr:      Virtual address to map (will be page-aligned).
 * @paddr:      Physical address to map to (will be page-aligned).
 * @flags:      PTE flags (PTE_KERNEL, PTE_USER_PAGE, etc.).
 *
 * Returns 0 on success, -1 if a page table allocation fails.
 */
int arch_map_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
                 uint32_t flags);

/**
 * arch_unmap_page - Unmap a single virtual page (also frees the physical page).
 */
void arch_unmap_page(uint32_t pdir_phys, uint32_t vaddr);

/**
 * arch_map_range - Map a contiguous range.
 * Returns 0 on success, -1 on failure.
 */
int arch_map_range(uint32_t pdir_phys, uint32_t vaddr, uint32_t paddr,
                  uint32_t size, uint32_t flags);

/**
 * arch_virt_to_phys - Translate a virtual address to physical.
 * @pdir_phys: Page directory to use (0 = use current CR3).
 */
uint32_t arch_virt_to_phys(uint32_t pdir_phys, uint32_t vaddr);

/**
 * arch_alloc_user_page - Allocate a physical page and map it into the
 * given address space at the specified user virtual address.
 * Returns the virtual address on success, 0 on failure.
 */
void* arch_alloc_user_page(uint32_t pdir_phys, uint32_t vaddr, uint32_t flags);

/**
 * arch_enable_paging - Set CR0.PG, enabling paging.
 * Must be called with the initial page directory already built and
 * loaded into CR3.
 */
void arch_enable_paging(void);

/**
 * arch_paging_init - Architecture-specific paging bootstrap.
 *
 * Builds the initial kernel page directory (identity-mapping all
 * physical memory + higher-half kernel mapping at 3GB), initialises
 * the PMM, and enables paging.
 *
 * @total_memory:  Total physical memory in bytes.
 * @reserved_end:  Physical address of the first byte AFTER all
 *                 bootstrap structures (PD, PT0, kernel BSS end).
 */
void arch_paging_init(uint32_t total_memory, uint32_t reserved_end);

void arch_map_4mb(void* cr3, void* va, void* pa, uint32_t flags);
void arch_unmap_4mb(void* cr3, void* va);

void arch_map_4kb(void* cr3, void* va, void* pa, uint32_t flags);
void arch_unmap_4kb(void* cr3, void* va);

#endif /* PAGING_H */
