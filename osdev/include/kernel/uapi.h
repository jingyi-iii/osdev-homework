#ifndef KERNEL_UAPI_H
#define KERNEL_UAPI_H

/*
 * User ABI — fixed syscall numbers and config structs shared verbatim
 * between the kernel and user-space ELF programs.
 *
 * Syscall convention (int $100):
 *   ebx = syscall number, ecx = config pointer, edx = config size,
 *   eax = return value.
 *
 * Unlike the old runtime-allocated handles, these numbers are FIXED so
 * that separately-linked user ELF programs can call into the kernel
 * without linking against kernel symbols.
 */

#include "lib/types.h"

#define SYSCALL_PROC_THREAD 0   /* process/thread control (kernel/process.c) */
#define SYSCALL_IO          1   /* port I/O (kernel/io.c)                    */
#define SYSCALL_VMM         2   /* memory alloc/map (kernel/mm/vmm.c)        */
#define SYSCALL_IRQ         3   /* IRQ request/release (kernel/irq.c)        */
#define SYSCALL_MAILBOX     4   /* mailbox IPC (kernel/ipc/mailbox.c)        */
#define SYSCALL_PORTAL      5   /* portal RPC (kernel/ipc/portal.c)          */
#define SYSCALL_SYSCTL      6   /* register/unregister a user syscall (kernel/syscall.c) */
#define SYSCALL_LOG         7   /* LOG via the user-mode log server (user/server/serial/log_server2.c) */

/*
 * SYSCALL_SYSCTL — user-mode syscall registry.
 *
 * Lets a ring-3 server (e.g. the log server ELF) claim a FIXED syscall
 * number with a handler that lives in its own address space.  The kernel
 * stores the handler together with the registering process's page
 * directory; syscall_dispatch() switches CR3 to it before invoking the
 * handler, so the handler's code and globals are mapped while it runs at
 * ring 0, then switches back to the caller.
 */
enum {
    U_SYSCTL_REGISTER = 0,
    U_SYSCTL_UNREGISTER,
};

typedef struct user_sysctl_config {
    i32  cmd;              /* U_SYSCTL_REGISTER / U_SYSCTL_UNREGISTER */
    i32  num;              /* fixed syscall number to claim / release  */
    void* fn;              /* handler fn (must live below USER_SPACE_TOP) */
    u32  max_param_size;   /* config struct size the handler expects    */
    i32  ret;              /* out: 0 or negative errno                  */
} user_sysctl_config;

/*
 * SYSCALL_LOG — the payload is carried INLINE in the config struct, so the
 * handler (which runs with the log server's page tables) can read it from
 * the kernel's copy without dereferencing the caller's address space.
 */
typedef struct user_log_config {
    u32  size;             /* bytes in data[] (<= sizeof(data)) */
    char data[256];
    i32  ret;              /* out: 0 or negative errno */
} user_log_config;

/*
 * Well-known portal id: the terminal server publishes its console portal
 * under this fixed id (portal_init_fixed) so separately-linked user ELF
 * programs can print via portal_call() without linking a kernel symbol.
 */
#define PORTAL_ID_CONSOLE   1

#endif
