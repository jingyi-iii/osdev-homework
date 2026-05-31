#ifndef GRAPHICS_DRIVER_H
#define GRAPHICS_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "drivers/platform_bus.h"

/************************************************************************/
/*                    Graphics Mode Definitions                         */
/************************************************************************/

/* VGA mode 0x13: 320x200, 256 colors, linear framebuffer at 0xA0000 */
#define GFX_WIDTH       320
#define GFX_HEIGHT      200
#define GFX_BUF_ADDR    0xA0000

/* Font dimensions (8x16 VGA-style glyphs) */
#define FONT_WIDTH      8
#define FONT_HEIGHT     16
#define GFX_COLS        (GFX_WIDTH  / FONT_WIDTH)   /* 40 columns */
#define GFX_ROWS        (GFX_HEIGHT / FONT_HEIGHT)  /* 12 rows   */

/* 256-color palette (same as VGA default) */
enum gfx_color {
    GFX_BLACK        = 0,
    GFX_BLUE         = 1,
    GFX_GREEN        = 2,
    GFX_CYAN         = 3,
    GFX_RED          = 4,
    GFX_MAGENTA      = 5,
    GFX_BROWN        = 6,
    GFX_LIGHT_GREY   = 7,
    GFX_DARK_GREY    = 8,
    GFX_LIGHT_BLUE   = 9,
    GFX_LIGHT_GREEN  = 10,
    GFX_LIGHT_CYAN   = 11,
    GFX_LIGHT_RED    = 12,
    GFX_LIGHT_MAGENTA= 13,
    GFX_YELLOW       = 14,
    GFX_WHITE        = 15,
};

/************************************************************************/
/*                    Graphics Public API                               */
/************************************************************************/

/* Initialize graphics mode (switch from text to mode 0x13) */
void gfx_init(void);

/* Switch back to text mode */
void gfx_exit(void);

/* Clear the entire screen with a color */
void gfx_clear(uint8_t color);

/* Draw a single pixel */
void gfx_put_pixel(size_t x, size_t y, uint8_t color);

/* Draw a filled rectangle */
void gfx_fill_rect(size_t x, size_t y, size_t w, size_t h, uint8_t color);

/* Draw a character at (col, row) in character-cell coordinates */
void gfx_put_char(char c, size_t col, size_t row, uint8_t fg, uint8_t bg);

/* Write a null-terminated string starting at (col, row) */
void gfx_write(const char* str, size_t col, size_t row, uint8_t fg, uint8_t bg);

/* Scroll the screen up by one row */
void gfx_scroll(uint8_t bg);

/* Terminal-style putchar (advances cursor, handles \n, \b) */
void gfx_putchar(char c);

/* Get current cursor position */
void gfx_get_cursor(size_t* col, size_t* row);

/* Set cursor position */
void gfx_set_cursor(size_t col, size_t row);

#endif /* GRAPHICS_DRIVER_H */
