/*******************************************************************************
 *                                                                             *
 *    Process Test Entry — Text-Mode Menu for Process API Test Suites          *
 *                                                                             *
 *    Uses terminal_driver (VGA text mode 0x03) instead of graphics_driver.    *
 *    Provides a keyboard-driven menu to launch thread-level or process-level  *
 *    API tests.  The test processes use terminal_write to report results      *
 *    so developers can observe whether the process.c interfaces behave        *
 *    correctly.                                                               *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_driver.h"
#include "drivers/kb_driver.h"
#include "kernel/process.h"

/* Test entry points (defined in demo/) */
extern void thread_api_test_main(void);
extern void process_api_test_main(void);
extern void mailbox_api_test_main(void);
extern void rbtree_test_main(void);
extern void sched_mix_test_main(void);

/* Menu state: 0 = waiting, 1 = thread tests, 2 = process tests, 3 = mailbox tests, 4 = rbtree tests, 5 = mixed scheduling tests */
static volatile int menu_choice = 0;

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
}

/* ------------------------------------------------------------------ *
 *  Draw the text-mode menu using terminal_write                      *
 * ------------------------------------------------------------------ */
static void draw_menu(void)
{
    terminal_switch_to_text_mode();
    terminal_flush(0);

    terminal_write("========================================\n");
    terminal_write("  PROCESS API TEST SUITE (Text Mode)    \n");
    terminal_write("========================================\n");
    terminal_write("\n");
    terminal_write("  [1] Thread API Test Suite\n");
    terminal_write("      thread_create / thread_yield\n");
    terminal_write("      thread_block / thread_unblock\n");
    terminal_write("      thread_exit / proc_get_pid\n");
    terminal_write("\n");
    terminal_write("  [2] Process API Test Suite\n");
    terminal_write("      proc_create / proc_exit\n");
    terminal_write("      proc_block / proc_unblock\n");
    terminal_write("      proc_get_pid\n");
    terminal_write("\n");
    terminal_write("  [3] Mailbox API Test Suite\n");
    terminal_write("      mailbox_send / mailbox_listen\n");
    terminal_write("      mailbox_register_handler\n");
    terminal_write("      broadcast (MAIL_ANY_TID)\n");
    terminal_write("\n");
    terminal_write("  [4] Red-Black Tree Test Suite\n");
    terminal_write("      rbtree_insert / rbtree_delete / rbtree_search\n");
    terminal_write("      rbtree_next / rbtree_prev / rbtree_replace_node\n");
    terminal_write("      rbtree_find_or_insert / rbtree_for_each_safe\n");
    terminal_write("\n");
    terminal_write("  [5] Mixed Scheduling Test Suite\n");
    terminal_write("      kernel + user thread interleaving\n");
    terminal_write("      cross-privilege block / unblock\n");
    terminal_write("\n");
    terminal_write("  Press 1, 2, 3, 4 or 5 to select a test suite\n");
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

        kb_unregister_callback(menu_kb_handler);

        /* Launch the selected test as a new process */
        if (menu_choice == 1) {
            terminal_write("\n--- Launching Thread API Test Suite ---\n\n");
            proc_create(PROC_PRIV_KERNEL, thread_api_test_main);
        } else if (menu_choice == 2) {
            terminal_write("\n--- Launching Process API Test Suite ---\n\n");
            proc_create(PROC_PRIV_KERNEL, process_api_test_main);
        } else if (menu_choice == 3) {
            terminal_write("\n--- Launching Mailbox API Test Suite ---\n\n");
            proc_create(PROC_PRIV_KERNEL, mailbox_api_test_main);
        } else if (menu_choice == 5) {
            terminal_write("\n--- Launching Mixed Scheduling Test Suite ---\n\n");
            proc_create(PROC_PRIV_KERNEL, sched_mix_test_main);
        } else {
            terminal_write("\n--- Launching Red-Black Tree Test Suite ---\n\n");
            proc_create(PROC_PRIV_KERNEL, rbtree_test_main);
        }

        /*
         * Wait for the test process to finish.  The test process sets
         * test_finished_flag just before calling proc_exit on itself.
         */
        while (!test_finished_flag) {
            thread_yield();
        }

        terminal_write("\n*** Test suite finished. Returning to menu... ***\n");
    }
}
