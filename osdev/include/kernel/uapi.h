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
#define SYSCALL_IRQ         1   /* IRQ request/release (kernel/irq.c)        */
#define SYSCALL_MAILBOX     2   /* mailbox IPC (kernel/ipc/mailbox.c)        */
#define SYSCALL_PORTAL      3   /* portal RPC (kernel/ipc/portal.c)          */
#define SYSCALL_HEAP        4   /* user-heap malloc/free (kernel/mm/heap.c)  */
#define SYSCALL_IO          5   /* port I/O (kernel/io.c)                    */
#define SYSCALL_MMIO        6   /* MMIO read/write (kernel/mmio.c)           */

/*
 * PORTAL_ID_NAMESPACE — the ONE fixed well-known portal id (bootstrap).
 * Every other service publishes a DYNAMIC portal id and registers itself
 * by name; clients resolve ids through namespace_server.elf.  The name
 * registry protocol (service names, ns_request) is a USER<->USER contract
 * and lives in user/ns_proto.h — it is NOT part of the kernel ABI.
 */
#define PORTAL_ID_NAMESPACE   1

/*
 * Mail magic for KERNEL-PRODUCED notifications.
 *
 * The mailbox is a dumb transport: mail.magic is an opaque tag the kernel
 * carries but never interprets.  Server-to-server messages define their own
 * formats (and any magic they want) inside their payloads.  The one
 * exception is the kernel's own IRQ notification (kernel/irq.c
 * dispatch_user_mode_irq): its payload is empty, so the kernel tags the
 * mail with MAIL_MAGIC_IRQ and the registering user thread checks it
 * (user/userlib.c user_irq_wait).
 */
#define MAIL_MAGIC_IRQ      0x66666666

#endif
