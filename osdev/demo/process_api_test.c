/*******************************************************************************
 *                                                                             *
 *    Process API Test Suite                                                   *
 *                                                                             *
 *    Tests the following process.c process-level interfaces:                  *
 *      - proc_get_pid()      get current process ID                           *
 *      - proc_create()       spawn a child process                            *
 *      - proc_block()        block an entire process by PID                   *
 *      - proc_unblock()      unblock a previously blocked process             *
 *      - proc_exit()         destroy a process (self + child)                 *
 *                                                                             *
 *    Strategy:                                                                *
 *      The main test thread creates a child process.  The child sets a        *
 *      global variable with its PID, then does some work yielding.            *
 *      The parent reads the child PID, tests proc_block / proc_unblock,       *
 *      observes the child's behaviour, and finally exits.                     *
 *                                                                             *
 *    All output goes through terminal_write so results are visible in         *
 *    VGA text mode (mode 0x03).                                               *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_driver.h"
#include "drivers/timer_driver.h"
#include "drivers/log_driver.h"
#include "kernel/process.h"
#include "lib/string.h"

/*
 * Set by the entry module (process_test_entry.c).  The test process writes
 * this flag just before calling proc_exit so the entry knows to redraw the
 * menu.  All kernel processes share the same address space.
 */
extern volatile int test_finished_flag;

/*
 * Shared between parent and child process (same address space).
 * The child writes its PID here; the parent reads it for block/unblock tests.
 */
static volatile int g_child_pid = -1;

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
 *  Child Process Entry Point                                          *
 * ================================================================== */
static void child_proc_main(void)
{
    g_child_pid = proc_get_pid();

    terminal_write("[CHILD] Started.  My PID = ");
    term_write_int("", g_child_pid);

    /*
     * Do several iterations of work, yielding each time.
     * When the parent calls proc_block on us, we will stop being
     * scheduled.  After proc_unblock, we resume right where we
     * left off (inside thread_yield or the for loop).
     */
    for (int i = 0; i < 6; i++) {
        terminal_write("[CHILD] iteration ");
        term_write_int("", i);
        thread_yield();
    }

    terminal_write("[CHILD] Work complete, exiting my process.\n");
    proc_exit(g_child_pid);
}

/* ================================================================== *
 *  Main Test Thread (process_api_test_main)                            *
 * ================================================================== */
void process_api_test_main(void)
{
    terminal_flush(0);
    terminal_write("\n========== Process API Test Suite ==========\n\n");

    /* -------------------------------------------------------------- *
     *  Test 1 — proc_get_pid (self)                                  *
     * -------------------------------------------------------------- */
    int my_pid = -1;
    {
        check_flush();
        terminal_write("\n");
        my_pid = proc_get_pid();
        terminal_write("[TEST 1] proc_get_pid() => ");
        term_write_int("", my_pid);
        if (my_pid >= 0)
            term_pass("proc_get_pid");
        else
            term_fail("proc_get_pid");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 2 — proc_create                                          *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 2] proc_create() — spawning child process...\n");
        proc_create(PROC_PRIV_KERNEL, child_proc_main);

        /* Wait for the child to run and write its PID */
        while (g_child_pid == -1) {
            thread_yield();
        }
        term_write_int("[TEST 2] Child PID = ", g_child_pid);
        term_pass("proc_create");
        timer_delay_ms(1000);
    }

    /* Let the child run a couple of iterations so we can see its output */
    check_flush();
    terminal_write("[MAIN] Yielding to let child run a bit...\n");
    for (int i = 0; i < 3; i++) {
        thread_yield();
    }

    /* -------------------------------------------------------------- *
     *  Test 3 — proc_block                                           *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        int cpid = g_child_pid;
        terminal_write("[TEST 3] proc_block() — blocking child process...\n");
        proc_block(cpid);
        term_pass("proc_block (returned)");

        /*
         * Yield several times — the child should NOT print any more
         * "[CHILD] iteration" messages while blocked.
         */
        terminal_write("[TEST 3] Yielding: child should stay silent...\n");
        for (int i = 0; i < 4; i++) {
            if (i == 2) check_flush();
            terminal_write("[MAIN] yield (child blocked)\n");
            thread_yield();
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 4 — proc_unblock                                         *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        int cpid = g_child_pid;
        terminal_write("[TEST 4] proc_unblock() — unblocking child...\n");
        proc_unblock(cpid);
        term_pass("proc_unblock (returned)");

        /* Yield so the child can resume its iterations */
        terminal_write("[TEST 4] Yielding: child should resume...\n");
        for (int i = 0; i < 8; i++) {
            if (i == 4) check_flush();
            terminal_write("[MAIN] yield\n");
            thread_yield();
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  All done — delay, signal the entry and exit this process      *
     * -------------------------------------------------------------- */
    terminal_write("\n========== Process API Test Suite COMPLETE ==========\n");
    terminal_write("Returning to menu in 5 seconds...\n");
    timer_delay_ms(5000);
    test_finished_flag = 1;
    proc_exit(my_pid);
}
