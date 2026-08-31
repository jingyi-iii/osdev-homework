#ifndef KERNEL_KTERM_H
#define KERNEL_KTERM_H

/*
 * Kernel terminal — ring-0 direct-to-VGA text-mode output.
 *
 * Decoupled from the user-mode terminal server
 * (drivers/display/terminal_server.c): kterm owns the VGA text buffer
 * (0xB8000) and the CRT controller ports (0x3D4/0x3D5) and drives them
 * with raw memory-mapped writes plus port I/O through the io layer
 * (kernel/io.c).  No terminal_device struct, no platform bus, no
 * uspinlock object.  A whole kterm_write() is guarded with cli/sti so an
 * ISR-side writer (fault/IRQ handler) can never interleave bytes into a
 * message on this single-CPU kernel.
 *
 * kterm_* is ring-0 ONLY (it executes cli/sti, which #GPs at CPL3 with
 * IOPL=0).  Ring-3 screen output goes through the user-mode terminal
 * server path in terminal_server.c instead.
 */

#include "lib/types.h"

void kterm_init(void);                  /* bring up VGA text mode (idempotent; also lazy) */
void kterm_putc(char c);                /* write one character (handles \n and \b) */
void kterm_write(const char* s);        /* write a NUL-terminated string */
void kterm_write_color(const char* s, u8 color); /* write a string with a raw attribute byte */
void kterm_clear(void);                 /* clear the screen and reset the cursor */
void kterm_switch_to_text_mode(void);   /* reprogram VGA for mode 0x03 (80x25) */

#endif
