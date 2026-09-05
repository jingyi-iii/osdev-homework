/*******************************************************************************
 *                                                                             *
 *    Process Test Entry — user-mode ELF test menu (keyboard-driven)           *
 *                                                                             *
 *    Runs as a standalone ring-3 ELF.  Prints through the console portal      *
 *    (terminal_server) and reads key presses from the kb_server's             *
 *    MSG_KEY_EVENT mailbox broadcast.  Test suites run as THREADS of this     *
 *    process, so they share this process's address space (globals such as     *
 *    test_finished_flag work) and their output reaches the same console.      *
 *                                                                             *
 *******************************************************************************/

#include "demo_common.h"
#include "../server/server_msgs.h"

/* Test entry points (defined in demo/) */
extern void thread_api_test_main(void);
extern void process_api_test_main(void);
extern void rbtree_test_main(void);
extern void sched_mix_test_main(void);

/* Set by a test suite just before it exits; the menu polls it. */
volatile int test_finished_flag = 0;

/* Menu state: -1 = waiting, 0..9 = selected option ('0' = graphics demo —
 * a valid choice, so the "no key yet" sentinel is -1, not 0). */
static volatile int menu_choice = -1;

/* ------------------------------------------------------------------ *
 *  Key listener: kb_server broadcasts MSG_KEY_EVENT mails to every    *
 *  subscribed mailbox; '0' = graphics demo, '1'..'9' select suites.   *
 * ------------------------------------------------------------------ */
static void key_listener_thread(void)
{
    if (user_mail_subscribe(MSG_KEY_EVENT) != 0) {
        /* No CAP_IPC or no mailbox — nothing to listen on. */
        for (;;)
            user_yield();
    }

    for (;;) {
        user_mail* m = (user_mail*)user_mail_listen();
        if (m && m->magic == MSG_KEY_EVENT) {
            key_event ev;
            for (u32 i = 0; i < sizeof(ev); i++)
                ((u8*)&ev)[i] = ((const u8*)m->data)[i];

            if (ev.pressed && ev.ascii >= '0' && ev.ascii <= '9')
                menu_choice = ev.ascii - '0';
        }
        if (m)
            user_mail_release(m);
    }
}

/* ------------------------------------------------------------------ *
 *  Draw the text-mode menu                                            *
 * ------------------------------------------------------------------ */
static void draw_menu(void)
{
    /* The whole menu (clear + every line) is assembled into ONE buffer
     * and sent as a single portal call.  The terminal server renders a
     * request atomically, so concurrent writers (boot demos etc.) can
     * never splice their text between menu rows — with per-line calls
     * they could, because every portal RPC costs a few scheduler ticks
     * and other processes write in between. */
    static const char* lines[] = {
        "========================================",
        "  PROCESS API TEST SUITE                ",
        "========================================",
        "",
        "  [0] Graphics Demo (mode 0x13)         ",
        "  [1] Thread API Test Suite",
        "  [2] Process API Test Suite",
        "  [3] Mailbox API Test Suite            (not yet ported)",
        "  [4] Red-Black Tree Test Suite",
        "  [5] Mixed Scheduling Test Suite",
        "  [6] Run All Test Suites",
        "  [7] SHM Test Suite                    (not yet ported)",
        "  [8] SHM Stress Test                   (not yet ported)",
        "  [9] Portal RPC Test Suite             (not yet ported)",
        "",
        "  Press 0, 1, 2, 3, 4, 5, 6, 7, 8 or 9 to select",
    };
    static char buf[640];
    int o = 0;
    const char* p;

    /* Leading ESC[2J: clear the screen, then the text fills it from the
     * top — all inside the one request. */
    p = "\x1b[2J";
    while (*p)
        buf[o++] = *p++;

    for (u32 i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        for (p = lines[i]; *p; p++)
            buf[o++] = *p;
        buf[o++] = '\n';
    }
    buf[o] = 0;

    console_putstr(buf);
}

static void note_not_ported(const char* name)
{
    terminal_write("\n*** ");
    terminal_write(name);
    terminal_write(" still uses kernel-only APIs (mailbox handlers / shm /\n");
    terminal_write("    kernel portal internals) — not yet ported to the user ELF. ***\n");
    timer_delay_ms(2000);
}

/* ------------------------------------------------------------------ *
 *  Graphics demo — switch the whole screen to mode 0x13, animate a    *
 *  bouncing square in the shared frame buffer, then back to text.     *
 *  Pacing comes from the rtc server (timer_delay_ms = SLEEP_MS RPC).  *
 * ------------------------------------------------------------------ */
static void graphics_demo(void)
{
    int x = 0, y = 0, dx = 2, dy = 1;
    int w = 24, h = 24;
    int cx = 160 - w / 2, cy = 100 - h / 2;   /* target centre */
    u8 bg = 1;                                 /* blue background  */

    gfx_set_graphics_mode();

    for (int frame = 0; frame < 250; frame++) {
        gfx_clear_screen(bg);
        gfx_fill_rect(cx + x, cy + y, w, h, 14);      /* yellow block */
        gfx_fill_rect(10, 10, 60, 8, 2);              /* green banner */
        gfx_fill_rect(10, 182, 300, 8, 4);            /* red footer   */
        gfx_flush();

        x += dx;
        y += dy;
        if (x <= -cx || x + w >= 320 - cx)
            dx = -dx;
        if (y <= -cy || y + h >= 200 - cy)
            dy = -dy;
        if ((frame & 3) == 0)
            bg = (u8)((bg + 1) & 0x0F);
        timer_delay_ms(20);
    }

    gfx_set_text_mode();
    terminal_flush(0);
    terminal_write("\n*** Graphics demo finished — back in text mode. ***\n");
}

/* ------------------------------------------------------------------ *
 *  Launch one test suite as a THREAD of this process and wait for it  *
 * ------------------------------------------------------------------ */
static void run_test_suite(const char* name, task_entry_t entry)
{
    terminal_write("\n--- Launching ");
    terminal_write(name);
    terminal_write(" (USER) ---\n\n");

    test_finished_flag = 0;
    int tid = user_thread_create(TASK_PRIV_USER, (void*)entry, 0);
    if (tid >= 0)
        user_thread_unblock(tid);

    /* Wait for the suite to finish: it sets test_finished_flag just
     * before deleting itself. */
    while (!test_finished_flag)
        user_yield();

    terminal_write("\n*** ");
    terminal_write(name);
    terminal_write(" finished. ***\n");
    timer_delay_ms(800);
}

/* ------------------------------------------------------------------ *
 *  Run every ported suite once, in order                              *
 * ------------------------------------------------------------------ */
static void run_all_test_suites(void)
{
    terminal_write("\n========== RUNNING ALL TEST SUITES ==========\n\n");

    run_test_suite("Thread API Test Suite",      thread_api_test_main);
    run_test_suite("Process API Test Suite",     process_api_test_main);
    run_test_suite("Red-Black Tree Test Suite",  rbtree_test_main);
    run_test_suite("Mixed Scheduling Test Suite", sched_mix_test_main);

    terminal_write("\n========== ALL TEST SUITES COMPLETE ==========\n\n");
}

/* ------------------------------------------------------------------ *
 *  Main entry thread — loops forever showing the menu                 *
 * ------------------------------------------------------------------ */
void _start(void)
{
    int tid = user_thread_create(TASK_PRIV_USER, (void*)key_listener_thread, 0);
    if (tid >= 0)
        user_thread_unblock(tid);

    for (;;) {
        menu_choice = -1;
        test_finished_flag = 0;

        draw_menu();

        /* Wait for the user to choose an option */
        while (menu_choice < 0)
            user_yield();

        switch (menu_choice) {
        case 0:
            graphics_demo();
            break;
        case 1:
            run_test_suite("Thread API Test Suite", thread_api_test_main);
            break;
        case 2:
            run_test_suite("Process API Test Suite", process_api_test_main);
            break;
        case 3:
            note_not_ported("Mailbox API Test Suite");
            break;
        case 4:
            run_test_suite("Red-Black Tree Test Suite", rbtree_test_main);
            break;
        case 5:
            run_test_suite("Mixed Scheduling Test Suite", sched_mix_test_main);
            break;
        case 6:
            run_all_test_suites();
            break;
        case 7:
            note_not_ported("SHM Test Suite");
            break;
        case 8:
            note_not_ported("SHM Stress Test");
            break;
        case 9:
            note_not_ported("Portal RPC Test Suite");
            break;
        default:
            break;
        }

        terminal_write("\n*** Returning to menu... ***\n");
    }
}
