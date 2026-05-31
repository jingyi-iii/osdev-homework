#include "kernel/init.h"
#include "drivers/graphics_driver.h"
#include "drivers/kb_driver.h"
#include "drivers/timer_driver.h"
#include "kernel/process.h"

/* Demo thread entry points (defined in demo/) */
extern void snake_thread(void);
extern void airplane_thread(void);

/* Menu state: 0 = waiting, 1 = snake, 2 = airplane */
static int menu_choice = 0;

/* Set by a game thread when the user presses Q to quit.
 * init_thread polls this to bring the menu back.
 * volatile prevents the compiler from caching the value in a register. */
volatile int game_exited_flag = 0;

static void menu_kb_handler(const char* data, size_t size)
{
    (void)size;
    if (!data) return;

    char key = data[0];
    if (key == '1') menu_choice = 1;
    if (key == '2') menu_choice = 2;
}

static void draw_menu(void)
{
    gfx_clear(GFX_BLACK);

    /* Top decorative bar */
    gfx_fill_rect(0, 0, GFX_WIDTH, 22, GFX_DARK_GREY);
    gfx_write("===  GAME  MENU  ===", 9, 0, GFX_YELLOW, GFX_DARK_GREY);

    /* Option 1 — Snake */
    gfx_write("[1]  SNAKE GAME", 4, 3, GFX_LIGHT_GREEN, GFX_BLACK);
    gfx_write("     Eat, grow, avoid walls!", 4, 4,
              GFX_DARK_GREY, GFX_BLACK);

    /* Option 2 — Airplane */
    gfx_write("[2]  AIRPLANE BATTLE", 4, 6, GFX_LIGHT_CYAN, GFX_BLACK);
    gfx_write("     Shoot, dodge, survive!", 4, 7,
              GFX_DARK_GREY, GFX_BLACK);

    /* Separator + prompt */
    gfx_write("----------------------------------------", 0, 9,
              GFX_DARK_GREY, GFX_BLACK);
    gfx_write("    Press  1  or  2  to select a game", 3, 10,
              GFX_WHITE, GFX_BLACK);
}

void init_thread(void)
{
    for (;;) {
        menu_choice = 0;
        game_exited_flag = 0;

        draw_menu();
        kb_register_callback(menu_kb_handler);

        /* Wait for user to choose a game */
        while (menu_choice == 0) {
            timer_delay_ms(50);
            thread_yield();
        }

        kb_unregister_callback(menu_kb_handler);

        if (menu_choice == 1) {
            proc_create(PROC_PRIV_KERNEL, snake_thread);
        } else {
            proc_create(PROC_PRIV_KERNEL, airplane_thread);
        }

        /* Wait for the game to quit (user presses Q),
         * then loop back and redraw the menu. */
        while (!game_exited_flag) {
            timer_delay_ms(100);
            thread_yield();
        }
    }
}