#ifndef MEMORY_H
#define MEMORY_H

#include "lib/types.h"

typedef enum {
    MEM_MAP     = 0,
    MEM_UNMAP   = 1,
} mem_ctrl_cmd;

typedef struct mem_syscall_data {
    u8   cmd;
    u32  pa;    /* MAP:   in — physical address to map           */
    u32  size;  /* MAP/UNMAP: in — byte count                     */
    u32  va;    /* MAP:   out — kernel-chosen VA; UNMAP: in — VA  */
    int  ret;   /* out: 0 / errno                                 */
} mem_syscall_data;

/* MAP: the kernel picks a free VA; on success *out_va = mapping address.
 * UNMAP: undo a previous MAP by its VA. */
int memory_map(u32 pa, u32 size, u32* out_va);
int memory_unmap(u32 va, u32 size);

#endif
