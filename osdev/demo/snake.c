/*******************************************************************************
 *                                                                             *
 *    Snake Game — A classic snake game running as a kernel process.           *
 *                 Rendered via graphics_driver (VGA mode 0x13),              *
 *                 controlled via kb_driver keyboard callbacks.                *
 *                                                                             *
 *    Controls:  W/A/S/D  —  move Up/Left/Down/Right                          *
 *               R        —  restart after game over                          *
 *               Q        —  quit the game                                    *
 *                                                                             *
 *******************************************************************************/

#include "drivers/graphics_driver.h"
#include "drivers/kb_driver.h"
#include "drivers/log_driver.h"
#include "drivers/timer_driver.h"
#include "kernel/process.h"
#include "lib/string.h"
#include "mm/heap.h"

/* ===========================================================
 *  Game Constants
 * =========================================================== */

#define SNAKE_TILE       8                /* pixel size of each snake cell     */
#define GRID_W           (GFX_WIDTH  / SNAKE_TILE)   /* 40 columns             */
#define GRID_H           (GFX_HEIGHT / SNAKE_TILE)   /* 25 rows                */
#define SNAKE_MAX_LEN    ((GRID_W) * (GRID_H))       /* theoretical max        */
#define SNAKE_INIT_LEN   4                           /* starting length         */
#define GAME_SPEED_MS    120                         /* ms per tick             */

/* Colors */
#define CLR_BG           GFX_BLACK
#define CLR_BORDER       GFX_DARK_GREY
#define CLR_SNAKE_HEAD   GFX_LIGHT_GREEN
#define CLR_SNAKE_BODY   GFX_GREEN
#define CLR_FOOD         GFX_LIGHT_RED
#define CLR_SCORE        GFX_WHITE
#define CLR_GAMEOVER     GFX_YELLOW

/* ===========================================================
 *  Direction
 * =========================================================== */

typedef enum {
    DIR_UP    = 0,
    DIR_DOWN  = 1,
    DIR_LEFT  = 2,
    DIR_RIGHT = 3,
} direction_t;

/* ===========================================================
 *  Game State
 * =========================================================== */

typedef struct {
    int x;
    int y;
} point_t;

static point_t   snake[SNAKE_MAX_LEN];    /* ring buffer: [0] = head       */
static int       snake_len;               /* current length                */
static direction_t dir;                   /* current movement direction    */
static point_t   food;                    /* food position                 */
static int       game_over;               /* 0 = running, 1 = over         */
static int       game_quit;               /* 0 = running, 1 = quit by user */
static int       game_restart;            /* 0 = no,      1 = restart game */
static int       score;                   /* food eaten count              */

/* ===========================================================
 *  Forward declarations
 * =========================================================== */

static void draw_border(void);
static void draw_snake(void);
static void draw_food(void);
static void draw_score(void);
static void draw_game_over(void);
static void spawn_food(void);
static void move_snake(void);

/* ===========================================================
 *  Simple pseudo-random number generator (LCG)
 * =========================================================== */

static unsigned int rand_seed = 12345;

static unsigned int my_rand(void)
{
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}

/* ===========================================================
 *  Keyboard Callback
 * =========================================================== */

static void snake_kb_handler(const char* data, size_t size)
{
    (void)size;
    if (!data) return;

    char key = data[0];

    switch (key) {
    case 'w': case 'W': case UP:
        if (dir != DIR_DOWN)  dir = DIR_UP;
        break;
    case 's': case 'S': case DOWN:
        if (dir != DIR_UP)    dir = DIR_DOWN;
        break;
    case 'a': case 'A': case LEFT:
        if (dir != DIR_RIGHT) dir = DIR_LEFT;
        break;
    case 'd': case 'D': case RIGHT:
        if (dir != DIR_LEFT)  dir = DIR_RIGHT;
        break;
    case 'q': case 'Q':
        game_quit = 1;
        game_over = 1;
        break;
    case 'r': case 'R':
        if (game_over) {
            game_restart = 1;
        }
        break;
    default:
        break;
    }
}

/* ===========================================================
 *  Drawing Helpers
 * =========================================================== */

static void draw_tile(int gx, int gy, uint8_t color)
{
    gfx_fill_rect(
        (size_t)(gx * SNAKE_TILE),
        (size_t)(gy * SNAKE_TILE),
        SNAKE_TILE, SNAKE_TILE, color);
}

static void draw_border(void)
{
    /* top & bottom border */
    for (int x = 0; x < GRID_W; x++) {
        draw_tile(x, 0,          CLR_BORDER);
        draw_tile(x, GRID_H - 1, CLR_BORDER);
    }
    /* left & right border (skip corners already drawn) */
    for (int y = 1; y < GRID_H - 1; y++) {
        draw_tile(0,         y, CLR_BORDER);
        draw_tile(GRID_W - 1, y, CLR_BORDER);
    }
}

static void draw_snake(void)
{
    /* draw body segments (back to front so head overwrites overlaps) */
    for (int i = snake_len - 1; i >= 0; i--) {
        uint8_t color = (i == 0) ? CLR_SNAKE_HEAD : CLR_SNAKE_BODY;
        draw_tile(snake[i].x, snake[i].y, color);
    }
}

static void draw_food(void)
{
    draw_tile(food.x, food.y, CLR_FOOD);
}

static void draw_score(void)
{
    /* score bar at the bottom, outside the playable grid if possible,
     * but since we use the full grid, overlay on border row 0 */
    char buf[32];
    memset(buf, 0, sizeof(buf));
    int len = snprintf(buf, sizeof(buf), "SCORE: %d", score);
    /* center the text on row 0 (border row) */
    int col = (int)((GRID_W - len) / 2);
    if (col < 0) col = 0;

    gfx_write(buf, (size_t)col, 0, CLR_SCORE, CLR_BORDER);
}

static void draw_game_over(void)
{
    const char* msg1 = "GAME OVER";
    const char* msg2 = "R:Restart  Q:Quit";
    int col1 = (int)((GRID_W - 8) / 2);
    int col2 = (int)((GRID_W - 17) / 2);

    gfx_write(msg1, (size_t)(col1 > 0 ? col1 : 1), (size_t)(GRID_H / 2 - 1),
              CLR_GAMEOVER, CLR_BG);
    gfx_write(msg2, (size_t)(col2 > 0 ? col2 : 1), (size_t)(GRID_H / 2 + 1),
              GFX_WHITE, CLR_BG);
}

/* ===========================================================
 *  Game Logic
 * =========================================================== */

static int is_on_snake(int x, int y)
{
    for (int i = 0; i < snake_len; i++) {
        if (snake[i].x == x && snake[i].y == y)
            return 1;
    }
    return 0;
}

static void spawn_food(void)
{
    int attempts = 0;
    do {
        food.x = (int)(my_rand() % (GRID_W - 2)) + 1;  /* avoid border */
        food.y = (int)(my_rand() % (GRID_H - 2)) + 1;
        attempts++;
    } while (is_on_snake(food.x, food.y) && attempts < (GRID_W * GRID_H));

    /* if grid is completely full (unlikely), just pick any spot */
}

static void move_snake(void)
{
    /* compute new head position */
    int new_x = snake[0].x;
    int new_y = snake[0].y;

    switch (dir) {
    case DIR_UP:    new_y--; break;
    case DIR_DOWN:  new_y++; break;
    case DIR_LEFT:  new_x--; break;
    case DIR_RIGHT: new_x++; break;
    }

    /* check collision with walls (border is at 0 and GRID_W-1 / GRID_H-1) */
    if (new_x <= 0 || new_x >= GRID_W - 1 ||
        new_y <= 0 || new_y >= GRID_H - 1) {
        game_over = 1;
        return;
    }

    /* check collision with self (skip tail since it will move away unless
     * food was just eaten; we handle the "eat" case below) */
    int ate_food = (new_x == food.x && new_y == food.y);
    int check_len = ate_food ? snake_len : snake_len - 1;
    for (int i = 0; i < check_len; i++) {
        if (snake[i].x == new_x && snake[i].y == new_y) {
            game_over = 1;
            return;
        }
    }

    /* shift the ring buffer: insert new head, drop tail if not eating */
    if (ate_food) {
        /* grow: shift everything right, insert new head */
        for (int i = snake_len; i > 0; i--)
            snake[i] = snake[i - 1];
        snake_len++;
        score++;
        spawn_food();
    } else {
        /* normal move: shift right, drop last */
        for (int i = snake_len - 1; i > 0; i--)
            snake[i] = snake[i - 1];
    }
    snake[0].x = new_x;
    snake[0].y = new_y;
}

static void render_frame(void)
{
    gfx_clear(CLR_BG);
    draw_border();
    draw_food();
    draw_snake();
    draw_score();
}

/* ===========================================================
 *  Main Game Thread
 * =========================================================== */

void snake_thread(void)
{
    KLOG("snake_thread started");

    /* register keyboard listener */
    kb_register_callback(snake_kb_handler);

    do {
        game_restart = 0;

        /* ====== initialize game state ====== */
        snake_len = SNAKE_INIT_LEN;
        dir       = DIR_RIGHT;
        game_over = 0;
        game_quit = 0;
        score     = 0;

        /* place initial snake horizontally in the middle */
        int start_x = GRID_W / 2;
        int start_y = GRID_H / 2;
        for (int i = 0; i < snake_len; i++) {
            snake[i].x = start_x - i;
            snake[i].y = start_y;
        }

        rand_seed = 12345;
        spawn_food();

        /* ====== main game loop ====== */
        while (!game_over) {
            render_frame();
            timer_delay_ms(GAME_SPEED_MS);
            move_snake();
            thread_yield();
        }

        /* ====== game over ====== */
        render_frame();
        draw_game_over();

        /* wait for user to press R (restart) or Q (quit) */
        while (!game_quit && !game_restart) {
            timer_delay_ms(50);
            thread_yield();
        }

    } while (game_restart && !game_quit);

    KLOG("snake_thread exiting, score=%d", score);

    kb_unregister_callback(snake_kb_handler);
    proc_exit(0);
}
