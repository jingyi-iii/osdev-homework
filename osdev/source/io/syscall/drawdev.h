#ifndef __DRAWDEV_H__
#define __DRAWDEV_H__

#include "iodev.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VGA Graphics Mode Constants */
#define VGA_FRAMEBUFFER_ADDR    0xA0000
#define VGA_MODE_13H            0x13    /* 320x200x8bpp */
#define VGA_TEXT_MODE           0x03    /* 80x25 text mode */

#define VGA_FB_WIDTH            320
#define VGA_FB_HEIGHT           200
#define VGA_FB_BPP              8
#define VGA_FB_SIZE             (VGA_FB_WIDTH * VGA_FB_HEIGHT)

/* Color palette for 8bpp mode (256 colors) */
typedef struct vga_palette {
    uint8_t r;    /* Red component (0-63) */
    uint8_t g;    /* Green component (0-63) */
    uint8_t b;    /* Blue component (0-63) */
} vga_palette;

/* Framebuffer information structure */
typedef struct vga_fb_info {
    uint8_t* framebuffer;     /* Pointer to framebuffer (0xA0000) */
    uint32_t width;           /* Screen width in pixels */
    uint32_t height;          /* Screen height in pixels */
    uint32_t bpp;             /* Bits per pixel */
    uint32_t pitch;           /* Bytes per scanline */
    uint8_t  is_graphics;     /* 1 if in graphics mode, 0 if text mode */
} vga_fb_info;

typedef enum draw_cmd {
    GRAPHIC_MODE,
    CHAR_MODE,
    GET_FB_INFO,              /* Get framebuffer info */
    SET_PIXEL,                /* Set a single pixel */
    SET_PALETTE,              /* Set DAC palette register */
} draw_cmd;

/* Pixel drawing command argument */
typedef struct pixel_cmd {
    int32_t x;
    int32_t y;
    uint8_t color;
} pixel_cmd;

/* Palette command argument */
typedef struct palette_cmd {
    uint8_t index;            /* Palette index (0-255) */
    vga_palette color;        /* RGB color values */
} palette_cmd;

int drawdev_init(iodev **out_dev);

#ifdef __cplusplus
}
#endif

#endif
