#ifndef HEAP_H
#define HEAP_H

#include "lib/types.h"

/* ---- kernel heap (kmalloc, low identity pool) ---- */
void* kmalloc(unsigned int alloc_size);
void kfree(void* pointer);

/* ---- user heap (umalloc/malloc, shared user region) ---- */
void* malloc(unsigned int alloc_size);
void  free(void* pointer);

/*
 * User-heap syscall (SYSCALL_HEAP).  Kernel-side config struct; the
 * user mirrors it as user_heap_ctrl in user/userlib.h (layout + command
 * values must stay in sync).
 */
#define HEAP_CTRL_MALLOC    0
#define HEAP_CTRL_FREE      1

typedef struct heap_ctrl_config {
    u32  cmd;      /* HEAP_CTRL_MALLOC / HEAP_CTRL_FREE */
    u32  size;     /* MALLOC: bytes requested            */
    void* ptr;     /* MALLOC: out — new ptr; FREE: in    */
    int  ret;      /* out: 0 / errno                     */
} heap_ctrl_config;

void heap_syscall_init(void);

#endif
