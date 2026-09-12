#ifndef MMIO_H
#define MMIO_H

#include "lib/types.h"

/*
 * MMIO window mapping syscall (SYSCALL_MMIO, kernel/mmio.c).
 *
 * Mirrored on the user side as user_mmio_ctrl (user/userlib.h) — keep
 * layout + command values in sync.
 */
typedef enum {
    MMIO_CTRL_MAP   = 0,
    MMIO_CTRL_UNMAP = 1,
} mmio_ctrl_cmd;

typedef struct mmio_syscall_data {
    u8   cmd;
    u32  pa;    /* MAP:   in — physical MMIO address            */
    u32  size;  /* MAP/UNMAP: in — byte count (page multiple)   */
    u32  va;    /* MAP/UNMAP: in — caller-chosen fixed VA       */
    int  ret;   /* out: 0 / errno                               */
} mmio_syscall_data;

/* MAP: map [pa, pa+size) at the caller-chosen fixed VA vaddr with
 * own_phys = 0 (the MMIO window is never returned to the PMM).
 * UNMAP: drop a previous MAP by its VA. */
int mmio_map(u32 pa, u32 size, void* vaddr);
int mmio_unmap(void* vaddr, u32 size);

void mmio_syscall_init(void);
void mmio_syscall_exit(void);

#endif
