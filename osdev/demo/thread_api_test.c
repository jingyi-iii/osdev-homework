/*******************************************************************************
 *                                                                             *
 *    Thread API Test Suite                                                    *
 *                                                                             *
 *    Tests the following process.c thread-level interfaces:                   *
 *      - proc_get_pid()      get current process ID                           *
 *      - thread_create()     spawn a helper thread                            *
 *      - thread_yield()      cooperative CPU yield                            *
 *      - thread_block()      block a thread by TID                            *
 *      - thread_unblock()    unblock a previously blocked thread              *
 *      - thread_exit()       destroy a thread                                 *
 *                                                                             *
 *    All output goes through terminal_write so results are visible in         *
 *    VGA text mode (mode 0x03).                                               *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_server.h"
#include "drivers/timer_server.h"
#include "drivers/log_server.h"
#include "kernel/process.h"
#include "lib/string.h"

/*
 * Set by the entry module (process_test_entry.c).  The test process writes
 * this flag just before calling proc_exit so the entry knows to redraw the
 * menu.  All kernel processes share the same address space.
 */
extern volatile int test_finished_flag;

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

/* ------------------------------------------------------------------ *
 *  Helper thread — created by the main test thread                    *
 * ------------------------------------------------------------------ */
static void helper_thread_entry(void)
{
    terminal_write("[HELPER] Started.\n");

    /* Do some work, yielding cooperatively */
    for (int i = 0; i < 3; i++) {
        terminal_write("[HELPER] Working...\n");
        thread_yield();
    }

    terminal_write("[HELPER] Finished, waiting to be destroyed.\n");
    /*
     * Spin forever — a kernel thread MUST NOT return from its entry
     * function (there is no valid return address on the stack).
     * The main test thread will destroy us via thread_exit().
     */
    for (;;) {
        thread_yield();
    }
}

/* ------------------------------------------------------------------ *
 *  Main test thread                                                   *
 * ------------------------------------------------------------------ */
void thread_api_test_main(void)
{
    terminal_flush(0);
    terminal_write("\n========== Thread API Test Suite ==========\n\n");

    /* -------------------------------------------------------------- *
     *  Test 1 — proc_get_pid                                         *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        int pid = proc_get_pid();
        terminal_write("[TEST 1] proc_get_pid() => ");
        term_write_int("", pid);
        if (pid >= 0)
            term_pass("proc_get_pid");
        else
            term_fail("proc_get_pid");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 2 — thread_yield (basic cooperative scheduling)          *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 2] thread_yield() — yielding CPU...\n");
        thread_yield();
        terminal_write("[TEST 2] Returned from thread_yield()\n");
        term_pass("thread_yield");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 3 — thread_create                                        *
     * -------------------------------------------------------------- */
    int helper_tid = -1;
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 3] thread_create() — spawning helper...\n");
        helper_tid = thread_create(TASK_PRIV_KERNEL, helper_thread_entry, 0);
        if (helper_tid >= 0) {
            term_write_int("[TEST 3] Helper TID = ", helper_tid);
            term_pass("thread_create");
        } else {
            terminal_write("[TEST 3] thread_create FAILED\n");
            term_fail("thread_create");
        }
        timer_delay_ms(1000);
    }

    /* Let the helper run a bit */
    check_flush();
    terminal_write("[MAIN] Yielding so helper can run...\n");
    for (int i = 0; i < 3; i++) {
        thread_yield();
    }

    /* -------------------------------------------------------------- *
     *  Test 4 — thread_block                                         *
     * -------------------------------------------------------------- */
    if (helper_tid >= 0) {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 4] thread_block() — blocking helper...\n");
        thread_block(helper_tid);
        term_pass("thread_block (returned)");

        /* Yield a few times — the helper should NOT print anything */
        terminal_write("[TEST 4] Yielding: helper should stay silent...\n");
        for (int i = 0; i < 3; i++) {
            if (i == 2) check_flush();
            terminal_write("[MAIN] yield (helper blocked)\n");
            thread_yield();
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 5 — thread_unblock                                       *
     * -------------------------------------------------------------- */
    if (helper_tid >= 0) {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 5] thread_unblock() — unblocking helper...\n");
        thread_unblock(helper_tid);
        term_pass("thread_unblock (returned)");

        /* Yield so the helper can resume */
        terminal_write("[TEST 5] Yielding: helper should resume...\n");
        for (int i = 0; i < 5; i++) {
            if (i == 3) check_flush();
            thread_yield();
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 6 — thread_exit (clean up the helper)                    *
     * -------------------------------------------------------------- */
    if (helper_tid >= 0) {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 6] thread_exit() — destroying helper...\n");
        thread_exit(helper_tid);
        term_pass("thread_exit");

        /* Yield a bit — helper should be gone */
        for (int i = 0; i < 2; i++) {
            thread_yield();
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  All done — delay, signal the entry and exit this process      *
     * -------------------------------------------------------------- */
    terminal_write("\n========== Thread API Test Suite COMPLETE ==========\n");
    terminal_write("Returning to menu in 5 seconds...\n");
    timer_delay_ms(5000);
    test_finished_flag = 1;
    proc_exit(proc_get_pid());
}
