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

#include "demo_common.h"
#include "lib/string.h"

/*
 * Set by the entry module (process_test_entry.c).  The test process writes
 * this flag just before calling proc_exit so the entry knows to redraw the
 * menu.  All kernel processes share the same address space.
 */
extern volatile int test_finished_flag;

/*
 * NOTE: the old KERNEL-mode version spawned a child PROCESS and shared
 * globals (g_child_pid) with it, because kernel processes shared one
 * address space.  A user ELF's child processes get their own page
 * directory, so cross-process globals and child entry points in the
 * caller's address space do not work — those tests are skipped here.
 */

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
        terminal_write("[SKIP] proc_create() — a user ELF's child processes get\n");
        terminal_write("       their own address space, so the old shared-global\n");
        terminal_write("       child handshake (kernel-mode test) cannot run here.\n");
        timer_delay_ms(1200);
    }

    /* -------------------------------------------------------------- *
     *  Test 3 — proc_block (skipped in user mode)                    *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[SKIP] proc_block() — needs a child process (see above).\n");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 4 — proc_unblock (skipped in user mode)                  *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[SKIP] proc_unblock() — needs a child process (see above).\n");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  All done — delay, signal the entry and exit this process      *
     * -------------------------------------------------------------- */
    terminal_write("\n========== Process API Test Suite COMPLETE ==========\n");
    terminal_write("Returning to menu in 5 seconds...\n");
    timer_delay_ms(5000);
    test_finished_flag = 1;
    thread_exit(thread_get_tid());
}
