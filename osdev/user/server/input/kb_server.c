/*
 * user/server/input/kb_server.c — ring-3 PS/2 keyboard driver
 * (standalone user ELF).
 *
 * Registers keyboard IRQ 0x21 as a USER irq through SYSCALL_IRQ: on every
 * keypress the kernel ISR drops a MAIL_TYPE_IRQ mail into this thread's
 * mailbox (see kernel/irq.c dispatch_user_mode_irq).  The main loop
 * listens on its own mailbox (SYSCALL_MAILBOX with mb == NULL), drains the
 * 8042 output buffer, translates scancode-set-1 codes with a compact table
 * and writes the resulting key to COM1 for diagnostics.
 *
 * Cross-process key delivery (the old kb_register_callback API for games)
 * needs a kernel-side listener registry and is not implemented here yet.
 */

#include "userlib.h"          /* user_syscall / IRQ + mailbox helpers */
#include "kernel/uapi.h"
#include "kernel/io.h"        /* ioread8/iowrite8 (user_service.c) */
#include <stddef.h>

#define KB_IRQ_NO        0x21
#define KB_STATUS_PORT   0x64
#define KB_DATA_PORT     0x60

#define COM1_BASE        0x3F8
#define COM1_LSR_OFF     5
#define LSR_THR_EMPTY    0x20

/* Scancode set 1 → ASCII for the unshifted printable keys. */
static const char scancode_ascii[0x3A] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

/* Minimal COM1 writer for diagnostics (through the io syscall gate). */
static void kb_putc(char c)
{
    int timeout = 0;

    while ((ioread8(COM1_BASE + COM1_LSR_OFF) & LSR_THR_EMPTY) == 0) {
        if (++timeout > 1000000)
            return;
        __asm__ __volatile__("pause" ::: "memory");
    }
    iowrite8(COM1_BASE, (u8)c);
}

static void kb_log_key(char key)
{
    kb_putc('[');
    kb_putc(key);
    kb_putc(']');
    kb_putc('\n');
}

void _start(void)
{
    void* irq = user_irq_request(KB_IRQ_NO, 0);
    if (irq)
        user_irq_unmask(irq);

    for (;;) {
        void* m = user_mail_listen();
        if (!m)
            continue;

        if (((user_mail*)m)->type == USER_MAIL_TYPE_IRQ) {
            /* Drain the 8042 output buffer (bounded) so a burst of
             * scancodes under one IRQ cannot desynchronize anything. */
            int drained = 0;
            while (drained < 8) {
                u8 status = ioread8(KB_STATUS_PORT);
                if (!(status & 0x01))
                    break;
                u8 scancode = ioread8(KB_DATA_PORT);
                if (scancode < sizeof(scancode_ascii)) {
                    char key = scancode_ascii[scancode];
                    if (key)
                        kb_log_key(key);
                }
                drained++;
            }
        }

        user_mail_release(m);
    }
}
