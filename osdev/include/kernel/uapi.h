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

/*
 * Well-known portal id: the terminal server publishes its console portal
 * under this fixed id (portal_init_fixed) so separately-linked user ELF
 * programs can print via portal_call() without linking a kernel symbol.
 */
#define PORTAL_ID_CONSOLE   1

#endif
