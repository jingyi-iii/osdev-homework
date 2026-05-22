#include "terminal_driver.h"
#include "spinlock.h"
#include "module.h"

/************************************************************************/
/*                        Internal Definitions                          */
/************************************************************************/
#define VGA_WIDTH      80
#define VGA_HEIGHT     25
#define VGA_BUF_ADDR   0xB8000

struct terminal_device {
    struct platform_device plat_dev;
    spinlock* lock;
    uint16_t* vga_buffer;
    size_t curr_row;
    size_t curr_col;
    uint8_t curr_color;
};

static struct terminal_device term_device = {
    .plat_dev = {
        .dev = {
            .name = "terminal",
            .type = "terminal",
        },
        .num_res = 1,
        .resources[0] = {
            .type = PLAT_RES_MEM,
            .mem.addr = VGA_BUF_ADDR,
            .mem.size = VGA_WIDTH * VGA_HEIGHT * 2,
        },
    },
    .lock = NULL,
    .vga_buffer = (uint16_t*)VGA_BUF_ADDR,
    .curr_row = 0,
    .curr_col = 0,
    .curr_color = 0,
};

/************************************************************************/
/*                      Driver Probe / Remove                           */
/************************************************************************/
static int terminal_probe(struct device* dev)
{
    (void)dev;

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
void terminal_flush(void)
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

/************************************************************************/
/*                      Init / Exit                                     */
/************************************************************************/
void terminal_init(void)
{
    platform_driver_register(&terminal_driver);
    platform_device_register(&term_device.plat_dev.dev);
}

void terminal_exit(void)
{
    platform_driver_unregister(&terminal_driver);
    platform_device_unregister(&term_device.plat_dev.dev);
}

module_init(terminal_init);
