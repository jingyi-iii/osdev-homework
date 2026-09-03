/*
 * user/demo/demo_kmalloc.c — kmalloc/kfree shims so kernel-library code
 * linked into a demo ELF (kernel/lib/rbtree.c allocates its tree + nil
 * node) gets memory from the user-mode heap instead of the kernel heap.
 */
#include "user/userheap.h"

void* kmalloc(unsigned int size)
{
    return malloc(size);
}

void kfree(void* ptr)
{
    free(ptr);
}
