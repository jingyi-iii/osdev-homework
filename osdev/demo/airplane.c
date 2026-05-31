/*******************************************************************************
 *                                                                             *
 *    Airplane Battle — A classic shoot-'em-up running as a kernel process.    *
 *                       Rendered via graphics_driver (VGA mode 0x13),         *
 *                       controlled via kb_driver keyboard callbacks.          *
 *                                                                             *
 *    Controls:  W/A/S/D or Arrow Keys — move plane                            *
 *               Space                — fire bullet                            *
 *               R                    — restart after game over                *
 *               Q                    — quit the game                          *
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

#define TILE_SIZE          8
#define GRID_W             (GFX_WIDTH  / TILE_SIZE)   /* 40 */
#define GRID_H             (GFX_HEIGHT / TILE_SIZE)   /* 25 */
#define PLANE_TILE_W       3
#define PLANE_TILE_H       2
#define MAX_BULLETS        30
#define MAX_ENEMIES        10
#define PLAYER_LIVES       3
#define GAME_SPEED_MS      50

/* Movement speeds (in game ticks) */
#define PLAYER_MOVE_TICKS  2
#define BULLET_MOVE_TICKS  1
#define ENEMY_MOVE_TICKS   6
#define ENEMY_SPAWN_TICKS  35
#define PLAYER_SHOOT_TICKS 5
#define ENEMY_SHOOT_TICKS  15

/* Colors */
#define CLR_BG              GFX_BLACK
#define CLR_STAR            GFX_DARK_GREY
#define CLR_PLAYER_COCKPIT  GFX_YELLOW
#define CLR_PLAYER_BODY     GFX_LIGHT_GREEN
#define CLR_PLAYER_WING     GFX_LIGHT_CYAN
#define CLR_ENEMY_BODY      GFX_LIGHT_RED
#define CLR_ENEMY_COCKPIT   GFX_LIGHT_MAGENTA
#define CLR_ENEMY_WING      GFX_DARK_GREY
#define CLR_BULLET_P        GFX_WHITE
#define CLR_BULLET_E        GFX_LIGHT_RED
#define CLR_EXPLOSION       GFX_YELLOW
#define CLR_HUD             GFX_WHITE
#define CLR_GAMEOVER        GFX_YELLOW

/* ===========================================================
 *  Direction
 * =========================================================== */

typedef enum {
    DIR_NONE  = -1,
    DIR_UP    = 0,
    DIR_DOWN  = 1,
    DIR_LEFT  = 2,
    DIR_RIGHT = 3,
} direction_t;

/* ===========================================================
 *  Data Structures
 * =========================================================== */

typedef struct {
    int x, y;
} point_t;

typedef struct {
    int x, y;
    int active;
    int is_player;
} bullet_t;

typedef struct {
    int x, y;
    int active;
    int shoot_timer;
} enemy_t;

typedef struct {
    int dx, dy;
    uint8_t color;
} plane_tile_t;

/* ===========================================================
 *  Plane Sprites (tile offsets relative to plane origin)
 *
 *  Player (3x2):          Enemy (3x2):
 *    [ ] [▲] [ ]            [▶] [█] [◀]
 *    [▶] [█] [◀]            [ ] [▼] [ ]
 * =========================================================== */

static const plane_tile_t player_sprite[] = {
    { 1, 0, CLR_PLAYER_COCKPIT },
    { 0, 1, CLR_PLAYER_WING },
    { 1, 1, CLR_PLAYER_BODY },
    { 2, 1, CLR_PLAYER_WING },
};
#define PLAYER_SPRITE_LEN \
    (sizeof(player_sprite) / sizeof(player_sprite[0]))

static const plane_tile_t enemy_sprite[] = {
    { 0, 0, CLR_ENEMY_WING },
    { 1, 0, CLR_ENEMY_BODY },
    { 2, 0, CLR_ENEMY_WING },
    { 1, 1, CLR_ENEMY_COCKPIT },
};
#define ENEMY_SPRITE_LEN \
    (sizeof(enemy_sprite) / sizeof(enemy_sprite[0]))

/* ===========================================================
 *  Game State
 * =========================================================== */

static point_t      player;
static direction_t  player_dir;
static int          player_shoot_cooldown;
static int          player_lives;
static int          player_invincible;
static int          invincible_timer;

static bullet_t     bullets[MAX_BULLETS];
static enemy_t      enemies[MAX_ENEMIES];

static int          game_over;
static int          game_quit;
static int          game_restart;
static int          score;
static int          tick_count;
static int          enemy_spawn_timer;
static int          explosion_timer;
static point_t      explosion_pos;

/* Starfield */
#define MAX_STARS   40
static point_t      stars[MAX_STARS];

/* ===========================================================
 *  Forward Declarations
 * =========================================================== */

static void draw_plane(int gx, int gy,
                       const plane_tile_t* sprite, size_t len);
static void draw_bullets(void);
static void draw_hud(void);
static void draw_game_over_screen(void);
static void draw_stars(void);
static void draw_explosion(void);
static void spawn_enemy(void);
static void fire_bullet(int x, int y, int is_player);
static void move_bullets(void);
static void move_enemies(void);
static void move_player(void);
static void enemy_shoot(void);
static void check_collisions(void);
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

static void airplane_kb_handler(const char* data, size_t size)
{
    (void)size;
    if (!data) return;

    char key = data[0];

    switch (key) {
    case 'w': case 'W': case UP:
        player_dir = DIR_UP;
        break;
    case 's': case 'S': case DOWN:
        player_dir = DIR_DOWN;
        break;
    case 'a': case 'A': case LEFT:
        player_dir = DIR_LEFT;
        break;
    case 'd': case 'D': case RIGHT:
        player_dir = DIR_RIGHT;
        break;
    case ' ':
        if (player_shoot_cooldown <= 0 && !game_over) {
            fire_bullet(player.x + PLANE_TILE_W / 2,
                        player.y - 1, 1);
            player_shoot_cooldown = PLAYER_SHOOT_TICKS;
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

static void draw_tile(int gx, int gy, uint8_t color)
{
    gfx_fill_rect(
        (size_t)(gx * TILE_SIZE),
        (size_t)(gy * TILE_SIZE),
        TILE_SIZE, TILE_SIZE, color);
}

static void draw_plane(int gx, int gy,
                       const plane_tile_t* sprite, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        draw_tile(gx + sprite[i].dx, gy + sprite[i].dy,
                  sprite[i].color);
    }
}

static void draw_bullets(void)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].active) {
            uint8_t clr = bullets[i].is_player
                          ? CLR_BULLET_P : CLR_BULLET_E;
            draw_tile(bullets[i].x, bullets[i].y, clr);
        }
    }
}

static void draw_stars(void)
{
    for (int i = 0; i < MAX_STARS; i++) {
        if ((my_rand() % 10) < 7) {
            gfx_put_pixel(
                (size_t)(stars[i].x * TILE_SIZE
                         + (int)(my_rand() % TILE_SIZE)),
                (size_t)(stars[i].y * TILE_SIZE
                         + (int)(my_rand() % TILE_SIZE)),
                CLR_STAR);
        }
    }
}

static void draw_hud(void)
{
    char buf[48];
    memset(buf, 0, sizeof(buf));
    int len = snprintf(buf, sizeof(buf),
                       "SCORE:%d  LIVES:%d", score, player_lives);
    int col = (int)((GRID_W - len) / 2);
    if (col < 0) col = 0;
    gfx_write(buf, (size_t)col, 0, CLR_HUD, CLR_BG);
}

static void draw_game_over_screen(void)
{
    /* Darken screen */
    for (int y = 0; y < GFX_HEIGHT; y += 2) {
        for (int x = 0; x < GFX_WIDTH; x += 2) {
            gfx_put_pixel((size_t)x, (size_t)y, GFX_BLACK);
        }
    }

    const char* msg1 = "GAME OVER";
    const char* msg2 = "R:Restart  Q:Quit";
    int col1 = (int)((GRID_W - 9) / 2);
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

static void draw_explosion(void)
{
    if (explosion_timer <= 0) return;

    int radius = 4 - explosion_timer / 2;
    if (radius < 1) radius = 1;
    if (radius > 3) radius = 3;

    int cx = explosion_pos.x * TILE_SIZE + TILE_SIZE / 2;
    int cy = explosion_pos.y * TILE_SIZE + TILE_SIZE / 2;

    for (int dy = -radius * 3; dy <= radius * 3; dy += 2) {
        for (int dx = -radius * 3; dx <= radius * 3; dx += 2) {
            if (dx * dx + dy * dy <= (radius * 3) * (radius * 3)) {
                int px = cx + dx;
                int py = cy + dy;
                if (px >= 0 && (size_t)px < GFX_WIDTH &&
                    py >= 0 && (size_t)py < GFX_HEIGHT) {
                    uint8_t clr = (my_rand() % 2)
                                  ? CLR_EXPLOSION : GFX_RED;
                    gfx_fill_rect((size_t)px, (size_t)py,
                                  2, 2, clr);
                }
            }
        }
    }
}

/* ===========================================================
 *  Game Logic
 * =========================================================== */

static void fire_bullet(int x, int y, int is_player)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            bullets[i].x = x;
            bullets[i].y = y;
            bullets[i].active = 1;
            bullets[i].is_player = is_player;
            return;
        }
    }
}

static void move_bullets(void)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;

        if (bullets[i].is_player) {
            bullets[i].y--;
        } else {
            bullets[i].y++;
        }

        if (bullets[i].y < 0 || bullets[i].y >= GRID_H ||
            bullets[i].x < 0 || bullets[i].x >= GRID_W) {
            bullets[i].active = 0;
        }
    }
}

static void move_player(void)
{
    if (player_dir == DIR_NONE) return;

    int nx = player.x;
    int ny = player.y;

    switch (player_dir) {
    case DIR_UP:    ny--; break;
    case DIR_DOWN:  ny++; break;
    case DIR_LEFT:  nx--; break;
    case DIR_RIGHT: nx++; break;
    default: return;
    }

    /* Clamp to playable area */
    if (nx < 1) nx = 1;
    if (nx > GRID_W - 1 - PLANE_TILE_W)
        nx = GRID_W - 1 - PLANE_TILE_W;
    if (ny < GRID_H / 3) ny = GRID_H / 3;
    if (ny > GRID_H - 1 - PLANE_TILE_H)
        ny = GRID_H - 1 - PLANE_TILE_H;

    player.x = nx;
    player.y = ny;
}

static void move_enemies(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;

        enemies[i].y++;

        if (enemies[i].y >= GRID_H) {
            enemies[i].active = 0;
            if (!game_over) {
                player_lives--;
                if (player_lives <= 0) {
                    game_over = 1;
                }
            }
        }
    }
}

static void spawn_enemy(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].x = (int)(my_rand() % (GRID_W
                                - PLANE_TILE_W - 2)) + 1;
            enemies[i].y = -PLANE_TILE_H;
            enemies[i].active = 1;
            enemies[i].shoot_timer =
                (int)(my_rand() % ENEMY_SHOOT_TICKS);
            return;
        }
    }
}

static void trigger_explosion(int x, int y)
{
    explosion_pos.x = x;
    explosion_pos.y = y;
    explosion_timer = 8;
}

static void check_collisions(void)
{
    /* Player bullets vs enemies */
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!bullets[b].active || !bullets[b].is_player)
            continue;

        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (!enemies[e].active) continue;

            if (bullets[b].x >= enemies[e].x &&
                bullets[b].x < enemies[e].x + PLANE_TILE_W &&
                bullets[b].y >= enemies[e].y &&
                bullets[b].y < enemies[e].y + PLANE_TILE_H) {
                bullets[b].active = 0;
                enemies[e].active = 0;
                score += 10;
                trigger_explosion(
                    enemies[e].x + PLANE_TILE_W / 2,
                    enemies[e].y + PLANE_TILE_H / 2);
                break;
            }
        }
    }

    if (player_invincible) return;

    /* Enemy bullets vs player */
    for (int b = 0; b < MAX_BULLETS; b++) {
        if (!bullets[b].active || bullets[b].is_player)
            continue;

        if (bullets[b].x >= player.x &&
            bullets[b].x < player.x + PLANE_TILE_W &&
            bullets[b].y >= player.y &&
            bullets[b].y < player.y + PLANE_TILE_H) {
            bullets[b].active = 0;
            player_lives--;
            trigger_explosion(
                player.x + PLANE_TILE_W / 2,
                player.y + PLANE_TILE_H / 2);
            if (player_lives <= 0) {
                game_over = 1;
            } else {
                player_invincible = 1;
                invincible_timer = 30;
            }
            return;
        }
    }

    /* Enemy body vs player */
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!enemies[e].active) continue;

        if (player.x < enemies[e].x + PLANE_TILE_W &&
            player.x + PLANE_TILE_W > enemies[e].x &&
            player.y < enemies[e].y + PLANE_TILE_H &&
            player.y + PLANE_TILE_H > enemies[e].y) {
            enemies[e].active = 0;
            player_lives--;
            trigger_explosion(
                enemies[e].x + PLANE_TILE_W / 2,
                enemies[e].y + PLANE_TILE_H / 2);
            if (player_lives <= 0) {
                game_over = 1;
            } else {
                player_invincible = 1;
                invincible_timer = 30;
            }
            return;
        }
    }
}

static void enemy_shoot(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;

        if (enemies[i].shoot_timer <= 0) {
            fire_bullet(enemies[i].x + PLANE_TILE_W / 2,
                        enemies[i].y + PLANE_TILE_H, 0);
            enemies[i].shoot_timer =
                ENEMY_SHOOT_TICKS + (int)(my_rand() % 10);
        } else {
            enemies[i].shoot_timer--;
        }
    }
}

static void render_frame(void)
{
    gfx_clear(CLR_BG);
    draw_stars();

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            draw_plane(enemies[i].x, enemies[i].y,
                       enemy_sprite, ENEMY_SPRITE_LEN);
        }
    }

    /* Blink player when invincible */
    if (!player_invincible || (tick_count % 4) < 2) {
        draw_plane(player.x, player.y,
                   player_sprite, PLAYER_SPRITE_LEN);
    }

    draw_bullets();
    draw_explosion();
    draw_hud();
}

/* ===========================================================
 *  Initialization
 * =========================================================== */

static void init_stars(void)
{
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x = (int)(my_rand() % GRID_W);
        stars[i].y = (int)(my_rand() % GRID_H);
    }
}

static void clear_bullets(void)
{
    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = 0;
    }
}

static void clear_enemies(void)
{
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;
    }
}

/* ===========================================================
 *  Main Game Thread
 * =========================================================== */

void airplane_thread(void)
{
    KLOG("airplane_thread started");

    kb_register_callback(airplane_kb_handler);

    do {
        game_restart = 0;

        /* -------- init game state -------- */
        player.x = GRID_W / 2 - PLANE_TILE_W / 2;
        player.y = GRID_H - 4;
        player_dir = DIR_NONE;
        player_shoot_cooldown = 0;
        player_lives = PLAYER_LIVES;
        player_invincible = 0;
        invincible_timer = 0;

        clear_bullets();
        clear_enemies();

        game_over  = 0;
        game_quit  = 0;
        score      = 0;
        tick_count = 0;
        enemy_spawn_timer = 0;
        explosion_timer   = 0;

        rand_seed = 12345;
        init_stars();

        /* -------- main game loop -------- */
        while (!game_over) {
            tick_count++;

            if (player_shoot_cooldown > 0)
                player_shoot_cooldown--;
            if (invincible_timer > 0) {
                invincible_timer--;
                if (invincible_timer <= 0)
                    player_invincible = 0;
            }
            if (explosion_timer > 0)
                explosion_timer--;

            if (tick_count % PLAYER_MOVE_TICKS == 0)
                move_player();
            if (tick_count % BULLET_MOVE_TICKS == 0)
                move_bullets();
            if (tick_count % ENEMY_MOVE_TICKS == 0)
                move_enemies();

            enemy_spawn_timer++;
            if (enemy_spawn_timer >= ENEMY_SPAWN_TICKS) {
                enemy_spawn_timer = 0;
                spawn_enemy();
            }

            enemy_shoot();
            check_collisions();
            render_frame();

            timer_delay_ms(GAME_SPEED_MS);
            thread_yield();
        }

        /* -------- game over -------- */
        draw_game_over_screen();

        while (!game_quit && !game_restart) {
            timer_delay_ms(50);
            thread_yield();
        }

    } while (game_restart && !game_quit);

    KLOG("airplane_thread exiting, score=%d", score);

    kb_unregister_callback(airplane_kb_handler);

    extern int game_exited_flag;
    game_exited_flag = 1;

    proc_exit(proc_get_pid());
}
