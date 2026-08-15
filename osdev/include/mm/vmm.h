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

/*
 * VMM syscall gate (major 100, minor VMM_SYSCALL_MINOR) for RING3 access.
 * vmm_alloc_pages / vmm_free_pages / vmm_map_memory / vmm_unmap_memory
 * transparently route through this gate when the caller runs in user mode
 * (CPL3), because the underlying page-table manipulation executes the
 * privileged invlpg instruction.  The handler runs in kernel context, so
 * the capability checks inside the kernel implementations still apply.
 */
#define VMM_SYSCALL_MINOR   (4)

/* VMM syscall commands */
typedef enum {
    VMM_CTRL_ALLOC_PAGES   = 0,
    VMM_CTRL_FREE_PAGES    = 1,
    VMM_CTRL_MAP_MEMORY    = 2,
    VMM_CTRL_UNMAP_MEMORY  = 3,
} vmm_syscall_cmd;

/* Data structure carried through the VMM syscall gate.
 * vcb / proc may be NULL to mean "the current process". */
typedef struct vmm_syscall_data {
    u8   cmd;                  /* vmm_syscall_cmd                  */
    vmm_control_block* vcb;    /* alloc/free: address space to use */
    pcb* proc;                 /* map/unmap: target process        */
    u32  page_cnt;             /* alloc: page count                */
    u32  flags;                /* alloc/map: page flags            */
    u32  phys_addr;            /* map: physical address            */
    size_t size;               /* map/unmap: size                  */
    void* va;                  /* free/unmap: virtual address      */
    void* ret_va;              /* out: VA (alloc/map)              */
    int   ret;                 /* out: return code                 */
} vmm_syscall_data;

void vmm_syscall_init(void);
void vmm_syscall_exit(void);

#endif /* VMM_H */
