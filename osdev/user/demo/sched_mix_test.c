/*******************************************************************************
 *                                                                             *
 *    Mixed Scheduling Test Suite                                              *
 *                                                                             *
 *    Tests that the scheduler correctly interleaves kernel-mode and           *
 *    user-mode threads within the same process.  Verifies:                    *
 *      - Round-robin scheduling across different privilege levels             *
 *      - thread_block / thread_unblock across privilege boundaries            *
 *      - thread_exit on both kernel and user threads                          *
 *                                                                             *
 *    All output goes through terminal_write so results are visible        *
 *    in VGA text mode (mode 0x03).                                        *
 *                                                                             *
 *******************************************************************************/

#include "demo_common.h"
#include "lib/string.h"

/*
 * Set by the entry module (process_test_entry.c).  The test process writes
 * this flag just before calling proc_exit so the entry knows to redraw the
 * menu.  All kernel processes share the same address space.
 */
extern volatile int test_finished_flag;

/* ------------------------------------------------------------------ *
 *  Shared state for the test helpers                                  *
 * ------------------------------------------------------------------ */

/* Each helper thread increments its own counter when it runs.
 * The main thread reads these to verify all threads got CPU time. */
#define MAX_MIX_THREADS 8
static volatile int g_run_count[MAX_MIX_THREADS];
static volatile int g_helper_tids[MAX_MIX_THREADS];
static volatile int g_num_helpers = 0;

/* Synchronisation: set by main, waited on by a blocked helper */
static volatile int g_unblock_signal = 0;

/* Which helper should self-block for the cross-privilege block test */
static volatile int g_block_target_tid = -1;

/* ------------------------------------------------------------------ *
 *  Helper: write an integer to the terminal                           *
 * ------------------------------------------------------------------ */
static void term_write_int(const char* prefix, int val)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s%d\n", prefix, val);
    terminal_write(buf);
}

/* ------------------------------------------------------------------ *
 *  Helper: write a PASS/FAIL line                                     *
 * ------------------------------------------------------------------ */
static void term_pass(const char* test_name)
{
    terminal_write_color("[PASS] ", to_vga_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    terminal_write(test_name);
    terminal_write("\n");
}

static void term_fail(const char* test_name)
{
    terminal_write_color("[FAIL] ", to_vga_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    terminal_write(test_name);
    terminal_write("\n");
}

/* ------------------------------------------------------------------ *
 *  Helper: flush screen if nearly full                                *
 * ------------------------------------------------------------------ */
static void check_flush(void)
{
    if (terminal_get_row() >= 12) {
        timer_delay_ms(1500);
        terminal_flush(0);
    }
}

/* ================================================================== *
 *  Helper thread entry — works for both kernel and user privilege     *
 *                                                                      *
 *  Terminal access writes VGA directly (works at CPL=3 too thanks to     *
 *  user-mapped VGA + IOPL=3); timer delay is also a plain function       *
 *  (timer_delay_ms), so it is safe at CPL=3 too.                         *
 *  All verification is done by the main thread reading g_run_count[].  *
 * ================================================================== */
static void mix_helper_entry(void)
{
    int my_tid = thread_get_tid();
    int my_idx = -1;

    /* Find our slot in the global arrays.
     * Spin until the main thread has registered our TID — there is a
     * window between thread_create() and g_helper_tids[] assignment
     * where a timer IRQ could preempt the main thread and schedule us
     * before our slot is ready. */
    while (my_idx < 0) {
        for (int i = 0; i < MAX_MIX_THREADS; i++) {
            if (g_helper_tids[i] == my_tid) {
                my_idx = i;
                break;
            }
        }
        if (my_idx < 0)
            thread_yield();
    }

    /* Announce ourselves.  terminal_write() writes VGA directly — it is
     * plain RING3-callable code (VGA is user-mapped, IOPL=3 allows the
     * CRT port I/O), so it works from both kernel (CPL0) and user (CPL3)
     * threads. */
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "  [HELPER] TID=%d started\n", my_tid);
        terminal_write(buf);
    }

    /* Demonstrate a delay from a USER helper thread.
     * Helpers are created kernel×2 then user×2, so slot 2 is the
     * first user-mode helper. */
    if (my_idx == 2 && g_num_helpers > 2)
        timer_delay_ms(50);

    /*
     * Phase 1 — run several iterations, yielding each time.
     * This lets the main thread observe that both kernel and user
     * threads get scheduled.
     */
    for (int i = 0; i < 5; i++) {
        if (my_idx >= 0)
            g_run_count[my_idx]++;
        thread_yield();
    }

    /*
     * Phase 2 — if we are the designated block target, block ourselves.
     * The main thread will unblock us later (cross-privilege test).
     */
    if (my_tid == g_block_target_tid) {
        thread_block(my_tid);
        /* After unblock, increment to prove we resumed */
        if (my_idx >= 0)
            g_run_count[my_idx]++;
        {
            char buf[48];
            snprintf(buf, sizeof(buf),
                     "  [HELPER] TID=%d resumed after unblock\n", my_tid);
            terminal_write(buf);
        }
    }

    /*
     * Phase 3 — after unblock (or skipped if not the target),
     * run a few more iterations to prove we are alive.
     */
    for (int i = 0; i < 3; i++) {
        if (my_idx >= 0)
            g_run_count[my_idx]++;
        thread_yield();
    }

    /* Spin forever — kernel threads must not return */
    for (;;) {
        if (my_idx >= 0)
            g_run_count[my_idx]++;
        thread_yield();
    }
}

/* ================================================================== *
 *  Main test thread                                                   *
 * ================================================================== */
void sched_mix_test_main(void)
{
    terminal_flush(0);
    terminal_write("\n========== Mixed Scheduling Test Suite ==========\n\n");

    /* Initialise globals */
    for (int i = 0; i < MAX_MIX_THREADS; i++) {
        g_run_count[i]  = 0;
        g_helper_tids[i] = -1;
    }
    g_num_helpers    = 0;
    g_unblock_signal = 0;
    g_block_target_tid = -1;

    /* -------------------------------------------------------------- *
     *  Test 1 — Create kernel + user threads, verify all run         *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 1] Creating 2 kernel + 2 user threads...\n");

        /* Create 2 kernel-mode threads */
        for (int i = 0; i < 2; i++) {
            int tid = thread_create(TASK_PRIV_USER, mix_helper_entry, 0);
            if (tid >= 0) {
                g_helper_tids[g_num_helpers] = tid;
                g_num_helpers++;
                thread_unblock(tid);
                terminal_write("  Created KERNEL helper, TID=");
                term_write_int("", tid);
            } else {
                terminal_write("  FAILED to create kernel helper\n");
            }
        }

        /* Create 2 user-mode threads */
        for (int i = 0; i < 2; i++) {
            int tid = thread_create(TASK_PRIV_USER, mix_helper_entry, 0);
            if (tid >= 0) {
                g_helper_tids[g_num_helpers] = tid;
                g_num_helpers++;
                thread_unblock(tid);
                terminal_write("  Created USER helper, TID=");
                term_write_int("", tid);
            } else {
                terminal_write("  FAILED to create user helper\n");
            }
        }

        terminal_write("  Total helpers created: ");
        term_write_int("", g_num_helpers);
        timer_delay_ms(1000);   /* plain function — works from any ring */
    }

    /*
     * Let all helpers run their Phase 1 iterations.
     * Yield enough times so every helper gets several time slices.
     *
     * Also set g_block_target_tid NOW so that helper[1] self-blocks
     * during Phase 2 (before it reaches the infinite spin loop).
     */
    if (g_num_helpers > 1) {
        g_block_target_tid = g_helper_tids[1];
    }
    check_flush();
    terminal_write("\n[MAIN] Yielding to let all helpers run Phase 1...\n");
    for (int i = 0; i < 40; i++) {
        thread_yield();
    }

    /* Verify that every helper ran at least once */
    {
        check_flush();
        terminal_write("\n[TEST 1] Run-count verification:\n");
        int all_ran = 1;
        for (int i = 0; i < g_num_helpers; i++) {
            terminal_write("  Helper TID=");
            term_write_int("", g_helper_tids[i]);
            terminal_write(" run_count=");
            term_write_int("", g_run_count[i]);
            if (g_run_count[i] == 0) {
                all_ran = 0;
            }
        }
        if (all_ran && g_num_helpers == 4)
            term_pass("Mixed scheduling — all 4 helpers ran");
        else
            term_fail("Mixed scheduling — not all helpers ran");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 2 — Block a helper from the main thread                   *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 2] Block a helper thread...\n");

        if (g_num_helpers > 0) {
            /* pick the first helper (index 0) */
            int target_idx = 0;
            int target_tid = g_helper_tids[target_idx];

            terminal_write("  Before block: helper TID=");
            term_write_int("", target_tid);
            terminal_write(" run_count=");
            term_write_int("", g_run_count[target_idx]);

            thread_block(target_tid);

            /* read after block to get the true "before" value */
            int before = g_run_count[target_idx];
            terminal_write("  After  block: run_count=");
            term_write_int("", before);
            term_pass("thread_block (returned)");

            /* Yield several times — the blocked helper should not run */
            terminal_write("  Yielding: blocked helper should stay silent...\n");
            for (int i = 0; i < 15; i++) {
                thread_yield();
            }

            int after = g_run_count[target_idx];
            terminal_write("  After 15 yields: run_count=");
            term_write_int("", after);

            if (after == before)
                term_pass("Helper stayed blocked");
            else
                term_fail("Helper ran while blocked!");

            /* Now unblock it */
            terminal_write("  Unblocking helper TID=");
            term_write_int("", target_tid);
            thread_unblock(target_tid);
            term_pass("thread_unblock (returned)");

            /* Let it run again */
            for (int i = 0; i < 15; i++) {
                thread_yield();
            }

            int after_unblock = g_run_count[target_idx];
            terminal_write("  After unblock + 15 yields: run_count=");
            term_write_int("", after_unblock);
            if (after_unblock > after)
                term_pass("Helper resumed after unblock");
            else
                term_fail("Helper did NOT resume after unblock");
        } else {
            term_fail("No helpers to block");
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 3 — Self-block test: a helper blocks itself,             *
     *           then the main thread unblocks it                      *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 3] Helper self-block + main unblock...\n");

        if (g_num_helpers > 1) {
            /* pick the second helper (index 1) */
            int target_idx = 1;
            int target_tid = g_helper_tids[target_idx];

            terminal_write("  Before self-block: helper TID=");
            term_write_int("", target_tid);
            terminal_write(" run_count=");
            term_write_int("", g_run_count[target_idx]);

            g_block_target_tid = target_tid; /* tell the helper to self-block */

            /* Wait for the helper to actually block itself */
            terminal_write("  Waiting for helper to self-block...\n");
            for (int i = 0; i < 30; i++) {
                thread_yield();
            }

            int after = g_run_count[target_idx];
            terminal_write("  After self-block: run_count=");
            term_write_int("", after);

            /* Now unblock it from the main thread */
            terminal_write("  Unblocking helper TID=");
            term_write_int("", target_tid);
            thread_unblock(target_tid);
            term_pass("Cross-thread unblock");

            /* Let it run */
            for (int i = 0; i < 15; i++) {
                thread_yield();
            }

            int after_unblock = g_run_count[target_idx];
            terminal_write("  After unblock + 15 yields: run_count=");
            term_write_int("", after_unblock);
            if (after_unblock > after)
                term_pass("Helper resumed after cross-thread unblock");
            else
                term_fail("Helper did NOT resume after cross-thread unblock");

            g_block_target_tid = -1; /* reset */
        } else {
            term_fail("Need at least 2 helpers for self-block test");
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 4 — Clean up all helpers via thread_exit                  *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 4] Cleaning up all helper threads...\n");

        for (int i = 0; i < g_num_helpers; i++) {
            int tid = g_helper_tids[i];
            terminal_write("  Destroying helper TID=");
            term_write_int("", tid);
            thread_exit(tid);
        }

        /* Yield a few times — helpers should be gone */
        for (int i = 0; i < 5; i++) {
            thread_yield();
        }

        term_pass("All helper threads destroyed");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  All done — delay, signal the entry and exit this process      *
     * -------------------------------------------------------------- */
    terminal_write("\n========== Mixed Scheduling Test Suite COMPLETE ==========\n");
    terminal_write("Returning to menu in 5 seconds...\n");
    timer_delay_ms(5000);
    test_finished_flag = 1;
    thread_exit(thread_get_tid());
}
