/*
 * user/server/display/terminal_server.c — ring-3 console server
 * (standalone user ELF).
 *
 * Pure console-portal printer: publishes the well-known PORTAL_ID_CONSOLE
 * and prints every payload it receives straight to the VGA text buffer
 * (0xB8000 — the low identity map is user-accessible).  The interactive
 * command line is gone, so there is no kb dependency.
 *
 * VGA text cells are written with direct memory stores; the hardware
 * cursor is moved through the io syscall gate (ports 0x3D4/0x3D5, inside
 * the {0x3C0, 32} port grant given by the kernel at load time).
 *
 * Single-threaded: the portal loop is the only VGA writer after boot, so
 * no lock is needed.
 */

#include "userlib.h"          /* user_syscall / portal ABI               */
#include "kernel/uapi.h"      /* PORTAL_ID_CONSOLE                       */
#include "kernel/io.h"        /* ioread8/iowrite8 (provided by user_service.c) */
#include <stddef.h>

#define VGA_WIDTH      80
#define VGA_HEIGHT     25
#define VGA_BUF        ((volatile u16*)0xB8000)
#define VGA_CRT_ADDR   0x3D4
#define VGA_CRT_DATA   0x3D5

/* Light grey on black — the default text attribute. */
#define TERM_COLOR     0x07

static size_t term_row = 0;
static size_t term_col = 0;

static void term_cursor_update(void)
{
    u16 pos = (u16)(term_row * VGA_WIDTH + term_col);

    iowrite8(VGA_CRT_ADDR, 0x0E);
    iowrite8(VGA_CRT_DATA, (u8)(pos >> 8));
    iowrite8(VGA_CRT_ADDR, 0x0F);
    iowrite8(VGA_CRT_DATA, (u8)(pos & 0xFF));
}

/* Adopt the cursor position left by the kernel's kterm (the boot
 * launcher lines). */
static void term_sync_cursor_from_hw(void)
{
    int hi, lo;
    u16 pos;

    iowrite8(VGA_CRT_ADDR, 0x0E);
    hi = ioread8(VGA_CRT_DATA);
    iowrite8(VGA_CRT_ADDR, 0x0F);
    lo = ioread8(VGA_CRT_DATA);

    pos = (u16)((hi << 8) | lo);
    term_row = pos / VGA_WIDTH;
    term_col = pos % VGA_WIDTH;
}

static void term_putc(char c)
{
    if (c == '\n') {
        term_col = 0;
        if (++term_row >= VGA_HEIGHT)
            term_row = 0;
    } else if (c >= ' ') {
        size_t index = term_row * VGA_WIDTH + term_col;
        VGA_BUF[index] = (u16)c | (u16)TERM_COLOR << 8;
        if (++term_col >= VGA_WIDTH) {
            term_col = 0;
            if (++term_row >= VGA_HEIGHT)
                term_row = 0;
        }
    }

    term_cursor_update();
}

static void term_print(const u8* s, u32 len)
{
    for (u32 i = 0; i < len; i++)
        term_putc((char)s[i]);
}

void _start(void)
{
    user_portal_ctrl cfg = {0};

    /* Publish the fixed console portal (id == PORTAL_ID_CONSOLE) so
     * separately-linked user ELFs can print through it. */
    cfg.cmd       = U_PORTAL_CTRL_INIT;
    cfg.server_id = PORTAL_ID_CONSOLE;
    if (user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg)) != 0) {
        /* Portal already exists (should not happen at boot): idle. */
        for (;;)
            user_yield();
    }

    /* Continue on the screen where the kernel's kterm left off. */
    term_sync_cursor_from_hw();

    for (;;) {
        cfg.cmd = U_PORTAL_CTRL_WAIT;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

        cfg.cmd = U_PORTAL_CTRL_GET_REQ;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
        if (!cfg.req)
            continue;

        /* The payload is shm-mapped into this process's address space;
         * print it directly (no copy, no heap). */
        if (cfg.va && cfg.va_size)
            term_print((const u8*)cfg.va, cfg.va_size);

        cfg.cmd = U_PORTAL_CTRL_REPLY;
        cfg.ret = 0;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    }
}
