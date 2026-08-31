/*
 * kernel/kterm.c — ring-0 direct-to-VGA text-mode terminal.
 *
 * The kernel's own screen output path, fully decoupled from the user-mode
 * terminal server: no terminal_device struct, no platform bus, no
 * uspinlock.  kterm talks straight to the VGA hardware — text buffer at
 * 0xB8000 (memory-mapped) and CRT controller at 0x3D4/0x3D5 — through
 * the io layer (kernel/io.c).  At ring-0 iowrite8()/ioread8() are plain
 * outb/inb, so this is direct port I/O.
 *
 * kterm_* is ring-0 only (it runs cli/sti, which is not available at
 * CPL3 with IOPL=0).  Ring-3 screen output is handled by the terminal
 * server in terminal_server.c.
 */

#include "kernel/kterm.h"
#include "kernel/io.h"
#include <stddef.h>

#define KTERM_WIDTH      80
#define KTERM_HEIGHT     25
#define KTERM_VGA_BUF    0xB8000

#define VGA_CRT_ADDR     0x3D4
#define VGA_CRT_DATA     0x3D5

/* Default raw attribute byte: light grey on black (matches the driver). */
#define KTERM_DEFAULT_COLOR  0x07

static int kterm_inited = 0;

static u16* const kterm_buf = (u16*)KTERM_VGA_BUF;
static size_t kterm_row = 0;
static size_t kterm_col = 0;
static u8 kterm_color = KTERM_DEFAULT_COLOR;

/* Move the hardware text cursor to (row, col). */
static void kterm_cursor_update(void)
{
    u16 pos = (u16)(kterm_row * KTERM_WIDTH + kterm_col);

    iowrite8(VGA_CRT_ADDR, 0x0E);
    iowrite8(VGA_CRT_DATA, (u8)(pos >> 8));
    iowrite8(VGA_CRT_ADDR, 0x0F);
    iowrite8(VGA_CRT_DATA, (u8)(pos & 0xFF));
}

/* Single-character write.  Caller must hold IF=0 (kterm_putc/write do). */
static void kterm_putc_locked(char c)
{
    if (c == '\n') {
        kterm_col = 0;
        if (++kterm_row >= KTERM_HEIGHT)
            kterm_row = 0;
    } else if (c == '\b') {
        int moved = 0;
        if (kterm_col > 0) {
            kterm_col--;
            moved = 1;
        } else if (kterm_row > 0) {
            kterm_row--;
            kterm_col = KTERM_WIDTH - 1;
            moved = 1;
        }
        /* Only erase the cell if the cursor actually moved back;
         * at (0,0) a backspace is a no-op. */
        if (moved) {
            size_t index = kterm_row * KTERM_WIDTH + kterm_col;
            kterm_buf[index] = (u16)' ' | (u16)kterm_color << 8;
        }
    } else if (c >= ' ') {
        size_t index = kterm_row * KTERM_WIDTH + kterm_col;
        kterm_buf[index] = (u16)c | (u16)kterm_color << 8;
        if (++kterm_col >= KTERM_WIDTH) {
            kterm_col = 0;
            if (++kterm_row >= KTERM_HEIGHT)
                kterm_row = 0;
        }
    }

    kterm_cursor_update();
}

/* Bring up the VGA text-mode cursor and reset the terminal state.
 * Idempotent; also called lazily on the first write so very early boot
 * output (before kterm_init() runs) still reaches the screen. */
void kterm_init(void)
{
    /* Cursor Start Register: scanline 0, cursor visible */
    iowrite8(VGA_CRT_ADDR, 0x0A);
    iowrite8(VGA_CRT_DATA, 0x00);

    /* Cursor End Register: scanline 15, full block */
    iowrite8(VGA_CRT_ADDR, 0x0B);
    iowrite8(VGA_CRT_DATA, 0x0F);

    kterm_row = 0;
    kterm_col = 0;
    kterm_color = KTERM_DEFAULT_COLOR;
    kterm_cursor_update();
    kterm_inited = 1;
}

/* Single-character write.  Guards the whole cell update with IF=0 so an
 * ISR-side kterm_putc (e.g. from a fault or IRQ handler) cannot
 * interleave. */
void kterm_putc(char c)
{
    u32 eflags;

    if (!kterm_inited)
        kterm_init();

    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    __asm__ __volatile__("cli" ::: "memory");
    kterm_putc_locked(c);
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}

/* Write a NUL-terminated string with IF=0 for the whole message. */
void kterm_write(const char* s)
{
    u32 eflags;

    if (!s)
        return;

    if (!kterm_inited)
        kterm_init();

    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    __asm__ __volatile__("cli" ::: "memory");
    while (*s)
        kterm_putc_locked(*s++);
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}

/* Write a string using a temporary raw attribute byte (e.g. 0x0E for
 * light yellow), restoring the previous color afterwards. */
void kterm_write_color(const char* s, u8 color)
{
    u8 old = kterm_color;

    kterm_color = color;
    kterm_write(s);
    kterm_color = old;
}

/* Clear the VGA text buffer and reset the cursor to the top-left. */
void kterm_clear(void)
{
    u32 eflags;

    if (!kterm_inited)
        kterm_init();

    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    __asm__ __volatile__("cli" ::: "memory");

    kterm_row = 0;
    kterm_col = 0;
    kterm_color = KTERM_DEFAULT_COLOR;

    for (size_t y = 0; y < KTERM_HEIGHT; y++) {
        for (size_t x = 0; x < KTERM_WIDTH; x++) {
            const size_t index = y * KTERM_WIDTH + x;
            kterm_buf[index] = (u16)' ' | (u16)kterm_color << 8;
        }
    }

    kterm_cursor_update();

    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}

/************************************************************************/
/*               VGA Text-Mode (Mode 0x03) Switching                    */
/************************************************************************/

/*
 * VGA register values for text mode 0x03 (80x25, 16 colors).  These
 * restore the default VGA text-mode state after graphics mode.
 */

/* Sequencer registers for mode 0x03 */
static const u8 kterm_seq_0x03[] = {
    0x03, 0x00, 0x03, 0x00, 0x02
};

/* CRT Controller registers for mode 0x03 */
static const u8 kterm_crtc_0x03[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};

/* Graphics Controller registers for mode 0x03 */
static const u8 kterm_gc_0x03[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x0F, 0xFF
};

/* Attribute Controller registers for mode 0x03 (16 palette + mode ctrl) */
static const u8 kterm_ac_0x03[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0C
};

/* Additional VGA ports needed for mode switching. */
#define VGA_MISC_WRITE  0x3C2
#define VGA_SEQ_ADDR    0x3C4
#define VGA_SEQ_DATA    0x3C5
#define VGA_GC_ADDR     0x3CE
#define VGA_GC_DATA     0x3CF
#define VGA_AC_ADDR     0x3C0
#define VGA_AC_DATA     0x3C0
#define VGA_DAC_MASK    0x3C6
#define VGA_DAC_WRITE   0x3C8
#define VGA_DAC_DATA    0x3C9

static void kterm_vga_write_regs(u16 addr_port, u16 data_port,
                                 const u8* regs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        iowrite8(addr_port, (u8)i);
        iowrite8(data_port, regs[i]);
    }
}

static void kterm_vga_set_text_mode(void)
{
    /* 1. Reset attribute flip-flop */
    ioread8(0x3DA);

    /* 2. Set Misc Output Register for 28 MHz dot clock, text mode */
    iowrite8(VGA_MISC_WRITE, 0x67);

    /* 3. Disable sequencer during reprogramming */
    iowrite8(VGA_SEQ_ADDR, 0x00);
    iowrite8(VGA_SEQ_DATA, 0x01);

    /* 4. Program Sequencer registers */
    kterm_vga_write_regs(VGA_SEQ_ADDR, VGA_SEQ_DATA, kterm_seq_0x03, 5);

    /* 5. Re-enable sequencer */
    iowrite8(VGA_SEQ_ADDR, 0x00);
    iowrite8(VGA_SEQ_DATA, 0x03);

    /* 6. Unlock CRTC */
    iowrite8(VGA_CRT_ADDR, 0x11);
    iowrite8(VGA_CRT_DATA, (u8)(ioread8(VGA_CRT_DATA) & 0x7F));

    /* 7. Program CRTC registers */
    kterm_vga_write_regs(VGA_CRT_ADDR, VGA_CRT_DATA, kterm_crtc_0x03, 25);

    /* 8. Program Graphics Controller registers */
    kterm_vga_write_regs(VGA_GC_ADDR, VGA_GC_DATA, kterm_gc_0x03, 9);

    /* 9. Program Attribute Controller registers */
    ioread8(0x3DA);
    for (size_t i = 0; i < 17; i++) {
        iowrite8(VGA_AC_ADDR, (u8)i);
        iowrite8(VGA_AC_DATA, kterm_ac_0x03[i]);
    }
    /* Re-enable video output */
    iowrite8(VGA_AC_ADDR, 0x20);

    /* 10. Set DAC palette for text mode (standard 16-color palette) */
    {
        static const u8 kterm_text_palette[16][3] = {
            {0x00,0x00,0x00},{0x00,0x00,0x2A},{0x00,0x2A,0x00},{0x00,0x2A,0x2A},
            {0x2A,0x00,0x00},{0x2A,0x00,0x2A},{0x2A,0x15,0x00},{0x2A,0x2A,0x2A},
            {0x15,0x15,0x15},{0x15,0x15,0x3F},{0x15,0x3F,0x15},{0x15,0x3F,0x3F},
            {0x3F,0x15,0x15},{0x3F,0x15,0x3F},{0x3F,0x3F,0x15},{0x3F,0x3F,0x3F},
        };
        iowrite8(VGA_DAC_MASK, 0xFF);
        iowrite8(VGA_DAC_WRITE, 0);
        for (int i = 0; i < 16; i++) {
            iowrite8(VGA_DAC_DATA, kterm_text_palette[i][0]);
            iowrite8(VGA_DAC_DATA, kterm_text_palette[i][1]);
            iowrite8(VGA_DAC_DATA, kterm_text_palette[i][2]);
        }
    }
}

/* Switch the display back to VGA text mode 0x03 and reinitialize the
 * terminal state (clear screen, reset cursor). */
void kterm_switch_to_text_mode(void)
{
    u32 eflags;

    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    __asm__ __volatile__("cli" ::: "memory");

    /* Reprogram VGA registers for text mode 0x03 */
    kterm_vga_set_text_mode();

    /* Reinitialize hardware cursor */
    iowrite8(VGA_CRT_ADDR, 0x0A);
    iowrite8(VGA_CRT_DATA, 0x00);
    iowrite8(VGA_CRT_ADDR, 0x0B);
    iowrite8(VGA_CRT_DATA, 0x0F);

    /* Clear the VGA text buffer and reset terminal state */
    kterm_row = 0;
    kterm_col = 0;
    kterm_color = KTERM_DEFAULT_COLOR;

    for (size_t y = 0; y < KTERM_HEIGHT; y++) {
        for (size_t x = 0; x < KTERM_WIDTH; x++) {
            const size_t index = y * KTERM_WIDTH + x;
            kterm_buf[index] = (u16)' ' | (u16)kterm_color << 8;
        }
    }

    kterm_cursor_update();
    kterm_inited = 1;

    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}
