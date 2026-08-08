#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stddef.h>
#include "mm/paging.h"
#include "lib/rbtree.h"
#include "sync/spinlock.h"

typedef struct pcb pcb;

typedef struct vmm_control_block {
    rbtree*     tree;
    uint32_t    cr3;
    spinlock*   lock;
} vmm_control_block;

typedef struct vmm_region {
    rbnode      node;
    void*       start_va;
    uint32_t    size;
    uint32_t    flags;
    uint32_t    pa;             /* physical address for pmm_free_page */
    int         own_phys;
} vmm_region;

void vmm_switch(vmm_control_block* vcb);
int vmm_create(vmm_control_block* vcb, int user_accessible);
void vmm_destroy(vmm_control_block* vcb);

void* vmm_alloc_pages(vmm_control_block* vcb, uint32_t page_cnt, uint32_t flags);
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
#define VMM_ERR_PTR(err)    ((void*)(intptr_t)(err))
#define VMM_PTR_ERR(ptr)    ((int)(intptr_t)(ptr))
#define VMM_IS_ERR(ptr)     ((uintptr_t)(ptr) >= (uintptr_t)-4095)

void* vmm_map_memory(pcb* proc, uint32_t phys_addr, size_t size, uint32_t flags);
int   vmm_unmap_memory(pcb* proc, void* virt_addr, size_t size);

int vmm_lookup_region(pcb* proc, uint32_t va, uint32_t* out_pa, uint32_t* out_pa_size);
uint32_t vmm_va_to_pa(pcb* proc, uint32_t va);

#endif /* VMM_H */
