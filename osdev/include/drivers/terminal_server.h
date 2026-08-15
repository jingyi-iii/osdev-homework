#ifndef TERMINAL_SERVER_H
#define TERMINAL_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include "drivers/platform_bus.h"

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
void terminal_flush(const char* unused);
void terminal_write_at(char chr, uint8_t color, size_t x, size_t y);
void terminal_write_at_str(const char* str, uint8_t color, size_t x, size_t y);
void terminal_write(const char* str);
void terminal_write_color(const char* str, uint8_t color);
void terminal_putchar(char c);
size_t terminal_get_row(void);

typedef void (*terminal_cmd_fn)(const char* args);
int terminal_register_cmd(const char* name, terminal_cmd_fn callback);

/* Switch display back to VGA text mode 0x03 (80×25) */
void terminal_switch_to_text_mode(void);
void terminal_unregister_cmd(const char* name);

/************************************************************************/
/*                      RING3 Direct Access                             */
/************************************************************************/

/*
 * The terminal_* APIs above are plain functions: callable from both kernel
 * (CPL0) and user (CPL3) threads, writing the VGA hardware directly
 * (buffer at 0xB8000 + CRT controller ports).  No syscall gate is
 * involved — RING3 works because VGA is identity-mapped with
 * PTE_USER_PAGE and user threads run with IOPL=3.
 */

#endif /* TERMINAL_SERVER_H */
