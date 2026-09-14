/*
 * user/server/display/terminal_server.c — ring-3 console server
 * (standalone user ELF).
 *
 * Pure console-portal printer: publishes a DYNAMIC console portal and
 * registers it in the namespace under "console"; prints every payload it
 * receives straight to the VGA text buffer.  The two VGA MMIO windows
 * (text 0xB8000, mode-13 fb 0xA0000) are mapped into this process via the
 * SYSCALL_MMIO gate (granted CAP_MAP_MEM at load time); if a mapping is
 * unavailable its pointer stays NULL and the matching draw paths become
 * no-ops.  The interactive command line is gone, so there is no kb
 * dependency.
 *
 * VGA text cells are written with direct memory stores; the hardware
 * cursor is moved through the io syscall gate (ports 0x3D4/0x3D5, inside
 * the {0x3C0, 32} port grant given by the kernel at load time).
 *
 * Two threads share the VGA text buffer:
 *   - main thread: console-portal print loop;
 *   - key thread:  resolves the keyboard event magic from the namespace
 *                  ("kb"), subscribes to it and echoes printable keys.
 * Both take a small user spinlock around every character write / cursor
 * move, since they share the row/col state.
 */

#include "userlib.h"          /* user_syscall / portal + mailbox ABI     */
#include "kernel/uapi.h"      /* PORTAL_ID_NAMESPACE              */
#include "kernel/io.h"        /* ioread8/iowrite8 (provided by user_service.c) */
#include "user/uspinlock.h"   /* user-mode spinlock (same-process threads) */
#include "../server_msgs.h"   /* key_event payload                        */
#include <stddef.h>

#define VGA_WIDTH      80
#define VGA_HEIGHT     25

/* VGA text buffer — set by vga_map_windows() to the kernel-chosen high VA
 * returned by the MMIO syscall.  The low identity map (0xB8000) is now
 * supervisor-only, so if the mapping is missing this stays NULL and the
 * text paths below become no-ops instead of faulting.  Every text write
 * goes through this pointer. */
static volatile u16* vga_buf_ptr = 0;
#define VGA_BUF        vga_buf_ptr

#define VGA_CRT_ADDR   0x3D4
#define VGA_CRT_DATA   0x3D5

/* Extra VGA ports for mode switching (all inside the {0x3C0, 32} grant). */
#define VGA_MISC_WRITE 0x3C2
#define VGA_SEQ_ADDR   0x3C4
#define VGA_SEQ_DATA   0x3C5
#define VGA_GC_ADDR    0x3CE
#define VGA_GC_DATA    0x3CF
#define VGA_AC_ADDR    0x3C0
#define VGA_AC_DATA    0x3C0
#define VGA_AC_READ    0x3C1
#define VGA_DAC_MASK   0x3C6
#define VGA_DAC_WRITE  0x3C8
#define VGA_DAC_DATA   0x3C9
#define VGA_STAT_READ  0x3DA

/* Mode 0x13 linear frame buffer — set by vga_map_windows() like VGA_BUF;
 * NULL (unmapped) makes the frame-buffer paths no-ops. */
static volatile u8* gfx13_buf_ptr = 0;
#define GFX13_BUF      gfx13_buf_ptr

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

/* Current display mode: GFX_MODE_TEXT (0x03) or GFX_MODE_13 (0x13).
 * In graphics mode the console print loop and the key echo are parked —
 * the screen belongs to whoever called gfx_set_mode(0x13). */
static volatile u32 g_mode = GFX_MODE_TEXT;

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
    if (!vga_buf_ptr)
        return;               /* no VGA mapping — nothing to draw */

    for (size_t r = 1; r < VGA_HEIGHT; r++)
        for (size_t c = 0; c < VGA_WIDTH; c++)
            VGA_BUF[(r - 1) * VGA_WIDTH + c] = VGA_BUF[r * VGA_WIDTH + c];
    for (size_t c = 0; c < VGA_WIDTH; c++)
        VGA_BUF[(VGA_HEIGHT - 1) * VGA_WIDTH + c] =
            (u16)TERM_COLOR << 8;

    term_row = VGA_HEIGHT - 1;
    term_col = 0;
}

/* Blank the whole screen and home the cursor.  Caller holds g_vga_lock. */
static void term_clear_screen(void)
{
    if (!vga_buf_ptr)
        return;               /* no VGA mapping — nothing to draw */

    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUF[i] = (u16)TERM_COLOR << 8;

    term_row = 0;
    term_col = 0;
}

static void term_putc(char c)
{
    uspin_lock(&g_vga_lock);

    if (!vga_buf_ptr) {       /* no VGA mapping — nothing to draw */
        uspin_unlock(&g_vga_lock);
        return;
    }

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
        /* ANSI escape sequence: ESC [ <digits> <final>.  Consumed here so
         * it never reaches the screen as text.  Supported finals:
         *   m  — SGR colour (ESC[0m reset, ESC[30..37m / 90..97m colours);
         *   J  — erase display (ESC[2J / bare ESC[J = clear whole screen);
         *   H  — cursor home (ESC[H = row 0, col 0).
         * (A whole-page menu is sent as one request that starts with
         * ESC[2J so the clear + text render atomically in one request —
         * concurrent writers can never splice into it.) */
        if (s[i] == 0x1b && i + 1 < len && s[i + 1] == '[') {
            u32 j = i + 2;
            int code = 0;
            int has_digits = 0;

            while (j < len && s[j] >= '0' && s[j] <= '9') {
                code = code * 10 + (s[j] - '0');
                has_digits = 1;
                j++;
            }
            if (j < len && (s[j] == 'm' || s[j] == 'J' || s[j] == 'H')) {
                u8 final = (u8)s[j];

                if (final == 'm') {
                    term_set_color(has_digits ? code : 0);
                } else if (final == 'J') {
                    /* ESC[2J (or bare ESC[J): erase the whole screen. */
                    if (!has_digits || code == 2) {
                        uspin_lock(&g_vga_lock);
                        term_clear_screen();
                        term_color = TERM_COLOR;
                        term_cursor_update();
                        uspin_unlock(&g_vga_lock);
                    }
                } else if (final == 'H') {
                    /* ESC[H / ESC[1;1H: home the cursor. */
                    uspin_lock(&g_vga_lock);
                    term_row = 0;
                    term_col = 0;
                    term_cursor_update();
                    uspin_unlock(&g_vga_lock);
                }
                i = j;             /* skip the whole sequence */
                continue;
            }
            /* Unrecognised: fall through and print the ESC literally. */
        }
        term_putc((char)s[i]);
    }
}

/*
 * Key-listener thread: resolves the keyboard broadcast magic from the
 * namespace ("kb"), subscribes this thread's OWN mailbox to it and echoes
 * printable keys to the screen.  Runs concurrently with the portal loop;
 * VGA writes go through the shared uspinlock in term_putc().
 */
static void term_key_thread(void)
{
    /* Keyboard events are mailbox broadcasts; their magic is registered in
     * the namespace under "kb" (kb_server.elf).  Resolve it (retry while
     * the namespace/kb servers come up), then subscribe. */
    u32 pid = 0, kb_tid = 0, magic = 0;
    int tries = 0;

    while (tries++ < 10000) {
        if (ns_lookup(NS_NAME_KEYBOARD, &pid, &kb_tid, &magic) == 0 && magic)
            break;
        user_yield();
    }

    if (magic == 0 || user_mail_subscribe(magic) != 0) {
        /* No CAP_IPC / no mailbox / kb not registered — nothing to hear. */
        for (;;)
            user_yield();
    }

    for (;;) {
        user_mail* m = user_mail_listen();   /* yield until a mail arrives */
        if (m && m->magic == magic) {
            key_event ev;

            for (size_t i = 0; i < sizeof(ev); i++)
                ((u8*)&ev)[i] = ((const u8*)m->data)[i];
            if (ev.pressed && ev.ascii && g_mode == GFX_MODE_TEXT)
                term_putc(ev.ascii);
        }
        if (m)
            user_mail_release(m);
    }
}

/* ====================================================================
 * Graphics mode (VGA mode 0x13, 320x200x256) — the server doubles as
 * the graphics server.  Register dumps below are the standard mode-0x13
 * values; the text-mode (0x03) restore values mirror the kernel's
 * kterm_vga_set_text_mode().
 * ==================================================================== */

static void vga_write_regs(u16 addr_port, u16 data_port,
                           const u8* regs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        iowrite8(addr_port, (u8)i);
        iowrite8(data_port, regs[i]);
    }
}

/* ---- 8x16 text font save/restore ------------------------------------
 * VGA text glyphs are rendered from the 8x16 font that BIOS/GRUB load
 * once into VRAM plane 2 (QEMU: 256 glyphs x 32 bytes, windowed at
 * 0xA0000).  Mode 0x13 is chain4: its linear frame-buffer writes are
 * spread byte-by-byte across all four planes, wiping that font — and a
 * register-only switch back to 0x03 never reloads it, so after a
 * graphics session every glyph turns to garbage.
 *
 * The snapshot is taken LAZILY, just before the first mode-0x13 switch:
 * doing it at server start raced with the kernel's init_thread kterm
 * writes (while the window is remapped, writes meant for 0xB8000 land
 * in VRAM and corrupt the font — the reason the old boot-time snapshot
 * was removed).  By the time a client asks for graphics, boot is over
 * and only this server touches the VGA, serialised by g_vga_lock.
 * -------------------------------------------------------------------- */
#define FONT_BYTES  (256 * 32)          /* 256 glyphs x 32 bytes */

static u8 saved_font[FONT_BYTES];
static int font_saved = 0;

/* Snapshot plane 2 into saved_font[].  Caller holds g_vga_lock; must run
 * in text mode BEFORE any mode-0x13 session (no-op afterwards). */
static void font_save(void)
{
    if (font_saved || !GFX13_BUF)
        return;

    iowrite8(VGA_SEQ_ADDR, 0x02);
    iowrite8(VGA_SEQ_DATA, 0x0F);        /* map mask: all planes */
    iowrite8(VGA_GC_ADDR, 0x05);
    iowrite8(VGA_GC_DATA, 0x00);         /* disable odd/even */
    iowrite8(VGA_GC_ADDR, 0x06);
    iowrite8(VGA_GC_DATA, 0x04);         /* memory map 0xA0000 */
    iowrite8(VGA_GC_ADDR, 0x04);
    iowrite8(VGA_GC_DATA, 0x02);         /* read from plane 2 */

    for (u32 i = 0; i < FONT_BYTES; i++)
        saved_font[i] = GFX13_BUF[i];

    /* Put the VGA back to the text-mode state this server programs
     * (gfx_enter_text): planes 0/1 writes, odd/even on, B8000 map. */
    iowrite8(VGA_GC_ADDR, 0x04);
    iowrite8(VGA_GC_DATA, 0x00);
    iowrite8(VGA_GC_ADDR, 0x05);
    iowrite8(VGA_GC_DATA, 0x10);         /* odd/even text mode */
    iowrite8(VGA_GC_ADDR, 0x06);
    iowrite8(VGA_GC_DATA, 0x0E);         /* memory map 0xB8000 */
    iowrite8(VGA_SEQ_ADDR, 0x02);
    iowrite8(VGA_SEQ_DATA, 0x03);        /* text: planes 0/1 */

    font_saved = 1;
}

/* Reload saved_font[] into plane 2.  Caller holds g_vga_lock; runs from
 * gfx_enter_text() BEFORE the screen is blanked/repainted. */
static void font_load(void)
{
    if (!font_saved || !GFX13_BUF)
        return;

    /* Video off (AC index write with the enable bit clear) during the
     * upload. */
    ioread8(VGA_STAT_READ);              /* reset AC index flip-flop */
    iowrite8(VGA_AC_ADDR, 0x00);

    /* Point CPU writes at plane 2 of the 0xA0000 window, write mode 0.
     *
     * CRITICAL: sr[4] (sequencer memory mode) MUST have the SEQ_MODE bit
     * set (0x06) for the upload.  With the text-mode value 0x02, QEMU
     * (and real VGA text mapping) applies an odd/even filter to CPU
     * writes: odd window addresses would have their plane-2 mask
     * cleared and be dropped, leaving every other scanline of each
     * glyph corrupted. */
    iowrite8(VGA_SEQ_ADDR, 0x02);
    iowrite8(VGA_SEQ_DATA, 0x04);        /* map mask: plane 2 only */
    iowrite8(VGA_SEQ_ADDR, 0x04);
    iowrite8(VGA_SEQ_DATA, 0x06);        /* EXT_MEM | SEQ_MODE */
    iowrite8(VGA_GC_ADDR, 0x05);
    iowrite8(VGA_GC_DATA, 0x00);
    iowrite8(VGA_GC_ADDR, 0x06);
    iowrite8(VGA_GC_DATA, 0x04);
    iowrite8(VGA_GC_ADDR, 0x04);
    iowrite8(VGA_GC_DATA, 0x00);

    for (u32 i = 0; i < FONT_BYTES; i++)
        GFX13_BUF[i] = saved_font[i];

    /* Restore the text-mode GC/seq state gfx_enter_text programmed. */
    iowrite8(VGA_GC_ADDR, 0x04);
    iowrite8(VGA_GC_DATA, 0x00);
    iowrite8(VGA_GC_ADDR, 0x05);
    iowrite8(VGA_GC_DATA, 0x10);
    iowrite8(VGA_GC_ADDR, 0x06);
    iowrite8(VGA_GC_DATA, 0x0E);
    iowrite8(VGA_SEQ_ADDR, 0x04);
    iowrite8(VGA_SEQ_DATA, 0x02);
    iowrite8(VGA_SEQ_ADDR, 0x02);
    iowrite8(VGA_SEQ_DATA, 0x03);

    /* Video back on. */
    ioread8(VGA_STAT_READ);
    iowrite8(VGA_AC_ADDR, 0x20);
}

/* Switch to VGA graphics mode 0x13 (320x200, 256 colours, linear frame
 * buffer at 0xA0000).  Clears the frame buffer to colour 0. */
static void gfx_enter_mode13(void)
{
    /* chain4 is about to wipe the plane-2 text font — snapshot it first
     * (no-op after the first graphics session). */
    font_save();

    static const u8 seq_13[]   = { 0x03, 0x01, 0x0F, 0x00, 0x0E };
    static const u8 crtc_13[]  = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x0E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF
    };
    static const u8 gc_13[]    = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, 0xFF
    };
    static const u8 ac_13[]    = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x41
    };

    /* Misc output: 25 MHz dot clock, graphics. */
    iowrite8(VGA_MISC_WRITE, 0x63);

    /* Sequencer: reset, reprogram, restart. */
    iowrite8(VGA_SEQ_ADDR, 0x00);
    iowrite8(VGA_SEQ_DATA, 0x01);
    vga_write_regs(VGA_SEQ_ADDR, VGA_SEQ_DATA, seq_13, 5);
    iowrite8(VGA_SEQ_ADDR, 0x00);
    iowrite8(VGA_SEQ_DATA, 0x03);

    /* Unlock + program the CRTC. */
    iowrite8(VGA_CRT_ADDR, 0x11);
    iowrite8(VGA_CRT_DATA, (u8)(ioread8(VGA_CRT_DATA) & 0x7F));
    vga_write_regs(VGA_CRT_ADDR, VGA_CRT_DATA, crtc_13, 25);

    /* Graphics controller. */
    vga_write_regs(VGA_GC_ADDR, VGA_GC_DATA, gc_13, 9);

    /* Attribute controller (index write resets the flip-flop after the
     * status read; then re-enable video with 0x20). */
    ioread8(VGA_STAT_READ);
    for (size_t i = 0; i < 17; i++) {
        iowrite8(VGA_AC_ADDR, (u8)i);
        iowrite8(VGA_AC_DATA, ac_13[i]);
    }
    iowrite8(VGA_AC_ADDR, 0x20);

    /* Blank the frame buffer (no-op if the fb window is not mapped). */
    if (gfx13_buf_ptr)
        for (u32 i = 0; i < GFX_FB_SIZE; i++)
            GFX13_BUF[i] = 0;
}

/* Switch back to VGA text mode 0x03 (80x25) and clear the text buffer.
 * Register values mirror the kernel's kterm_vga_set_text_mode(). */
static void gfx_enter_text(void)
{
    static const u8 seq_03[]   = { 0x03, 0x00, 0x03, 0x00, 0x02 };
    static const u8 crtc_03[]  = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
        0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
        0xFF
    };
    static const u8 gc_03[]    = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x0F, 0xFF
    };
    static const u8 ac_03[]    = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x0C
    };
    static const u8 text_palette[16][3] = {
        {0x00,0x00,0x00},{0x00,0x00,0x2A},{0x00,0x2A,0x00},{0x00,0x2A,0x2A},
        {0x2A,0x00,0x00},{0x2A,0x00,0x2A},{0x2A,0x15,0x00},{0x2A,0x2A,0x2A},
        {0x15,0x15,0x15},{0x15,0x15,0x3F},{0x15,0x3F,0x15},{0x15,0x3F,0x3F},
        {0x3F,0x15,0x15},{0x3F,0x15,0x3F},{0x3F,0x3F,0x15},{0x3F,0x3F,0x3F},
    };

    ioread8(VGA_STAT_READ);
    iowrite8(VGA_MISC_WRITE, 0x67);

    iowrite8(VGA_SEQ_ADDR, 0x00);
    iowrite8(VGA_SEQ_DATA, 0x01);
    vga_write_regs(VGA_SEQ_ADDR, VGA_SEQ_DATA, seq_03, 5);
    iowrite8(VGA_SEQ_ADDR, 0x00);
    iowrite8(VGA_SEQ_DATA, 0x03);

    iowrite8(VGA_CRT_ADDR, 0x11);
    iowrite8(VGA_CRT_DATA, (u8)(ioread8(VGA_CRT_DATA) & 0x7F));
    vga_write_regs(VGA_CRT_ADDR, VGA_CRT_DATA, crtc_03, 25);

    vga_write_regs(VGA_GC_ADDR, VGA_GC_DATA, gc_03, 9);

    ioread8(VGA_STAT_READ);
    for (size_t i = 0; i < 17; i++) {
        iowrite8(VGA_AC_ADDR, (u8)i);
        iowrite8(VGA_AC_DATA, ac_03[i]);
    }
    iowrite8(VGA_AC_ADDR, 0x20);

    /* Restore the standard 16-colour text palette. */
    iowrite8(VGA_DAC_MASK, 0xFF);
    iowrite8(VGA_DAC_WRITE, 0);
    for (int i = 0; i < 16; i++) {
        iowrite8(VGA_DAC_DATA, text_palette[i][0]);
        iowrite8(VGA_DAC_DATA, text_palette[i][1]);
        iowrite8(VGA_DAC_DATA, text_palette[i][2]);
    }

    /* Hardware cursor shape (visible block) + blank the text buffer. */
    iowrite8(VGA_CRT_ADDR, 0x0A);
    iowrite8(VGA_CRT_DATA, 0x00);
    iowrite8(VGA_CRT_ADDR, 0x0B);
    iowrite8(VGA_CRT_DATA, 0x0F);

    /* Mode 0x13 (chain4) wiped the text font out of plane 2 — put it
     * back.  This MUST happen before term_clear_screen() below: the QEMU
     * text renderer only redraws cells whose content changed, so any
     * cell written while the font is still wiped would stay garbled on
     * screen (and never be repainted).  Reload first, then blank +
     * repaint everything with the restored glyphs. */
    font_load();

    term_clear_screen();
    term_cursor_update();
}

/* Handle one graphics control frame (payload already validated as a
 * gfx_ctrl).  Returns the reply int for the portal client. */
static int gfx_handle(const gfx_ctrl* g)
{
    if (g->cmd == GFX_CTRL_SET_MODE) {
        uspin_lock(&g_vga_lock);
        if (g->a == GFX_MODE_13) {
            gfx_enter_mode13();
            g_mode = GFX_MODE_13;
        } else if (g->a == GFX_MODE_TEXT) {
            gfx_enter_text();
            g_mode = GFX_MODE_TEXT;
        } else {
            uspin_unlock(&g_vga_lock);
            return -3;
        }
        uspin_unlock(&g_vga_lock);
        return 0;
    }

    if (g->cmd == GFX_CTRL_BLIT) {
        if (g_mode != GFX_MODE_13 || g->a != GFX_FB_SIZE)
            return -1;
        /* The client's frame buffer is shm-mapped right after this
         * control header in the same shared region (the client
         * shm_share()d header+fb together).  Copy it to the hardware
         * frame buffer — one memcpy per frame, no per-pixel IPC. */
        const u8* fb = (const u8*)g + sizeof(gfx_ctrl);
        uspin_lock(&g_vga_lock);
        if (gfx13_buf_ptr)
            for (u32 i = 0; i < GFX_FB_SIZE; i++)
                GFX13_BUF[i] = fb[i];
        uspin_unlock(&g_vga_lock);
        return 0;
    }

    return -3;
}

/* Fixed high VAs (below USER_SPACE_TOP 0xF0000000, above the shared user
 * heap [0xC0000000, 0xC1000000) and this ELF at 0xC1000000+) at which the
 * two VGA windows are mapped through the MMIO syscall.  Each process has
 * its own region tree, so the same VAs are fine in other servers. */
#define MMIO_TEXT_VA   ((void*)0xE0000000)
#define MMIO_GFX_VA    ((void*)0xE0010000)

/* Map the two VGA MMIO windows through the MMIO syscall gate.  On success
 * the pointers alias the hardware at the fixed high VAs above; the server
 * no longer depends on ring-3 access to the low identity map (which is
 * supervisor-only now).  If a mapping fails the pointer stays NULL and the
 * draw paths above become no-ops.  Runs once, before any thread can touch
 * the screen. */
static void vga_map_windows(void)
{
    if (user_mmio_map(0xB8000, 0x1000, MMIO_TEXT_VA) == 0)
        vga_buf_ptr = (volatile u16*)MMIO_TEXT_VA;
    if (user_mmio_map(0xA0000, 0x10000, MMIO_GFX_VA) == 0)
        gfx13_buf_ptr = (volatile u8*)MMIO_GFX_VA;
}

void _start(void)
{
    user_portal_ctrl cfg = {0};

    vga_map_windows();

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

    /* Publish a DYNAMIC console portal and register it in the namespace
     * under "console" so separately-linked user ELFs can resolve it. */
    cfg.cmd       = U_PORTAL_CTRL_INIT;
    cfg.server_id = 0;                         /* auto-assigned id */
    if (user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg)) != 0) {
        for (;;)
            user_yield();
    }

    {
        u32 my_id = (u32)(uptr)cfg.out;
        int tries = 0;
        while (tries++ < 10000 &&
               ns_register(NS_NAME_CONSOLE, my_id, 0, 0) != 0)
            user_yield();
    }

    /* Continue on the screen where the kernel's kterm left off. */
    term_sync_cursor_from_hw();

    /* NOTE: the text font is NOT snapshotted here at startup — the
     * remap window would race the kernel's init_thread kterm writes to
     * 0xB8000 (those land in VRAM while the window is redirected).
     * font_save() runs lazily instead, right before the first mode-0x13
     * switch (gfx_enter_mode13): by then boot is over and only this
     * server touches the VGA. */

    for (;;) {
        cfg.cmd = U_PORTAL_CTRL_WAIT;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

        cfg.cmd = U_PORTAL_CTRL_GET_REQ;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
        if (!cfg.req)
            continue;

        /* The payload is shm-mapped into this process's address space.
         * Three payload kinds, all starting with ESC:
         *   ESC 'G' ...   binary graphics control frame (gfx_ctrl) —
         *                 SET_MODE switches text/graphics, BLIT copies
         *                 the client's shm-mapped frame buffer (which
         *                 sits right after the header in the same shared
         *                 region) to 0xA0000.
         *   ESC [ 2 J     clear the text screen (menu redraws).
         *   anything else plain text (may embed ESC[<n>m SGR colours). */
        cfg.cmd = U_PORTAL_CTRL_REPLY;
        cfg.ret = 0;

        if (cfg.va && cfg.va_size >= sizeof(gfx_ctrl) &&
            ((const u8*)cfg.va)[0] == 0x1b &&
            ((const u8*)cfg.va)[1] == 'G') {
            cfg.ret = gfx_handle((const gfx_ctrl*)cfg.va);
        } else if (cfg.va && cfg.va_size == 4 &&
            ((const u8*)cfg.va)[0] == 0x1b &&
            ((const u8*)cfg.va)[1] == '[' &&
            ((const u8*)cfg.va)[2] == '2' &&
            ((const u8*)cfg.va)[3] == 'J') {
            uspin_lock(&g_vga_lock);
            term_clear_screen();
            term_cursor_update();
            uspin_unlock(&g_vga_lock);
        } else if (cfg.va && cfg.va_size && g_mode == GFX_MODE_TEXT) {
            term_print((const u8*)cfg.va, cfg.va_size);
        }

        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    }
}
