#ifndef TERMINAL_DRIVER_H
#define TERMINAL_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "platform_bus.h"
#include "platform_device.h"

/************************************************************************/
/*                        VGA Color Definitions                         */
/************************************************************************/
enum VgaColor {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN   = 14,
    VGA_COLOR_WHITE         = 15,
};

static inline uint8_t to_vga_color(enum VgaColor fg, enum VgaColor bg)
{
    return (uint8_t)(fg | bg << 4);
}

static inline uint16_t to_vga_char(uint8_t chr, uint8_t color)
{
    return (uint16_t)chr | (uint16_t)color << 8;
}

/************************************************************************/
/*                      Terminal Public API                             */
/************************************************************************/
void terminal_init(void);
void terminal_exit(void);

void terminal_flush(void);
void terminal_write_at(char chr, uint8_t color, size_t x, size_t y);
void terminal_write_at_str(const char* str, uint8_t color, size_t x, size_t y);
void terminal_write(const char* str);

#endif /* TERMINAL_DRIVER_H */
