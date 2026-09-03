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
 * Two threads share the VGA text buffer:
 *   - main thread: console-portal print loop;
 *   - key thread:  subscribes to the keyboard server's MSG_KEY_EVENT
 *                  broadcasts and echoes printable keys to the screen.
 * Both take a small user spinlock around every character write / cursor
 * move, since they share the row/col state.
 */

#include "userlib.h"          /* user_syscall / portal + mailbox ABI     */
#include "kernel/uapi.h"      /* PORTAL_ID_CONSOLE                       */
#include "kernel/io.h"        /* ioread8/iowrite8 (provided by user_service.c) */
#include "user/uspinlock.h"   /* user-mode spinlock (same-process threads) */
#include "../server_msgs.h"   /* MSG_KEY_EVENT + key_event payload       */
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

/* Current text attribute.  Set by ESC[<code>m SGR sequences received over
 * the console portal (clients emit them to colour PASS/FAIL lines); 0x07
 * is the default light grey on black.  Written under g_vga_lock. */
static volatile u8 term_color = TERM_COLOR;

/* Serialises VGA writes between the portal thread and the key thread. */
static uspinlock g_vga_lock = USPINLOCK_INIT;

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

    uspin_lock(&g_vga_lock);

    iowrite8(VGA_CRT_ADDR, 0x0E);
    hi = ioread8(VGA_CRT_DATA);
    iowrite8(VGA_CRT_ADDR, 0x0F);
    lo = ioread8(VGA_CRT_DATA);

    pos = (u16)((hi << 8) | lo);
    term_row = pos / VGA_WIDTH;
    term_col = pos % VGA_WIDTH;

    uspin_unlock(&g_vga_lock);
}

/* Scroll the text buffer up by one row and blank the new bottom row.
 * Caller holds g_vga_lock. */
static void term_scroll_up(void)
{
    for (size_t r = 1; r < VGA_HEIGHT; r++)
        for (size_t c = 0; c < VGA_WIDTH; c++)
            VGA_BUF[(r - 1) * VGA_WIDTH + c] = VGA_BUF[r * VGA_WIDTH + c];
    for (size_t c = 0; c < VGA_WIDTH; c++)
        VGA_BUF[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = (u16)TERM_COLOR << 8;

    term_row = VGA_HEIGHT - 1;
    term_col = 0;
}

/* Blank the whole screen and home the cursor.  Caller holds g_vga_lock. */
static void term_clear_screen(void)
{
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUF[i] = (u16)TERM_COLOR << 8;

    term_row = 0;
    term_col = 0;
}

static void term_putc(char c)
{
    uspin_lock(&g_vga_lock);

    if (c == '\n') {
        term_col = 0;
        if (++term_row >= VGA_HEIGHT)
            term_scroll_up();
    } else if (c >= ' ') {
        size_t index = term_row * VGA_WIDTH + term_col;
        VGA_BUF[index] = (u16)c | (u16)term_color << 8;
        if (++term_col >= VGA_WIDTH) {
            term_col = 0;
            if (++term_row >= VGA_HEIGHT)
                term_scroll_up();
        }
    }

    term_cursor_update();

    uspin_unlock(&g_vga_lock);
}

/* Switch the current text attribute from an ANSI SGR colour code
 * (ESC[30..37m dark, ESC[90..97m bright, ESC[0m reset). */
static void term_set_color(int code)
{
    u8 attr;

    if (code == 0)
        attr = TERM_COLOR;
    else if (code >= 30 && code <= 37)
        attr = (u8)(code - 30);          /* dark fg on black */
    else if (code >= 90 && code <= 97)
        attr = (u8)(8 + (code - 90));    /* bright fg on black */
    else
        return;

    uspin_lock(&g_vga_lock);
    term_color = attr;
    uspin_unlock(&g_vga_lock);
}

static void term_print(const u8* s, u32 len)
{
    for (u32 i = 0; i < len; i++) {
        /* ANSI SGR colour sequence: ESC [ <digits> m.  Consumed here so
         * it never reaches the screen as text. */
        if (s[i] == 0x1b && i + 1 < len && s[i + 1] == '[') {
            u32 j = i + 2;
            int code = 0;

            while (j < len && s[j] >= '0' && s[j] <= '9') {
                code = code * 10 + (s[j] - '0');
                j++;
            }
            if (j < len && s[j] == 'm') {
                term_set_color(code);
                i = j;             /* skip the whole sequence */
                continue;
            }
        }
        term_putc((char)s[i]);
    }
}

/*
 * Key-listener thread: subscribes this thread's OWN mailbox to the
 * keyboard server's MSG_KEY_EVENT broadcasts and echoes printable keys
 * to the screen.  Runs concurrently with the portal loop; VGA writes go
 * through the shared uspinlock in term_putc().
 */
static void term_key_thread(void)
{
    if (user_mail_subscribe(MSG_KEY_EVENT) != 0) {
        /* No CAP_IPC or no mailbox — nothing to listen on. */
        for (;;)
            user_yield();
    }

    for (;;) {
        user_mail* m = user_mail_listen();   /* yield until a mail arrives */
        if (m && m->magic == MSG_KEY_EVENT) {
            key_event ev;

            for (size_t i = 0; i < sizeof(ev); i++)
                ((u8*)&ev)[i] = ((const u8*)m->data)[i];
            if (ev.pressed && ev.ascii)
                term_putc(ev.ascii);
        }
        if (m)
            user_mail_release(m);
    }
}

void _start(void)
{
    user_portal_ctrl cfg = {0};

    /* Spawn a sibling USER thread that echoes keyboard events while this
     * thread keeps serving the console portal (threads of one process
     * share the address space; the kernel gives it its own user stack). */
    user_proc_ctrl tc = {0};
    tc.cmd   = U_THREAD_CTRL_CREATE;
    tc.priv  = 1;                              /* TASK_PRIV_USER */
    tc.entry = (void*)(uptr)term_key_thread;
    user_syscall(SYSCALL_PROC_THREAD, &tc, sizeof(tc));
    tc.cmd = U_THREAD_CTRL_UNBLOCK;            /* tc.tid filled by CREATE */
    user_syscall(SYSCALL_PROC_THREAD, &tc, sizeof(tc));

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
         * print it directly (no copy, no heap).  A tiny control payload
         * (ESC[2J) clears the screen instead: clients send it before
         * redrawing (menus, paginated tests) so stale text never mixes
         * with new output. */
        if (cfg.va && cfg.va_size == 4 &&
            ((const u8*)cfg.va)[0] == 0x1b &&
            ((const u8*)cfg.va)[1] == '[' &&
            ((const u8*)cfg.va)[2] == '2' &&
            ((const u8*)cfg.va)[3] == 'J') {
            uspin_lock(&g_vga_lock);
            term_clear_screen();
            term_cursor_update();
            uspin_unlock(&g_vga_lock);
        } else if (cfg.va && cfg.va_size) {
            term_print((const u8*)cfg.va, cfg.va_size);
        }

        cfg.cmd = U_PORTAL_CTRL_REPLY;
        cfg.ret = 0;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    }
}
