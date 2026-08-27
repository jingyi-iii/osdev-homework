/*******************************************************************************
 *                                                                             *
 *    Process Test Entry — Text-Mode Menu for Process API Test Suites          *
 *                                                                             *
 *    Uses terminal_server (VGA text mode 0x03) instead of graphics_server.    *
 *    Provides a keyboard-driven menu to launch thread-level or process-level  *
 *    API tests.  The test processes use terminal_write to report results      *
 *    so developers can observe whether the process.c interfaces behave        *
 *    correctly.                                                               *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_server.h"
#include "drivers/kb_server.h"
#include "kernel/process.h"

/* Test entry points (defined in demo/) */
extern void thread_api_test_main(void);
extern void process_api_test_main(void);
extern void mailbox_api_test_main(void);
extern void rbtree_test_main(void);
extern void sched_mix_test_main(void);
extern void shm_test_main(void);
extern void shm_stress_main(void);
extern void portal_api_test_main(void);

/* Menu state: 0 = waiting, 1 = thread tests, 2 = process tests, 3 = mailbox tests, 4 = rbtree tests, 5 = mixed scheduling tests, 6 = run all */
static volatile int menu_choice = 0;

/* Privilege selection state: 0 = waiting, 1 = KERNEL, 2 = USER */
static volatile int priv_choice = 0;

/*
 * Set by a test process before it calls proc_exit.
 * The entry polls this to bring the menu back.
 * volatile prevents the compiler from caching the value in a register.
 * All kernel processes share the same address space, so this works
 * across process boundaries.
 */
volatile int test_finished_flag = 0;

static void menu_kb_handler(const char* data, size_t size)
{
    (void)size;
    if (!data) return;

    char key = data[0];
    if (key == '1') menu_choice = 1;
    if (key == '2') menu_choice = 2;
    if (key == '3') menu_choice = 3;
    if (key == '4') menu_choice = 4;
    if (key == '5') menu_choice = 5;
    if (key == '6') menu_choice = 6;
    if (key == '7') menu_choice = 7;
    if (key == '8') menu_choice = 8;
    if (key == '9') menu_choice = 9;
    if (key == 'k' || key == 'K') priv_choice = 1;
    if (key == 'u' || key == 'U') priv_choice = 2;
}

/* ------------------------------------------------------------------ *
 *  Draw the text-mode menu using terminal_write                      *
 * ------------------------------------------------------------------ */
static void draw_menu(void)
{
    terminal_switch_to_text_mode();
    terminal_flush(0);

    terminal_write("========================================\n");
    terminal_write("  PROCESS API TEST SUITE                \n");
    terminal_write("========================================\n");
    terminal_write("\n");
    terminal_write("  [1] Thread API Test Suite\n");
    terminal_write("  [2] Process API Test Suite\n");
    terminal_write("  [3] Mailbox API Test Suite\n");
    terminal_write("  [4] Red-Black Tree Test Suite\n");
    terminal_write("  [5] Mixed Scheduling Test Suite\n");
    terminal_write("  [6] Run All Test Suites\n");
    terminal_write("  [7] SHM Test Suite\n");
    terminal_write("  [8] SHM Stress Test\n");
    terminal_write("  [9] Portal RPC Test Suite\n");
    terminal_write("\n");
    terminal_write("  Press 1, 2, 3, 4, 5, 6, 7, 8 or 9 to select\n");
}

/* ------------------------------------------------------------------ *
 *  Ask the user to pick KERNEL or USER privilege for a test run      *
 * ------------------------------------------------------------------ */
static proc_priv menu_ask_priv(const char* name)
{
    priv_choice = 0;
    terminal_write("\n  Run '");
    terminal_write(name);
    terminal_write("' as [K]ernel or [U]ser? (k/u) ");

    /* menu_kb_handler is still registered here and catches 'k'/'u' */
    while (priv_choice == 0) {
        thread_yield();
    }

    return (proc_priv)(priv_choice - 1);
}

/* ------------------------------------------------------------------ *
 *  Launch one test suite as a new process and wait for it to finish  *
 * ------------------------------------------------------------------ */
static void run_test_suite(const char* name, task_entry_t entry, proc_priv priv)
{
    terminal_write("\n--- Launching ");
    terminal_write(name);
    terminal_write(priv == PROC_PRIV_USER ? " (USER) ---\n\n" : " (KERNEL) ---\n\n");

    test_finished_flag = 0;
    proc_create(priv, entry, 0);

    /* Wait for the test process to finish.  The test process sets
     * test_finished_flag just before calling proc_exit on itself. */
    while (!test_finished_flag) {
        thread_yield();
    }

    terminal_write("\n*** ");
    terminal_write(name);
    terminal_write(" finished. ***\n");
}

/* ------------------------------------------------------------------ *
 *  Run every test suite once, in order                               *
 * ------------------------------------------------------------------ */
static void run_all_test_suites(proc_priv priv)
{
    terminal_write("\n========== RUNNING ALL TEST SUITES ==========\n\n");

    run_test_suite("Thread API Test Suite",      thread_api_test_main, priv);
    run_test_suite("Process API Test Suite",     process_api_test_main, priv);
    run_test_suite("Mailbox API Test Suite",     mailbox_api_test_main, priv);
    run_test_suite("Red-Black Tree Test Suite",  rbtree_test_main, priv);
    run_test_suite("Mixed Scheduling Test Suite", sched_mix_test_main, priv);
    /* SHM tests handshake via globals — all processes share the address space */
    run_test_suite("SHM Test Suite", shm_test_main, PROC_PRIV_USER);
    run_test_suite("SHM Stress Test", shm_stress_main, PROC_PRIV_USER);
    run_test_suite("Portal RPC Test Suite", portal_api_test_main, priv);

    terminal_write("\n========== ALL TEST SUITES COMPLETE ==========\n\n");
}

/* ------------------------------------------------------------------ *
 *  Main entry thread — loops forever showing the menu                *
 * ------------------------------------------------------------------ */
void process_test_main_thread(void)
{
    for (;;) {
        menu_choice = 0;
        test_finished_flag = 0;

        draw_menu();
        kb_register_callback(menu_kb_handler);

        /* Wait for user to choose a test */
        while (menu_choice == 0) {
            thread_yield();
        }

        /* Let the user pick the privilege level for this run.
         * menu_kb_handler is still registered here and catches 'k'/'u'. */
        proc_priv priv;
        if (menu_choice == 6) {
            priv = menu_ask_priv("all test suites");
        } else if (menu_choice == 1) {
            priv = menu_ask_priv("Thread API Test Suite");
        } else if (menu_choice == 2) {
            priv = menu_ask_priv("Process API Test Suite");
        } else if (menu_choice == 3) {
            priv = menu_ask_priv("Mailbox API Test Suite");
        } else if (menu_choice == 5) {
            priv = menu_ask_priv("Mixed Scheduling Test Suite");
        } else if (menu_choice == 7) {
            /* SHM tests handshake via globals — all processes share the address space */
            priv = PROC_PRIV_USER;
        } else if (menu_choice == 8) {
            priv = PROC_PRIV_USER;
        } else if (menu_choice == 9) {
            priv = menu_ask_priv("Portal RPC Test Suite");
        } else {
            priv = menu_ask_priv("Red-Black Tree Test Suite");
        }
        kb_unregister_callback(menu_kb_handler);

        /* Launch the selected test as a new process */
        if (menu_choice == 6) {
            run_all_test_suites(priv);
        } else if (menu_choice == 1) {
            run_test_suite("Thread API Test Suite", thread_api_test_main, priv);
        } else if (menu_choice == 2) {
            run_test_suite("Process API Test Suite", process_api_test_main, priv);
        } else if (menu_choice == 3) {
            run_test_suite("Mailbox API Test Suite", mailbox_api_test_main, priv);
        } else if (menu_choice == 5) {
            run_test_suite("Mixed Scheduling Test Suite", sched_mix_test_main, priv);
        } else if (menu_choice == 7) {
            run_test_suite("SHM Test Suite", shm_test_main, priv);
        } else if (menu_choice == 8) {
            run_test_suite("SHM Stress Test", shm_stress_main, priv);
        } else if (menu_choice == 9) {
            run_test_suite("Portal RPC Test Suite", portal_api_test_main, priv);
        } else {
            run_test_suite("Red-Black Tree Test Suite", rbtree_test_main, priv);
        }

        terminal_write("\n*** Returning to menu... ***\n");
    }
}
