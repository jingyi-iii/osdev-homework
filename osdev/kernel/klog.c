/*
 * kernel/klog.c — ring-0 direct-to-COM1 logger.
 *
 * The kernel's own serial log path, fully decoupled from the user-mode
 * log server: no log_device struct, no kernel spinlock.  klog talks
 * straight to the COM1 port through the io layer (kernel/io.c); at ring-0
 * iowrite8()/ioread8() are plain inb/outb, so this is direct port I/O.
 *
 * klog_* is ring-0 only (it runs cli/sti, which is not available at CPL3
 * with IOPL=0).  Ring-3 LOG output is handled by log_server.c.
 */

#include "kernel/klog.h"
#include "kernel/io.h"

#define KLOG_COM1_BASE      0x3F8
#define KLOG_LSR_OFF        5      /* Line Status Register offset      */
#define KLOG_LSR_THR_EMPTY  0x20   /* Transmitter Holding Register empty */

static int klog_inited = 0;

/* Bring up COM1 at 38400 8N1.  Idempotent; also called lazily on the
 * first write so very early LOG() calls (before kernel_start calls
 * log_init()) still reach the port. */
void klog_init(void)
{
    iowrite8(KLOG_COM1_BASE + 1, 0x00);   /* Disable all interrupts          */
    iowrite8(KLOG_COM1_BASE + 3, 0x80);   /* Enable DLAB (baud rate divisor) */
    iowrite8(KLOG_COM1_BASE + 0, 0x03);   /* Divisor lo = 3  → 38400 baud    */
    iowrite8(KLOG_COM1_BASE + 1, 0x00);   /* Divisor hi                      */
    iowrite8(KLOG_COM1_BASE + 3, 0x03);   /* 8 bits, no parity, one stop bit */
    iowrite8(KLOG_COM1_BASE + 2, 0xC7);   /* Enable FIFO, 14-byte threshold  */
    iowrite8(KLOG_COM1_BASE + 4, 0x0B);   /* IRQs enabled, RTS/DSR set       */
    iowrite8(KLOG_COM1_BASE + 4, 0x0F);   /* Normal operation (not loopback) */
    klog_inited = 1;
}

/* Single-character write.  Caller must hold IF=0 (klog_write/putc do). */
static void klog_putc_locked(char c)
{
    int timeout = 0;

    while ((ioread8(KLOG_COM1_BASE + KLOG_LSR_OFF) & KLOG_LSR_THR_EMPTY) == 0) {
        /* No UART / broken status line: drop the rest instead of hanging
         * forever. */
        if (++timeout > 1000000)
            return;
        __asm__ __volatile__("pause" ::: "memory");
    }
    iowrite8(KLOG_COM1_BASE, (u8)c);
}

void klog_putc(char c)
{
    u32 eflags;

    if (!klog_inited)
        klog_init();

    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    __asm__ __volatile__("cli" ::: "memory");
    klog_putc_locked(c);
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}

void klog_write(const char* s)
{
    u32 eflags;

    if (!s)
        return;

    if (!klog_inited)
        klog_init();

    /* Hold IF=0 for the whole message so an ISR-side klog (e.g. from a
     * fault or IRQ handler) cannot interleave bytes mid-message. */
    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    __asm__ __volatile__("cli" ::: "memory");
    while (*s)
        klog_putc_locked(*s++);
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}
