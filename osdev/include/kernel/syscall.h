#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/types.h"
#include "lib/list.h"
#include "mm/heap.h"
#include <stddef.h>

/* Handler registered for a handle.  `arg` is the value the caller passed in
 * ECX (for the current struct-based ABI this is a user pointer).  The
 * handler returns an int errno/status that is propagated back to the
 * caller through EAX (see arch_syscall). */
typedef int (*syscall_handler_fn)(void* arg);

typedef struct syscall {
    i32 handle;
    syscall_handler_fn fn;
    size_t max_param_size;
    u32 owner_cr3;      /* 0 = kernel handler; else PD of registering process */
    list_node this_node;
} syscall;

/*
 * Register a handler under a FIXED syscall number (see kernel/uapi.h)
 * and return the number back (>= 0), or a negative errno.  The numbers
 * are part of the user ABI — user-space ELF programs call them directly —
 * so the same number can never be registered twice.
 */
i32 syscall_register(i32 num, syscall_handler_fn fn, size_t max_param_size);

/*
 * Register a handler whose code lives in the CALLING user process's
 * address space (a "user syscall", e.g. the log server's SYSCALL_LOG).
 * @owner_cr3 is the registering process's page directory, which
 * syscall_dispatch() switches to before invoking the handler.
 */
i32 syscall_register_user(i32 num, syscall_handler_fn fn, size_t max_param_size,
                          u32 owner_cr3);
int syscall_unregister(i32 handle);

/* Dispatcher invoked by arch_syscall_entry (int $100).  Looks up the
 * handler registered for `handle` and calls it with `arg`. */
int syscall_dispatch(u32 handle, void* arg, size_t size);

/*
 * Safe user-memory copy helpers: validate that the range lies in user
 * space and is mapped user-accessible before copying.  Return 0 on
 * success, E_FAULT on a bad range.
 */
int copy_from_user(void* dst, const void* user_src, size_t n);
int copy_to_user(void* user_dst, const void* src, size_t n);

void syscall_init(void);
void syscall_exit(void);

#endif
