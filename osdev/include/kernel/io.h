#ifndef __IO_H__
#define __IO_H__

#include "lib/types.h"
#include <stddef.h>
#include "lib/list.h"
#include "sync/spinlock.h"
#include "kernel/errno.h"

/*
 * Port I/O API.
 *
 * ioread8/16/32 and iowrite8/16/32 are safe to call from both kernel mode
 * (CPL0) and user mode (CPL3).  In user mode the privileged in/out
 * instructions are executed at ring 0 through the syscall gate
 * (int $100) instead of being done directly.
 */

u8  ioread8 (u16 port);
u16 ioread16(u16 port);
u32 ioread32(u16 port);

void iowrite8 (u16 port, u8  value);
void iowrite16(u16 port, u16 value);
void iowrite32(u16 port, u32 value);

/*
 * I/O syscall gate for RING3 access.  The handle is allocated by
 * syscall_register() (kernel/syscall.c) — callers never pick a number.
 *
 * CAP_ACCESS_IO is granted as a port RANGE {port_base, port_count}
 * (see drivers/platform/platform_devices.c).  The syscall handler simply
 * checks that the accessed port lies inside one of the process's granted
 * ranges; there is no per-access width concept.
 */

/* I/O syscall commands */
typedef enum {
    IO_CTRL_IN8   = 0,
    IO_CTRL_IN16  = 1,
    IO_CTRL_IN32  = 2,
    IO_CTRL_OUT8  = 3,
    IO_CTRL_OUT16 = 4,
    IO_CTRL_OUT32 = 5,
} io_syscall_cmd;

/* Data structure carried through the I/O syscall gate */
typedef struct io_syscall_data {
    u8   cmd;     /* io_syscall_cmd                       */
    u16  port;    /* I/O port number                      */
    u32  value;   /* in: value to write / out: value read */
    int  ret;     /* out: return code                     */
} io_syscall_data;

void io_syscall_init(void);
void io_syscall_exit(void);

#endif
