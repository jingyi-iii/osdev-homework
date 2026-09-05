#ifndef VMM_H
#define VMM_H

#include "lib/types.h"
#include <stddef.h>
#include "paging.h"
#include "lib/rbtree.h"
#include "sync/spinlock.h"

typedef struct pcb pcb;

typedef struct vmm_control_block {
    rbtree*     tree;
    u32    cr3;
    spinlock*   lock;
} vmm_control_block;

typedef struct vmm_region {
    rbnode      node;
    void*       start_va;
    u32    size;
    u32    flags;
    u32    pa;             /* physical address for pmm_free_page */
    int         own_phys;
} vmm_region;

void vmm_switch(vmm_control_block* vcb);
int vmm_create(vmm_control_block* vcb, int user_accessible);
void vmm_destroy(vmm_control_block* vcb);

void* vmm_alloc_pages(vmm_control_block* vcb, u32 page_cnt, u32 flags);
void vmm_free_pages(vmm_control_block* vcb, void* va);

/*
 * vmm_map_memory / vmm_unmap_memory operate on the current process.
 *
 * vmm_map_memory returns a valid user VA on success; on failure it returns
 * an error pointer.  Kernel errno values are negative, so the error pointers
 * live in the top 4KB of the 32-bit address space — a range the VA allocator
 * never hands out, so error pointers can never collide with a valid mapping.
 * Check with VMM_IS_ERR() and recover the code with VMM_PTR_ERR().
 */
#define VMM_ERR_PTR(err)    ((void*)(iptr)(err))
#define VMM_PTR_ERR(ptr)    ((int)(iptr)(ptr))
#define VMM_IS_ERR(ptr)     ((uptr)(ptr) >= (uptr)-4095)

void* vmm_map_memory(pcb* proc, u32 phys_addr, size_t size, u32 flags);
int   vmm_unmap_memory(pcb* proc, void* virt_addr, size_t size);

/*
 * vmm_map_fixed - map @size bytes of physical memory at a FIXED virtual
 * address (the caller's choice: an ELF segment's linked VA, or a ring-3
 * MMIO window the caller picks in the high user area).
 * Requires the caller to hold a CAP_MAP_MEM capability covering the range.
 * @own_phys: 1 = this mapping owns the pages — returned to the PMM on
 * address-space destroy (ELF loader), or via vmm_unmap_fixed(); 0 = pure
 * alias (MMIO windows) — never touched by the PMM, only unmapped.
 * Returns 0 or a negative errno.
 */
int vmm_map_fixed(pcb* proc, u32 phys_addr, void* vaddr, size_t size,
                  u32 flags, int own_phys);

/*
 * vmm_unmap_fixed - release a mapping created by vmm_map_fixed() at a
 * fixed VA.  Requires the caller to hold CAP_MAP_MEM over the region's
 * physical range.  Unmaps the PTEs; an own_phys = 1 region has its pages
 * returned to the PMM, an own_phys = 0 (MMIO) region just loses the alias.
 * Returns 0 or a negative errno.
 */
int vmm_unmap_fixed(pcb* proc, void* vaddr, size_t size);

int vmm_lookup_region(pcb* proc, u32 va, u32* out_pa, u32* out_pa_size,
                      void** out_start_va);
u32 vmm_va_to_pa(pcb* proc, u32 va);

/*
 * VMM is a KERNEL-INTERNAL service — there is deliberately NO ring-3
 * syscall gate (SYSCALL_VMM was removed from the user ABI).  Memory is
 * managed on behalf of user processes only by kernel modules: the ELF
 * loader (vmm_map_fixed), shared memory (shm_share / shm_unshare) and
 * thread-stack setup.  Ring-3 code that needs dynamic memory or sharing
 * goes through mailbox / portal / shm instead of touching page tables.
 */

#endif /* VMM_H */
