#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/types.h"
#include <stddef.h>

/* Capacity of the kernel-side dispatch table.  Slots are allocated
 * dynamically: syscall_register() returns an opaque handle (a slot
 * number) that callers pass to arch_syscall(). */
#define SYSCALL_MAX_HANDLES     (16)

/* Handler registered for a handle.  `arg` is the value the caller passed in
 * ECX (for the current struct-based ABI this is a user pointer). */
typedef void (*syscall_handler_fn)(void* arg);

/*
 * Register a handler and get an opaque handle back (>= 0), or a negative
 * errno.  The handle is allocated by this function — callers do NOT pick
 * their own minor number.  Called only at init time by kernel subsystems;
 * never reachable from user mode, so user processes cannot register
 * syscall handlers.
 */
i32 syscall_register(syscall_handler_fn fn);
int syscall_unregister(i32 handle);

/* Dispatcher invoked by arch_syscall_entry (int $100).  Looks up the
 * handler registered for `handle` and calls it with `arg`. */
void syscall_dispatch(u32 handle, void* arg);

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
