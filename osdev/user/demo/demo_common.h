/*
 * user/demo/demo_common.h — source-compatibility shims for the demo test
 * suites, originally written against the old drivers/* + kernel APIs.
 *
 * In the user-mode ELF world:
 *   - terminal_write* -> console portal (terminal_server prints it)
 *   - timer_delay_ms    -> rtc server SLEEP_MS portal RPC (rtc_server.elf,
 *     PIT-counter timed; falls back to a yield loop if the server is down)
 *   - thread_* / proc_* -> userlib syscall wrappers (see userlib.h)
 *   - colors / priv     -> plain constants (colors are ignored by the
 *     terminal server, kept for source compatibility)
 */
#ifndef DEMO_COMMON_H
#define DEMO_COMMON_H

#include "userlib.h"

/* --- VGA palette + attribute packing (source compat; unused at runtime) --- */
#define VGA_COLOR_BLACK         0
#define VGA_COLOR_BLUE          1
#define VGA_COLOR_GREEN         2
#define VGA_COLOR_CYAN          3
#define VGA_COLOR_RED           4
#define VGA_COLOR_MAGENTA       5
#define VGA_COLOR_BROWN         6
#define VGA_COLOR_LIGHT_GREY    7
#define VGA_COLOR_DARK_GREY     8
#define VGA_COLOR_LIGHT_BLUE    9
#define VGA_COLOR_LIGHT_GREEN   10
#define VGA_COLOR_LIGHT_CYAN    11
#define VGA_COLOR_LIGHT_RED     12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW        14
#define VGA_COLOR_WHITE         15
#define to_vga_color(fg, bg)    ((u8)(((bg) << 4) | (fg)))

/* --- privilege constants (kernel enum values) --- */
#define TASK_PRIV_KERNEL  0
#define TASK_PRIV_USER    1
#define PROC_PRIV_KERNEL  0
#define PROC_PRIV_USER    1

typedef void (*task_entry_t)(void);

/*
 * Map the old kernel process/thread API names onto the userlib syscall
 * wrappers so the ported suites keep their original call sites.
 * (Terminal/timer shims above are real functions; these are pure
 * renames — the userlib wrappers are static inlines in userlib.h.)
 */
#define thread_create(...)   user_thread_create(__VA_ARGS__)
#define thread_exit(...)     user_thread_exit(__VA_ARGS__)
#define thread_block(...)    user_thread_block(__VA_ARGS__)
#define thread_unblock(...)  user_thread_unblock(__VA_ARGS__)
#define thread_get_tid(...)  user_thread_get_tid(__VA_ARGS__)
#define thread_yield(...)    user_yield(__VA_ARGS__)
#define proc_create(...)     user_proc_create(__VA_ARGS__)
#define proc_exit(...)       user_proc_exit(__VA_ARGS__)
#define proc_block(...)      user_proc_block(__VA_ARGS__)
#define proc_unblock(...)    user_proc_unblock(__VA_ARGS__)
#define proc_get_pid(...)    user_proc_get_pid(__VA_ARGS__)

/* --- old drivers API surface, now backed by the user ABI --- */
void terminal_write(const char* s);
void terminal_write_color(const char* s, u8 attr);
void terminal_flush(int mode);
int  terminal_get_row(void);
void terminal_switch_to_text_mode(void);
void timer_delay_ms(u32 ms);
void log_write(const char* s);

/* --- graphics API (demo_common.c) — terminal server graphics mode ---
 * A single shared frame buffer in this demo process:
 *   gfx_clear_screen / gfx_put_pixel / gfx_fill_rect draw into it,
 *   gfx_flush() blits the whole buffer to the screen through the console
 *   portal (one shm_share per flush — no per-pixel IPC). */
#define GFX_W 320
#define GFX_H 200
extern u8 gfx_fb[GFX_FB_SIZE];   /* 320*200 = 64000 bytes (static) */

int  gfx_set_graphics_mode(void);   /* switch VGA to mode 0x13 (0 = ok) */
int  gfx_set_text_mode(void);       /* switch back to 80x25 text (0 = ok) */
void gfx_clear_screen(u8 color);
void gfx_put_pixel(int x, int y, u8 color);
void gfx_fill_rect(int x, int y, int w, int h, u8 color);
int  gfx_flush(void);               /* blit the shared fb (0 = ok) */

/*
 * Set by a test suite right before it exits (as a thread of the menu
 * process).  The menu polls it to redraw itself.  Defined in
 * process_test_entry.c.
 */
extern volatile int test_finished_flag;

#endif
