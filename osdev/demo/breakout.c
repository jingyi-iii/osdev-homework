/*******************************************************************************
 *                                                                             *
 *    Breakout — A classic brick-breaking game running as a kernel process.    *
 *               Rendered via graphics_server (VGA mode 0x13),                 *
 *               controlled via kb_server keyboard callbacks.                  *
 *                                                                             *
 *    Controls:  A/D or Left/Right  —  move paddle                             *
 *               Space              —  launch ball / serve                     *
 *               R                  —  restart after game over / win           *
 *               Q                  —  quit the game                           *
 *                                                                             *
 *******************************************************************************/

#include "drivers/graphics_server.h"
#include "drivers/kb_server.h"
#include "drivers/log_server.h"
#include "drivers/timer_server.h"
#include "kernel/process.h"
#include "lib/string.h"
#include "mm/heap.h"

/* ===========================================================
 *  Game Constants
 * =========================================================== */

#define TILE_SIZE          8
#define GRID_W             (GFX_WIDTH  / TILE_SIZE)   /* 40 */
#define GRID_H             (GFX_HEIGHT / TILE_SIZE)   /* 25 */

/* Paddle */
#define PADDLE_W           5    /* tiles wide */
#define PADDLE_Y           (GRID_H - 3)  /* row */

/* Ball */
#define BALL_SIZE          1    /* tiles */

/* Brick layout */
#define BRICK_ROWS         5
#define BRICK_COLS         8
#define BRICK_TOP_Y         3    /* top row of bricks */
#define BRICK_LEFT_X        4    /* leftmost brick column */
#define BRICK_GAP           1    /* gap between bricks (tiles) */
#define BRICK_W             3    /* each brick width in tiles */
#define BRICK_H             1    /* each brick height in tiles */

#define MAX_BRICKS          (BRICK_ROWS * BRICK_COLS)

/* Gameplay */
#define PLAYER_LIVES        3
#define GAME_SPEED_MS       150
#define SERVE_SPEED_MS      150

/* Colors */
#define CLR_BG              GFX_BLACK
#define CLR_PADDLE          GFX_LIGHT_CYAN
#define CLR_PADDLE_EDGE     GFX_CYAN
#define CLR_BALL            GFX_WHITE
#define CLR_BRICK_COLORS    { GFX_LIGHT_RED, GFX_LIGHT_MAGENTA, \
                              GFX_LIGHT_GREEN, GFX_YELLOW,      \
                              GFX_LIGHT_BLUE }
#define CLR_HUD             GFX_WHITE
#define CLR_WALL            GFX_DARK_GREY
#define CLR_GAMEOVER        GFX_YELLOW

/* ===========================================================
 *  Data Structures
 * =========================================================== */

typedef struct {
    int x, y;
} point;

typedef struct {
    int x, y;
    int alive;
    u8 color;
} brick;

/* ===========================================================
 *  Game State
 * =========================================================== */

static point   paddle;
static point   ball;
static int       ball_dx, ball_dy;
static int       ball_attached;     /* 1 = ball riding on paddle */
static brick   bricks[MAX_BRICKS];
static int       bricks_left;
static int       player_lives;
static int       score;
static int       game_over;
static int       game_quit;
static int       game_restart;
static int       victory;

/* Particle effects for brick destruction */
#define MAX_PARTICLES   20
static point   particles[MAX_PARTICLES];
static int       particle_timers[MAX_PARTICLES];
static u8   particle_colors[MAX_PARTICLES];

/* ===========================================================
 *  Forward Declarations
 * =========================================================== */

static void draw_paddle(void);
static void draw_ball(void);
static void draw_bricks(void);
static void draw_hud(void);
static void draw_walls(void);
static void draw_particles(void);
static void draw_game_over_screen(void);
static void init_bricks(void);
static void move_ball(void);
static void check_collisions(void);
static void spawn_particles(int x, int y, u8 color);
static void update_particles(void);
static void render_frame(void);

/* ===========================================================
 *  Simple PRNG (LCG)
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

static void breakout_kb_handler(const char* data, size_t size)
{
    (void)size;
    if (!data) return;

    char key = data[0];

    switch (key) {
    case 'a': case 'A': case LEFT:
        if (!game_over) {
            paddle.x -= 2;
            if (paddle.x < 1) paddle.x = 1;
            if (ball_attached) ball.x = paddle.x + PADDLE_W / 2;
        }
        break;
    case 'd': case 'D': case RIGHT:
        if (!game_over) {
            paddle.x += 2;
            if (paddle.x > GRID_W - 1 - PADDLE_W)
                paddle.x = GRID_W - 1 - PADDLE_W;
            if (ball_attached) ball.x = paddle.x + PADDLE_W / 2;
        }
        break;
    case ' ':
        if (!game_over && ball_attached) {
            ball_attached = 0;
            ball_dx = ((int)(my_rand() % 3)) - 1;  /* -1, 0, or 1 */
            if (ball_dx == 0) ball_dx = 1;
            ball_dy = -1;
        }
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

static void draw_tile(int gx, int gy, u8 color)
{
    gfx_fill_rect(
        (size_t)(gx * TILE_SIZE),
        (size_t)(gy * TILE_SIZE),
        TILE_SIZE, TILE_SIZE, color);
}

static void draw_paddle(void)
{
    int gx = paddle.x;
    int gy = paddle.y;

    for (int i = 0; i < PADDLE_W; i++) {
        u8 clr = (i == 0 || i == PADDLE_W - 1)
                      ? CLR_PADDLE_EDGE : CLR_PADDLE;
        draw_tile(gx + i, gy, clr);
    }
}

static void draw_ball(void)
{
    if (!ball_attached || ((rand_seed / 100) % 6) < 4) {
        draw_tile(ball.x, ball.y, CLR_BALL);
    }
}

static void draw_bricks(void)
{
    for (int i = 0; i < MAX_BRICKS; i++) {
        if (bricks[i].alive) {
            for (int dx = 0; dx < BRICK_W; dx++) {
                draw_tile(bricks[i].x + dx, bricks[i].y,
                          bricks[i].color);
            }
        }
    }
}

static void draw_walls(void)
{
    /* top wall */
    for (int x = 0; x < GRID_W; x++) {
        gfx_put_pixel((size_t)(x * TILE_SIZE), 0, CLR_WALL);
        gfx_put_pixel((size_t)(x * TILE_SIZE), 1, CLR_WALL);
    }
    /* left wall */
    for (int y = 0; y < GRID_H; y++) {
        gfx_put_pixel(0, (size_t)(y * TILE_SIZE), CLR_WALL);
        gfx_put_pixel(1, (size_t)(y * TILE_SIZE), CLR_WALL);
    }
    /* right wall */
    for (int y = 0; y < GRID_H; y++) {
        gfx_put_pixel((size_t)(GFX_WIDTH - 1),
                      (size_t)(y * TILE_SIZE), CLR_WALL);
        gfx_put_pixel((size_t)(GFX_WIDTH - 2),
                      (size_t)(y * TILE_SIZE), CLR_WALL);
    }
}

static void draw_hud(void)
{
    char buf[48];
    memset(buf, 0, sizeof(buf));
    int len = snprintf(buf, sizeof(buf),
                       "SCORE:%-4d  LIVES:%d  BRICKS:%-2d",
                       score, player_lives, bricks_left);
    int col = (int)((GRID_W - len) / 2);
    if (col < 0) col = 0;
    gfx_write(buf, (size_t)col, 1, CLR_HUD, CLR_BG);
}

static void draw_game_over_screen(void)
{
    /* Darken screen */
    for (int y = 0; y < GFX_HEIGHT; y += 2) {
        for (int x = 0; x < GFX_WIDTH; x += 2) {
            gfx_put_pixel((size_t)x, (size_t)y, GFX_BLACK);
        }
    }

    const char* msg1 = victory ? "YOU WIN!" : "GAME OVER";
    const char* msg2 = "R:Restart  Q:Quit";
    int col1 = (int)((GRID_W - 8) / 2);
    int col2 = (int)((GRID_W - 17) / 2);

    gfx_write(msg1,
              (size_t)(col1 > 0 ? col1 : 1),
              (size_t)(GFX_ROWS / 2 - 1),
              CLR_GAMEOVER, GFX_BLACK);
    gfx_write(msg2,
              (size_t)(col2 > 0 ? col2 : 1),
              (size_t)(GFX_ROWS / 2 + 1),
              GFX_WHITE, GFX_BLACK);
}

static void draw_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particle_timers[i] > 0) {
            int px = particles[i].x;
            int py = particles[i].y;
            if (px >= 0 && px < GRID_W && py >= 0 && py < GRID_H) {
                draw_tile(px, py, particle_colors[i]);
            }
        }
    }
}

/* ===========================================================
 *  Game Logic
 * =========================================================== */

static void init_bricks(void)
{
    static const u8 row_colors[BRICK_ROWS] = CLR_BRICK_COLORS;

    int idx = 0;
    for (int row = 0; row < BRICK_ROWS; row++) {
        for (int col = 0; col < BRICK_COLS; col++) {
            bricks[idx].x = BRICK_LEFT_X
                            + col * (BRICK_W + BRICK_GAP);
            bricks[idx].y = BRICK_TOP_Y + row * (BRICK_H + BRICK_GAP);
            bricks[idx].alive = 1;
            bricks[idx].color = row_colors[row];
            idx++;
        }
    }
    bricks_left = MAX_BRICKS;
}

static void clear_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        particle_timers[i] = 0;
    }
}

static void spawn_particles(int x, int y, u8 color)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particle_timers[i] <= 0) {
            particles[i].x = x;
            particles[i].y = y;
            particle_timers[i] = 6 + (int)(my_rand() % 4);
            particle_colors[i] = color;
            break;  /* one particle per collision — don't fill every slot */
        }
    }
}

static void update_particles(void)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particle_timers[i] > 0) {
            particle_timers[i]--;
            /* drift particles downward */
            if (particle_timers[i] % 2 == 0)
                particles[i].y++;
        }
    }
}

static void reset_ball(void)
{
    ball.x = paddle.x + PADDLE_W / 2;
    ball.y = paddle.y - 1;
    ball_dx = 0;
    ball_dy = 0;
    ball_attached = 1;
}

static void move_ball(void)
{
    if (ball_attached) return;

    int new_x = ball.x + ball_dx;
    int new_y = ball.y + ball_dy;

    /* Bounce off left/right walls (tile 0 and GRID_W-1 are walls) */
    if (new_x <= 0) {
        new_x = 1;
        ball_dx = -(ball_dx);
    }
    if (new_x >= GRID_W - 1) {
        new_x = GRID_W - 2;
        ball_dx = -(ball_dx);
    }

    /* Bounce off top wall */
    if (new_y <= 1) {
        new_y = 2;
        ball_dy = -(ball_dy);
    }

    /* Fell below the screen */
    if (new_y >= GRID_H) {
        player_lives--;
        if (player_lives <= 0) {
            game_over = 1;
            victory = 0;
        } else {
            reset_ball();
        }
        return;
    }

    ball.x = new_x;
    ball.y = new_y;
}

static void check_collisions(void)
{
    if (ball_attached) return;

    /* Paddle collision */
    if (ball.y == paddle.y - 1 &&
        ball.x >= paddle.x && ball.x < paddle.x + PADDLE_W) {
        /* Angle depends on where ball hits paddle */
        int rel = ball.x - (paddle.x + PADDLE_W / 2);
        ball_dx = rel;  /* -2 .. +2 */
        if (ball_dx < -2) ball_dx = -2;
        if (ball_dx > 2) ball_dx = 2;

        /* If ball_dx is 0, give it a small nudge */
        if (ball_dx == 0) {
            ball_dx = (my_rand() % 2) ? 1 : -1;
        }

        ball_dy = -1;
        ball.y = paddle.y - 2;
        return;
    }

    /* Brick collision */
    for (int i = 0; i < MAX_BRICKS; i++) {
        if (!bricks[i].alive) continue;

        int bx = bricks[i].x;
        int by = bricks[i].y;
        int bw = bx + BRICK_W;
        int bh = by + BRICK_H;

        if (ball.x >= bx && ball.x < bw &&
            ball.y >= by && ball.y < bh) {
            bricks[i].alive = 0;
            bricks_left--;
            score += 10;

            spawn_particles(bx + BRICK_W / 2, by, bricks[i].color);

            /* Simple bounce: reverse vertical direction */
            ball_dy = -(ball_dy);
            ball.y += ball_dy;

            if (bricks_left <= 0) {
                game_over = 1;
                victory = 1;
            }
            return;
        }
    }
}

static void render_frame(void)
{
    gfx_clear(CLR_BG);
    draw_walls();
    draw_bricks();
    draw_paddle();
    draw_ball();
    draw_particles();
    draw_hud();
}

/* ===========================================================
 *  Main Game Thread
 * =========================================================== */

void breakout_thread(void)
{
    LOG("breakout_thread started");

    kb_register_callback(breakout_kb_handler);

    do {
        game_restart = 0;

        /* -------- init game state -------- */
        paddle.x = GRID_W / 2 - PADDLE_W / 2;
        paddle.y = PADDLE_Y;

        init_bricks();
        clear_particles();

        player_lives  = PLAYER_LIVES;
        score         = 0;
        game_over     = 0;
        game_quit     = 0;
        victory       = 0;
        rand_seed     = 12345;

        reset_ball();

        /* -------- main game loop -------- */
        while (!game_over) {
            move_ball();
            check_collisions();
            update_particles();
            render_frame();

            timer_delay_ms(ball_attached ? SERVE_SPEED_MS
                                         : GAME_SPEED_MS);
            thread_yield();
        }

        /* -------- game over -------- */
        render_frame();
        draw_game_over_screen();

        while (!game_quit && !game_restart) {
            timer_delay_ms(50);
            thread_yield();
        }

    } while (game_restart && !game_quit);

    LOG("breakout_thread exiting, score=%d", score);

    kb_unregister_callback(breakout_kb_handler);

    extern int game_exited_flag;
    game_exited_flag = 1;

    proc_exit(proc_get_pid());
}
