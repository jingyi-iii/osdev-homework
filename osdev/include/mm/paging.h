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

#define IS_PAGE_ALIGNED(x) \
    (((uint32_t)(x) & (PAGE_SIZE - 1)) == 0)

/*
 * Kernel virtual address space layout (32-bit, 4GB total):
 *   0x00000000 - 0xBFFFFFFF  (0~3GB)    : User space
 *   0xC0000000 - 0xFFFFFFFF  (3GB~4GB)  : Kernel space (identity-mapped to physical 0~1GB)
 */
#define KERNEL_BASE_VADDR   0xC0000000
#define USER_SPACE_TOP      KERNEL_BASE_VADDR

#endif /* PAGING_H */
