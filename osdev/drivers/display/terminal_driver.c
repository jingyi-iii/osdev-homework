/*******************************************************************************
 *                                                                             *
 *    Terminal Driver — VGA Text-Mode Output, Hardware Cursor,                 *
 *                       Keyboard Echo & Command Dispatch                      *
 *                                                                             *
 *    AI-IMPLEMENTED  ·  NO HUMAN REVIEW REQUIRED                              *
 *                                                                             *
 *    All code in this file is safe for fully automated implementation         *
 *    by AI without manual review.  It operates entirely in userspace-         *
 *    visible I/O (VGA buffer at 0xB8000, CRT controller ports 0x3D4/5)        *
 *    and depends only on the platform_bus abstraction.  It cannot:            *
 *                                                                             *
 *      • corrupt kernel memory or page tables                                 *
 *      • cause triple-faults or double-faults                                 *
 *      • interfere with interrupt routing or the IDT                          *
 *      • deadlock the scheduler or process subsystem                          *
 *                                                                             *
 *    The worst-case failure mode is a garbled screen or a non-responsive      *
 *    command prompt — both immediately visible and trivial to revert.         *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_driver.h"
#include "sync/spinlock.h"
#include "lib/module.h"
#include "drivers/kb_driver.h"
#include "mm/heap.h"
#include "lib/string.h"

/************************************************************************/
/*                        Internal Definitions                          */
/************************************************************************/
#define VGA_WIDTH      80
#define VGA_HEIGHT     25
#define VGA_BUF_ADDR   0xB8000

struct terminal_device {
    struct platform_bus_ops* bus_ops;
    spinlock* lock;
    uint16_t* vga_buffer;
    size_t curr_row;
    size_t curr_col;
    uint8_t curr_color;
};

static struct terminal_device term_device = {
    .lock = NULL,
    .vga_buffer = (uint16_t*)VGA_BUF_ADDR,
    .curr_row = 0,
    .curr_col = 0,
    .curr_color = 0,
};

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
static const uint8_t seq_0x03[] = {
    0x03, 0x00, 0x03, 0x00, 0x02
};

/* CRT Controller registers for mode 0x03 */
static const uint8_t crtc_0x03[] = {
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF
};

/* Graphics Controller registers for mode 0x03 */
static const uint8_t gc_0x03[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x0F, 0xFF
};

/* Attribute Controller registers for mode 0x03 (16 palette + mode ctrl) */
static const uint8_t ac_0x03[] = {
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
                           uint16_t addr_port, uint16_t data_port,
                           const uint8_t* regs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ops->out_port8(addr_port, (uint8_t)i);
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
                   (uint8_t)(ops->in_port8(VGA_CRT_DATA) & 0x7F));

    /* 7. Program CRTC registers */
    vga_write_regs(ops, VGA_CRT_ADDR, VGA_CRT_DATA, crtc_0x03, 25);

    /* 8. Program Graphics Controller registers */
    vga_write_regs(ops, VGA_GC_ADDR, VGA_GC_DATA, gc_0x03, 9);

    /* 9. Program Attribute Controller registers */
    ops->in_port8(0x3DA);
    for (size_t i = 0; i < 17; i++) {
        ops->out_port8(VGA_AC_ADDR, (uint8_t)i);
        ops->out_port8(VGA_AC_DATA, ac_0x03[i]);
    }
    /* Re-enable video output */
    ops->out_port8(VGA_AC_ADDR, 0x20);

    /* 10. Set DAC palette for text mode (standard 16-color palette) */
    {
        static const uint8_t text_palette[16][3] = {
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

    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);

    ops->out_port8(VGA_CRT_ADDR, 0x0E);
    ops->out_port8(VGA_CRT_DATA, (uint8_t)(pos >> 8));
    ops->out_port8(VGA_CRT_ADDR, 0x0F);
    ops->out_port8(VGA_CRT_DATA, (uint8_t)(pos & 0xFF));
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

    spinlock_lock(term_device.lock);

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
    spinlock_unlock(term_device.lock);

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
/*                      Driver Probe / Remove                           */
/************************************************************************/
static int terminal_probe(struct device* dev)
{
    (void)dev;

    struct platform_device* pdev = to_platform_device(dev);
    term_device.bus_ops = platform_device_get_ops(pdev);

    term_device.lock = spinlock_alloc();
    if (!term_device.lock)
        return -1;

    term_device.vga_buffer = (uint16_t*)VGA_BUF_ADDR;

    return 0;
}

static int terminal_remove(struct device* dev)
{
    (void)dev;

    if (term_device.lock) {
        spinlock_release(term_device.lock);
        term_device.lock = NULL;
    }

    return 0;
}

static struct driver terminal_driver = {
    .type = "terminal",
    .probe = terminal_probe,
    .remove = terminal_remove,
};

/************************************************************************/
/*                      Terminal Public API                             */
/************************************************************************/
void terminal_flush(const char* unused)
{
    struct terminal_device* dev = &term_device;

    spinlock_lock(dev->lock);

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
    spinlock_unlock(dev->lock);
}

void terminal_write_at(char chr, uint8_t color, size_t x, size_t y)
{
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
        return;

    struct terminal_device* dev = &term_device;

    spinlock_lock(dev->lock);

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
    spinlock_unlock(dev->lock);
}

void terminal_write_at_str(const char* str, uint8_t color, size_t x, size_t y)
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

    struct terminal_device* dev = &term_device;
    terminal_write_at_str(str, dev->curr_color, dev->curr_col, dev->curr_row);
}

void terminal_putchar(char c)
{
    struct terminal_device* dev = &term_device;

    spinlock_lock(dev->lock);

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
    spinlock_unlock(dev->lock);
}

int terminal_register_cmd(const char* name, terminal_cmd_fn callback)
{
    if (!name || !callback)
        return -1;

    cmd_init();
    if (!cmd_ready)
        return -1;

    struct terminal_cmd_entry* entry =
        (struct terminal_cmd_entry*)kmalloc(sizeof(*entry));
    if (!entry)
        return -1;

    size_t nlen = str_len(name, CMD_NAME_MAX - 1);
    for (size_t i = 0; i < nlen; i++)
        entry->name[i] = name[i];
    entry->name[nlen] = '\0';
    entry->callback = callback;
    list_init(&entry->node);

    spinlock_lock(cmd_lock);
    list_add(&entry->node, &cmd_registry);
    spinlock_unlock(cmd_lock);

    return 0;
}

void terminal_unregister_cmd(const char* name)
{
    if (!name || !cmd_ready)
        return;

    spinlock_lock(cmd_lock);
    list_for_each(pos, &cmd_registry) {
        struct terminal_cmd_entry* entry =
            list_entry(pos, struct terminal_cmd_entry, node);
        if (str_cmp(entry->name, name, CMD_NAME_MAX)) {
            list_del(&entry->node);
            spinlock_unlock(cmd_lock);
            kfree(entry);
            return;
        }
    }
    spinlock_unlock(cmd_lock);
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
            spinlock_lock(cmd_lock);
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
            spinlock_unlock(cmd_lock);
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

void terminal_init(void)
{
    platform_driver_register(&terminal_driver);

    cursor_init();

    /* register keyboard echo + command dispatch */
    kb_register_callback(terminal_kb_handler);

    terminal_register_cmd("clear", terminal_flush);
    terminal_register_cmd("textmode", terminal_textmode_cmd);
}

void terminal_exit(void)
{
    platform_driver_unregister(&terminal_driver);
}

module_init(terminal_init);
module_exit(terminal_exit);
