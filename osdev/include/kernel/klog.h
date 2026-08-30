#ifndef KERNEL_KLOG_H
#define KERNEL_KLOG_H

/*
 * Kernel log — ring-0 direct-to-COM1 logger.
 *
 * Decoupled from the user-mode log server (drivers/serial/log_server.c):
 * klog owns the COM1 port and writes to it with raw port I/O (via the io
 * layer, kernel/io.c), so kernel logging needs no kernel data structure —
 * no log_device struct, no spinlock object.  A whole klog_write() is
 * guarded with cli/sti so an ISR-side log (fault/IRQ handler) can never
 * interleave bytes into a message on this single-CPU kernel.
 *
 * klog_* is ring-0 ONLY (it executes cli/sti, which #GPs at CPL3 with
 * IOPL=0).  Ring-3 log output goes through the user-mode log server path
 * in log_server.c instead.
 */

void klog_init(void);        /* bring up COM1 (idempotent; also lazy) */
void klog_putc(char c);      /* write one character */
void klog_write(const char* s);  /* write a NUL-terminated string */

#endif
