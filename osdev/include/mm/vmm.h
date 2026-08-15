#ifndef VMM_H
#define VMM_H

#include "lib/types.h"
#include <stddef.h>
#include "mm/paging.h"
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

int vmm_lookup_region(pcb* proc, u32 va, u32* out_pa, u32* out_pa_size);
u32 vmm_va_to_pa(pcb* proc, u32 va);

#endif /* VMM_H */
