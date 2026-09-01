#ifndef ARCH_MEM_H
#define ARCH_MEM_H

#include "lib/types.h"
#include <stddef.h>

/*
 * Validate that [addr, addr+n) lies in user space and is mapped with the
 * required permissions in the CURRENT page table.  Callers run in the
 * target address space, so CR3 is its directory.
 *
 * x86-specific: the PDE/PTE walk lives in arch/i386/mem.c.
 * Returns 1 if the whole range is ok, 0 otherwise.
 */
int arch_validate_user_range(const void* user_addr, size_t n, int for_write);

#endif
