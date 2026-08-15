/*******************************************************************************
 *                                                                             *
 *    Terminal Server — user-mode VGA text-mode output, hardware cursor,       *
 *                      keyboard echo & command dispatch                       *
 *                                                                             *
 *    Runs as a DRIVER_CLASS_USER server (ring-3 process, like kb_server.c).   *
 *    All terminal_* APIs are plain functions callable directly from RING3 —   *
 *    no syscall gate.  The VGA buffer (0xB8000) is identity-mapped with       *
 *    PTE_USER_PAGE, and RING3 threads run with IOPL=3 (see task.c), so the    *
 *    raw in/out on the CRT controller (0x3D4/0x3D5) and the VGA registers     *
 *    work directly from ring 3.                                               *
 *                                                                             *
 *    Keyboard input comes from the user-mode kb_server via                *
 *    kb_register_callback.                                               *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_server.h"
#include "sync/spinlock.h"
#include "drivers/kb_server.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "kernel/process.h"
#include "regs.h"

/************************************************************************/
/*                        Internal Definitions                          */
/************************************************************************/
#define VGA_WIDTH      80
#define VGA_HEIGHT     25
#define VGA_BUF_ADDR   0xB8000

struct terminal_device {
    struct platform_bus_ops* bus_ops;   /* set at start(): RING3-safe wrappers */
    spinlock* lock;
    u16* vga_buffer;
    size_t curr_row;
    size_t curr_col;
    u8 curr_color;
};

static struct terminal_device term_device = {
    .lock = NULL,
    .vga_buffer = (u16*)VGA_BUF_ADDR,
    .curr_row = 0,
    .curr_col = 0,
    .curr_color = 0,
};

/* ---- RING3-safe VGA I/O -----------------------------------------------
 * The platform bus ops are never attached to a DRIVER_CLASS_USER device
 * (probe() is not called for user drivers), so the terminal server uses
 * its own wrappers around the raw in/out instructions.  RING3 may execute
 * them directly because user threads run with IOPL=3 (see task.c). */
static int term_out8(u16 port, u8 data)
{
    arch_outb(port, data);
    return 0;
}

static int term_in8(u16 port)
{
    return (int)arch_inb(port);
}

static struct platform_bus_ops term_bus_ops = {
    .in_port8  = term_in8,
    .out_port8 = term_out8,
};

/* ---- IRQ-safe terminal lock -------------------------------------------
 * Terminal state is touched from RING3 apps, from kernel threads and from
 * the keyboard ISR callback (IF=0).  On this single-CPU kernel, cli around
 * the spinlock prevents a RING3/kernel holder from being preempted by the
 * keyboard ISR (which would otherwise spin forever on the same lock).
 * pushf/popf restores the previous IF; RING3 may run cli/sti thanks to
 * IOPL=3. */
static u32 term_lock(void)
{
    u32 eflags;
    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    arch_cli();
    spinlock_lock(term_device.lock);
    return eflags;
}

static void term_unlock(u32 eflags)
{
    spinlock_unlock(term_device.lock);
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}

/************************************************************************/
/*                      Hardware Cursor Control                         */
/************************************************************************/
static size_t input_len = 0;

#define VGA_CRT_ADDR    0x3D4
#define VGA_CRT_DATA    0x3D5

static void cursor_init(void)
{
    struct platform_bus_ops* ops = term_device.bus_ops;
    if (!ops)
        return;

    /* Cursor Start Register: scanline 0, cursor visible */
    ops->out_port8(VGA_CRT_ADDR, 0x0A);
    ops->out_port8(VGA_CRT_DATA, 0x00);

    /* Cursor End Register: scanline 15, full block */
    ops->out_port8(VGA_CRT_ADDR, 0x0B);
    ops->out_port8(VGA_CRT_DATA, 0x0F);
}

/************************************************************************/
/*               VGA Text-Mode (Mode 0x03) Switching                    */
/************************************************************************/

/*
 * VGA register values for text mode 0x03 (80×25, 16 colors).
 * These restore the default VGA text-mode state after graphics mode.
 */

/* Sequencer registers for mode 0x03 */
static const u8 seq_0x03[] = {
    0x03, 0x00, 0x03, 0x00, 0x02
};

/* CRT Controller registers for mode 0x03 */
static const u8 crtc_0x03[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};

/* Graphics Controller registers for mode 0x03 */
static const u8 gc_0x03[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x0F, 0xFF
};

/* Attribute Controller registers for mode 0x03 (16 palette + mode ctrl) */
static const u8 ac_0x03[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x0C
};

/*
 * Additional VGA ports needed for mode switching (same as graphics driver).
 */
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

static void vga_write_regs(struct platform_bus_ops* ops,
                           u16 addr_port, u16 data_port,
                           const u8* regs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ops->out_port8(addr_port, (u8)i);
        ops->out_port8(data_port, regs[i]);
    }
}

static void vga_set_text_mode(struct platform_bus_ops* ops)
{
    if (!ops) return;

    /* 1. Reset attribute flip-flop */
    ops->in_port8(0x3DA);

    /* 2. Set Misc Output Register for 28 MHz dot clock, text mode */
    ops->out_port8(VGA_MISC_WRITE, 0x67);

    /* 3. Disable sequencer during reprogramming */
    ops->out_port8(VGA_SEQ_ADDR, 0x00);
    ops->out_port8(VGA_SEQ_DATA, 0x01);

    /* 4. Program Sequencer registers */
    vga_write_regs(ops, VGA_SEQ_ADDR, VGA_SEQ_DATA, seq_0x03, 5);

    /* 5. Re-enable sequencer */
    ops->out_port8(VGA_SEQ_ADDR, 0x00);
    ops->out_port8(VGA_SEQ_DATA, 0x03);

    /* 6. Unlock CRTC */
    ops->out_port8(VGA_CRT_ADDR, 0x11);
    ops->out_port8(VGA_CRT_DATA,
                   (u8)(ops->in_port8(VGA_CRT_DATA) & 0x7F));

    /* 7. Program CRTC registers */
    vga_write_regs(ops, VGA_CRT_ADDR, VGA_CRT_DATA, crtc_0x03, 25);

    /* 8. Program Graphics Controller registers */
    vga_write_regs(ops, VGA_GC_ADDR, VGA_GC_DATA, gc_0x03, 9);

    /* 9. Program Attribute Controller registers */
    ops->in_port8(0x3DA);
    for (size_t i = 0; i < 17; i++) {
        ops->out_port8(VGA_AC_ADDR, (u8)i);
        ops->out_port8(VGA_AC_DATA, ac_0x03[i]);
    }
    /* Re-enable video output */
    ops->out_port8(VGA_AC_ADDR, 0x20);

    /* 10. Set DAC palette for text mode (standard 16-color palette) */
    {
        static const u8 text_palette[16][3] = {
            {0x00,0x00,0x00},{0x00,0x00,0x2A},{0x00,0x2A,0x00},{0x00,0x2A,0x2A},
            {0x2A,0x00,0x00},{0x2A,0x00,0x2A},{0x2A,0x15,0x00},{0x2A,0x2A,0x2A},
            {0x15,0x15,0x15},{0x15,0x15,0x3F},{0x15,0x3F,0x15},{0x15,0x3F,0x3F},
            {0x3F,0x15,0x15},{0x3F,0x15,0x3F},{0x3F,0x3F,0x15},{0x3F,0x3F,0x3F},
        };
        ops->out_port8(VGA_DAC_MASK, 0xFF);
        ops->out_port8(VGA_DAC_WRITE, 0);
        for (int i = 0; i < 16; i++) {
            ops->out_port8(VGA_DAC_DATA, text_palette[i][0]);
            ops->out_port8(VGA_DAC_DATA, text_palette[i][1]);
            ops->out_port8(VGA_DAC_DATA, text_palette[i][2]);
        }
    }
}

static void cursor_update(size_t row, size_t col)
{
    struct platform_bus_ops* ops = term_device.bus_ops;
    if (!ops)
        return;

    u16 pos = (u16)(row * VGA_WIDTH + col);

    ops->out_port8(VGA_CRT_ADDR, 0x0E);
    ops->out_port8(VGA_CRT_DATA, (u8)(pos >> 8));
    ops->out_port8(VGA_CRT_ADDR, 0x0F);
    ops->out_port8(VGA_CRT_DATA, (u8)(pos & 0xFF));
}

/*
 * Switch the display back to text mode (VGA mode 0x03) and reinitialize
 * the terminal state (clear screen, reset cursor, restore input prompt).
 */
void terminal_switch_to_text_mode(void)
{
    struct platform_bus_ops* ops = term_device.bus_ops;
    if (!ops)
        return;

    u32 eflags = term_lock();

    /* Reprogram VGA registers for text mode 0x03 */
    vga_set_text_mode(ops);

    /* Reinitialize hardware cursor */
    cursor_init();

    /* Clear the VGA text buffer and reset terminal state */
    term_device.curr_row = 0;
    term_device.curr_col = 0;
    term_device.curr_color = to_vga_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            term_device.vga_buffer[index] = to_vga_char(' ', term_device.curr_color);
        }
    }

    cursor_update(term_device.curr_row, term_device.curr_col);
    term_unlock(eflags);

    /* Reset input state and show prompt */
    input_len = 0;
    terminal_write("#: ");
}

/************************************************************************/
/*                Command Registry & Input Line Buffer                  */
/************************************************************************/
#define CMD_BUF_SIZE    256
#define CMD_NAME_MAX    32

struct terminal_cmd_entry {
    list_node node;
    char name[CMD_NAME_MAX];
    terminal_cmd_fn callback;
};

static char input_buf[CMD_BUF_SIZE];
static list_node cmd_registry;
static spinlock* cmd_lock = NULL;
static int cmd_ready = 0;

static void cmd_init(void)
{
    if (cmd_ready)
        return;

    list_init(&cmd_registry);
    cmd_lock = spinlock_alloc();
    if (cmd_lock)
        cmd_ready = 1;
}

/* IRQ-safe guard for the command registry (same rationale as term_lock) */
static u32 cmd_lock_irq(void)
{
    u32 eflags;
    __asm__ __volatile__("pushfl; popl %0" : "=r"(eflags) : : "memory");
    arch_cli();
    spinlock_lock(cmd_lock);
    return eflags;
}

static void cmd_unlock_irq(u32 eflags)
{
    spinlock_unlock(cmd_lock);
    __asm__ __volatile__("pushl %0; popfl" : : "r"(eflags) : "memory");
}

static int str_cmp(const char* a, const char* b, size_t max)
{
    for (size_t i = 0; i < max; i++) {
        if (a[i] != b[i])
            return 0;
        if (a[i] == '\0')
            return 1;
    }
    return 1;
}

static size_t str_len(const char* s, size_t max)
{
    size_t len = 0;
    while (len < max && s[len])
        len++;
    return len;
}

/************************************************************************/
/*                      Terminal Public API                             */
/************************************************************************/
void terminal_flush(const char* unused)
{
    (void)unused;

    struct terminal_device* dev = &term_device;

    u32 eflags = term_lock();

    dev->curr_row = 0;
    dev->curr_col = 0;
    dev->curr_color = to_vga_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            dev->vga_buffer[index] = to_vga_char(' ', dev->curr_color);
        }
    }

    cursor_update(dev->curr_row, dev->curr_col);
    term_unlock(eflags);
}

void terminal_write_at(char chr, u8 color, size_t x, size_t y)
{
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;

    struct terminal_device* dev = &term_device;

    u32 eflags = term_lock();

    const size_t index = y * VGA_WIDTH + x;
    dev->vga_buffer[index] = to_vga_char(chr, color);

    dev->curr_col = x;
    dev->curr_row = y;
    dev->curr_color = color;

    if (++dev->curr_col >= VGA_WIDTH) {
        dev->curr_col = 0;
        if (++dev->curr_row >= VGA_HEIGHT) {
            dev->curr_row = 0;
        }
    }

    cursor_update(dev->curr_row, dev->curr_col);
    term_unlock(eflags);
}

void terminal_write_at_str(const char* str, u8 color, size_t x, size_t y)
{
    if (!str)
        return;

    struct terminal_device* dev = &term_device;

    if (color)
        dev->curr_color = color;

    while (*str != '\0') {
        terminal_write_at(*str, dev->curr_color, x, y);
        str++;
        if (++x >= VGA_WIDTH) {
            x = 0;
            if (++y >= VGA_HEIGHT) {
                y = 0;
            }
        }
    }
}

void terminal_write(const char* str)
{
    if (!str)
        return;

    while (*str) {
        terminal_putchar(*str);
        str++;
    }
}

void terminal_write_color(const char* str, u8 color)
{
    if (!str)
        return;

    struct terminal_device* dev = &term_device;
    u8 old_color = dev->curr_color;
    dev->curr_color = color;
    terminal_write(str);
    dev->curr_color = old_color;
}

void terminal_putchar(char c)
{
    struct terminal_device* dev = &term_device;

    u32 eflags = term_lock();

    if (c == '\n') {
        dev->curr_col = 0;
        if (++dev->curr_row >= VGA_HEIGHT)
            dev->curr_row = 0;
    } else if (c == '\b') {
        if (dev->curr_col > 0) {
            dev->curr_col--;
        } else if (dev->curr_row > 0) {
            dev->curr_row--;
            dev->curr_col = VGA_WIDTH - 1;
        }
        size_t index = dev->curr_row * VGA_WIDTH + dev->curr_col;
        dev->vga_buffer[index] = to_vga_char(' ', dev->curr_color);
    } else if (c >= ' ') {
        size_t index = dev->curr_row * VGA_WIDTH + dev->curr_col;
        dev->vga_buffer[index] = to_vga_char(c, dev->curr_color);
        if (++dev->curr_col >= VGA_WIDTH) {
            dev->curr_col = 0;
            if (++dev->curr_row >= VGA_HEIGHT)
                dev->curr_row = 0;
        }
    }

    cursor_update(dev->curr_row, dev->curr_col);
    term_unlock(eflags);
}

size_t terminal_get_row(void)
{
    return term_device.curr_row;
}

int terminal_register_cmd(const char* name, terminal_cmd_fn callback)
{
    if (!name || !callback)
        return E_INVAL;

    cmd_init();
    if (!cmd_ready)
        return E_NOTREADY;

    struct terminal_cmd_entry* entry =
        (struct terminal_cmd_entry*)kmalloc(sizeof(*entry));
    if (!entry)
        return E_NOMEM;

    size_t nlen = str_len(name, CMD_NAME_MAX - 1);
    for (size_t i = 0; i < nlen; i++)
        entry->name[i] = name[i];
    entry->name[nlen] = '\0';
    entry->callback = callback;
    list_init(&entry->node);

    u32 eflags = cmd_lock_irq();
    list_add(&entry->node, &cmd_registry);
    cmd_unlock_irq(eflags);

    return 0;
}

void terminal_unregister_cmd(const char* name)
{
    if (!name || !cmd_ready)
        return;

    u32 eflags = cmd_lock_irq();
    list_for_each(pos, &cmd_registry) {
        struct terminal_cmd_entry* entry =
            list_entry(pos, struct terminal_cmd_entry, node);
        if (str_cmp(entry->name, name, CMD_NAME_MAX)) {
            list_del(&entry->node);
            cmd_unlock_irq(eflags);
            kfree(entry);
            return;
        }
    }
    cmd_unlock_irq(eflags);
}

/************************************************************************/
/*                   Keyboard Echo & Command Dispatch                   */
/************************************************************************/
static void terminal_kb_handler(const char* data, size_t size)
{
    (void)size;
    char c = data[0];

    /* ENTER (0x03 from keymap) or ASCII CR/LF */
    if (c == 0x03 || c == '\r' || c == '\n') {
        terminal_putchar('\n');

        if (input_len > 0 && input_len < CMD_BUF_SIZE)
            input_buf[input_len] = '\0';
        else if (input_len >= CMD_BUF_SIZE)
            input_buf[CMD_BUF_SIZE - 1] = '\0';
        else
            input_buf[0] = '\0';

        int matched = 0;
        if (cmd_ready && input_len > 0) {
            u32 eflags = cmd_lock_irq();
            list_for_each(pos, &cmd_registry) {
                struct terminal_cmd_entry* entry =
                    list_entry(pos, struct terminal_cmd_entry, node);

                size_t nlen = str_len(entry->name, CMD_NAME_MAX);
                if (nlen > input_len)
                    continue;

                int match = 1;
                for (size_t i = 0; i < nlen; i++) {
                    if (input_buf[i] != entry->name[i]) {
                        match = 0;
                        break;
                    }
                }

                if (match &&
                    (input_buf[nlen] == '\0' || input_buf[nlen] == ' ')) {
                    const char* args = input_buf + nlen;
                    while (*args == ' ')
                        args++;
                    entry->callback(args);
                    matched = 1;
                    break;
                }
            }
            cmd_unlock_irq(eflags);
        }

        if (!matched && input_len > 0) {
            terminal_write("Unknown command: ");
            terminal_write(input_buf);
            terminal_putchar('\n');
        }

        input_len = 0;
        terminal_write("#: ");
        return;
    }

    /* BACKSPACE (0x04 from keymap) or ASCII BS */
    if (c == 0x04 || c == '\b') {
        if (input_len > 0) {
            input_len--;
            terminal_putchar('\b');
        }
        return;
    }

    /* printable characters */
    if (c >= ' ' && c <= '~') {
        if (input_len < CMD_BUF_SIZE - 1) {
            input_buf[input_len++] = c;
            terminal_putchar(c);
        }
    }
}

/************************************************************************/
/*                      Init / Exit                                     */
/************************************************************************/

static void terminal_textmode_cmd(const char* args)
{
    (void)args;
    terminal_switch_to_text_mode();
}

static void terminal_server_loop(void)
{
    /* Keyboard echo and command dispatch are driven by the kb_server
     * callback (kb_server → kb_register_callback → terminal_kb_handler).
     * This server thread simply idles, keeping the server process alive. */
    for (;;)
        thread_yield();
}

int terminal_start(struct device* dev)
{
    (void)dev;

    /* DRIVER_CLASS_USER drivers never get probe() called, so allocate the
     * terminal lock here (inside the server process) instead.  Before this
     * runs, term_lock() still provides mutual exclusion via cli alone. */
    if (!term_device.lock) {
        term_device.lock = spinlock_alloc();
        if (!term_device.lock)
            return E_LIMIT;
    }

    /* Attach RING3-safe VGA I/O (raw in/out — user threads have IOPL=3) */
    term_device.bus_ops = &term_bus_ops;

    cursor_init();

    /* register keyboard echo + command dispatch */
    kb_register_callback(terminal_kb_handler);

    terminal_register_cmd("clear", terminal_flush);
    terminal_register_cmd("textmode", terminal_textmode_cmd);

    terminal_write("#: ");

    terminal_server_loop();   /* never returns */

    return 0;
}

int terminal_stop(struct device* dev)
{
    (void)dev;

    kb_unregister_callback(terminal_kb_handler);
    terminal_unregister_cmd("clear");
    terminal_unregister_cmd("textmode");
    term_device.bus_ops = NULL;

    return 0;
}

static struct driver terminal_server = {
    .class = DRIVER_CLASS_USER,
    .type = "terminal",
    .start = terminal_start,
    .stop = terminal_stop,
};

void terminal_init(void)
{
    platform_driver_register(&terminal_server);
}

void terminal_exit(void)
{
    platform_driver_unregister(&terminal_server);
}

// module_init(terminal_init);
// module_exit(terminal_exit);
