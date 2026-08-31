#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

/*
 * kernel/log.h — pure-IO kernel logger.
 *
 * LOG() writes straight to the COM1 serial port with raw port I/O only:
 * no user-mode log server, no tags, no timestamps, no kernel data
 * structure.  Callable from CPL0 and CPL3 at any time — even during early
 * boot before the scheduler or the heap exist.
 *
 *   ring 0: klog_write() (kernel/klog.c) — cli-guarded so a log from an
 *           ISR can never interleave bytes into a message;
 *   ring 3: iowrite8()/ioread8() loop — the io layer routes CPL3 port I/O
 *           through the capability-checked SYSCALL_IO gate (IOPL=0).
 */

#include <stddef.h>
#include "lib/types.h"
#include "lib/string.h"      /* snprintf */
#include "kernel/klog.h"     /* klog_init / klog_write (ring-0 COM1) */
#include "kernel/io.h"       /* ioread8 / iowrite8 (CPL-agnostic) */
#include "arch_irq.h"        /* arch_running_ring3 */

#define LOG_COM1_BASE       0x3F8
#define LOG_LSR_OFF         5       /* Line Status Register offset */
#define LOG_LSR_THR_EMPTY   0x20    /* Transmitter Holding Register empty */

/* Bring up COM1 (idempotent; klog also lazily inits on first write). */
static inline void log_init(void)
{
    klog_init();
}

/*
 * Write one NUL-terminated string to COM1.  Pure port I/O, no timestamp.
 */
static inline void kernel_log_write(const char* s)
{
    if (!s)
        return;

    if (!arch_running_ring3()) {
        klog_write(s);
        return;
    }

    while (*s) {
        int timeout = 0;
        while ((ioread8(LOG_COM1_BASE + LOG_LSR_OFF) & LOG_LSR_THR_EMPTY) == 0) {
            /* No UART / broken status line: drop the rest instead of
             * hanging forever. */
            if (++timeout > 1000000)
                return;
            __asm__ __volatile__("pause" ::: "memory");
        }
        iowrite8(LOG_COM1_BASE, (u8)*s++);
    }
}

/*
 * LOG() — format and write to the serial port directly.
 */
#define LOG(fmt, ...)                                                    \
    do {                                                                 \
        char log_buf[256] = {0};                                         \
        snprintf(log_buf, sizeof(log_buf), fmt "\n", ##__VA_ARGS__);     \
        kernel_log_write(log_buf);                                       \
    } while (0)

#endif
