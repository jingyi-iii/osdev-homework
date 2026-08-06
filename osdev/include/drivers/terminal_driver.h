#ifndef TERMINAL_DRIVER_H
#define TERMINAL_DRIVER_H

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
/*                      Terminal Syscall (RING3)                        */
/************************************************************************/

/* Syscall minor number on the syscall gate (major 100) for RING3 access */
#define TERMINAL_SYSCALL_MINOR  (4)

/* Terminal syscall commands */
typedef enum {
    TERM_SYSCALL_WRITE       = 0,
    TERM_SYSCALL_WRITE_COLOR = 1,
    TERM_SYSCALL_PUTCHAR     = 2,
    TERM_SYSCALL_FLUSH       = 3,
    TERM_SYSCALL_GET_ROW     = 4,
} terminal_syscall_cmd;

/* Data structure for the terminal syscall */
typedef struct terminal_syscall_data {
    uint32_t    cmd;        /* terminal_syscall_cmd */
    const char* buf;        /* string for WRITE / WRITE_COLOR */
    size_t      size;       /* string length (unused, null-terminated) */
    uint8_t     color;      /* color for WRITE_COLOR */
    char        chr;        /* character for PUTCHAR */
    size_t      row;        /* out: current row for GET_ROW */
} terminal_syscall_data;

/* Ring-3 accessible wrappers — these go through the syscall gate, so they
 * can be called from both kernel (CPL0) and user (CPL3) threads. */
void sys_terminal_write(const char* str);
void sys_terminal_write_color(const char* str, uint8_t color);
void sys_terminal_putchar(char c);
void sys_terminal_flush(void);
size_t sys_terminal_get_row(void);

#endif /* TERMINAL_DRIVER_H */
