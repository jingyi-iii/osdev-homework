/*******************************************************************************
 *                                                                             *
 *    Mailbox API Test Suite                                                   *
 *                                                                             *
 *    Tests the following mailbox interfaces:                                  *
 *      - mailbox_alloc_mail / mailbox_release_mail     mail lifecycle          *
 *      - mailbox_alloc / mailbox_release               mailbox lifecycle      *
 *      - mailbox_send + mailbox_listen                 queue path (no handler)*
 *      - mailbox_register_handler                      handler delivery       *
 *      - mailbox_unregister_handler                    handler removal        *
 *      - mailbox_send (broadcast)                      MAIL_ANY_PID/ANY_TID   *
 *                                                                             *
 *    IMPORTANT: handlers run synchronously in ISR context with mb->sp_lock    *
 *    held.  They MUST NOT call any mailbox API, terminal_write, or any        *
 *    function that may block or re-enter the syscall layer.  Handlers only    *
 *    set global volatile flags and copy data for later verification.          *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_server.h"
#include "drivers/timer_server.h"
#include "kernel/process.h"
#include "ipc/mailbox.h"
#include "lib/string.h"

extern volatile int test_finished_flag;

/* ------------------------------------------------------------------ *
 *  Shared state for handler verification                              *
 * ------------------------------------------------------------------ */
static volatile int    g_h1_called;
static volatile int    g_h2_called;
static volatile int    g_h1_sender_pid;
static volatile int    g_h2_sender_pid;
static volatile size_t g_h1_data_size;
static volatile size_t g_h2_data_size;
static volatile char   g_h1_data[256];
static volatile char   g_h2_data[256];

/* ------------------------------------------------------------------ *
 *  Shared state for listener (queue path) verification                *
 * ------------------------------------------------------------------ */
static volatile int    g_listener_got_mail;
static volatile size_t g_listener_data_size;
static volatile char   g_listener_data[256];

/* ------------------------------------------------------------------ *
 *  Handlers — ISR context, keep them minimal                          *
 * ------------------------------------------------------------------ */
static void handler_one(mail* m)
{
    g_h1_called++;
    g_h1_sender_pid = m->sender_pid;
    g_h1_data_size   = m->data_size;
    for (size_t i = 0; i < m->data_size && i < sizeof(g_h1_data); i++)
        g_h1_data[i] = m->data[i];
}

static void handler_two(mail* m)
{
    g_h2_called++;
    g_h2_sender_pid = m->sender_pid;
    g_h2_data_size   = m->data_size;
    for (size_t i = 0; i < m->data_size && i < sizeof(g_h2_data); i++)
        g_h2_data[i] = m->data[i];
}

/* ------------------------------------------------------------------ *
 *  Reset handler state between tests                                  *
 * ------------------------------------------------------------------ */
static void reset_handler_state(void)
{
    g_h1_called      = 0;
    g_h2_called      = 0;
    g_h1_sender_pid  = -1;
    g_h2_sender_pid  = -1;
    g_h1_data_size   = 0;
    g_h2_data_size   = 0;
    g_listener_got_mail    = 0;
    g_listener_data_size   = 0;
}

/* ------------------------------------------------------------------ *
 *  Listener thread — blocks on mailbox_listen, then spins             *
 * ------------------------------------------------------------------ */
static void listener_thread_entry(void)
{
    tcb* me = thread_get_by_tid(thread_get_tid());
    if (!me || !me->mailbox)
        return;

    mail* m = mailbox_listen(me->mailbox);
    if (m) {
        g_listener_got_mail  = 1;
        g_listener_data_size = m->data_size;
        for (size_t i = 0; i < m->data_size && i < sizeof(g_listener_data); i++)
            g_listener_data[i] = m->data[i];
        mailbox_release_mail(m);
    }

    /* kernel thread must not return */
    for (;;) thread_yield();
}

/* ------------------------------------------------------------------ *
 *  Helper thread for handler tests — just sits forever                *
 * ------------------------------------------------------------------ */
static void handler_host_thread(void)
{
    for (;;) thread_yield();
}

/* ------------------------------------------------------------------ *
 *  Output helpers                                                     *
 * ------------------------------------------------------------------ */
static void term_write_int(const char* prefix, int val)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%s%d\n", prefix, val);
    terminal_write(buf);
}

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
 *  Main test entry                                                    *
 * ================================================================== */
void mailbox_api_test_main(void)
{
    terminal_flush(0);
    terminal_write("\n========== Mailbox API Test Suite ==========\n\n");

    int my_pid = proc_get_pid();
    term_write_int("Test process PID: ", my_pid);

    /* -------------------------------------------------------------- *
     *  Test 1 — mailbox_alloc_mail / mailbox_release_mail             *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 1] mailbox_alloc_mail / mailbox_release_mail\n");
        mail* m = mailbox_alloc_mail();
        if (m) {
            term_pass("mailbox_alloc_mail (non-NULL)");

            /* write some data */
            const char* msg = "test1";
            for (size_t i = 0; msg[i]; i++)
                m->data[i] = msg[i];
            m->data_size = 5;

            mailbox_release_mail(m);
            term_pass("mailbox_release_mail (no crash)");
        } else {
            term_fail("mailbox_alloc_mail returned NULL");
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 2 — mailbox_alloc / mailbox_release                       *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 2] mailbox_alloc / mailbox_release\n");
        mailbox* mb = mailbox_alloc(my_pid, 0);
        if (mb) {
            term_pass("mailbox_alloc (non-NULL)");
            mailbox_release(mb);
            term_pass("mailbox_release (no crash)");
        } else {
            term_fail("mailbox_alloc returned NULL");
        }
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  Test 3 — Unicast send + listen (queue path, no handler)        *
     *                                                                    *
     *  IMPORTANT: we must send the mail BEFORE the listener enters     *
     *  mailbox_listen.  Otherwise both threads are inside int $100     *
     *  at the same time, which causes a re-entrancy hang because       *
     *  the syscall uses an Interrupt Gate (not Trap Gate).             *
     *  We use thread_block / thread_unblock to sequence this safely.   *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 3] Send + Listen (queue path)\n");
        reset_handler_state();

        /* Create a listener thread but block it immediately so it
         * does NOT enter mailbox_listen before the mail is queued. */
        int listener_tid = thread_create(TASK_PRIV_KERNEL, listener_thread_entry, 0);
        if (listener_tid < 0) {
            term_fail("thread_create for listener");
            goto skip_test3;
        }
        term_write_int("  Listener TID: ", listener_tid);

        thread_block(listener_tid);

        /* Send a unicast mail to the (still blocked) listener's TID.
         * No handler → mail is queued in the listener's mailbox. */
        mail* m = mailbox_alloc_mail();
        if (!m) {
            term_fail("alloc_mail in test 3");
            thread_unblock(listener_tid);
            goto skip_test3;
        }
        const char* msg = "hello_listener";
        for (size_t i = 0; msg[i]; i++)
            m->data[i] = msg[i];
        m->data_size   = 14;
        m->sender_pid  = my_pid;
        m->sender_tid  = thread_get_tid();
        m->receiver_pid = my_pid;
        m->receiver_tid = listener_tid;

        int ret = mailbox_send(m);
        term_write_int("  mailbox_send returned: ", ret);

        /* Now unblock the listener.  It will enter mailbox_listen
         * and find the mail already waiting in the queue. */
        thread_unblock(listener_tid);

        /* Wait for the listener to pick up the mail */
        for (int i = 0; i < 10 && !g_listener_got_mail; i++)
            thread_yield();

        if (g_listener_got_mail) {
            term_pass("listener received mail");
            term_write_int("  data_size: ", (int)g_listener_data_size);
            if (g_listener_data_size == 14)
                term_pass("data_size correct");
            else
                term_fail("data_size wrong");
        } else {
            term_fail("listener did NOT receive mail");
        }

        /* Clean up the listener thread */
        thread_exit(listener_tid);
        skip_test3:
        timer_delay_ms(1000);
        ;
    }

    /* -------------------------------------------------------------- *
     *  Test 4 — Handler registration + unicast delivery               *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 4] Register handler + unicast delivery\n");
        reset_handler_state();

        /* Create a helper thread whose mailbox we'll register a handler on */
        int host_tid = thread_create(TASK_PRIV_KERNEL, handler_host_thread, 0);
        if (host_tid < 0) {
            term_fail("thread_create for handler host");
            goto skip_test4;
        }
        term_write_int("  Host TID: ", host_tid);

        /* Get the host's auto-created mailbox and register a handler */
        tcb* host_tcb = thread_get_by_tid(host_tid);
        if (!host_tcb || !host_tcb->mailbox) {
            term_fail("host TCB or mailbox is NULL");
            goto skip_test4;
        }

        int reg_ret = mailbox_register_handler(host_tcb->mailbox, handler_one);
        term_write_int("  register_handler returned: ", reg_ret);
        if (reg_ret != 0) {
            term_fail("register_handler");
            goto skip_test4;
        }
        term_pass("register_handler");

        /* Send a unicast mail to the host thread */
        mail* m = mailbox_alloc_mail();
        if (!m) {
            term_fail("alloc_mail in test 4");
            goto skip_test4;
        }
        const char* msg = "handler_test4";
        for (size_t i = 0; msg[i]; i++)
            m->data[i] = msg[i];
        m->data_size   = 13;
        m->sender_pid  = my_pid;
        m->sender_tid  = thread_get_tid();
        m->receiver_pid = my_pid;
        m->receiver_tid = host_tid;

        mailbox_send(m);

        /* Handler runs synchronously — check flags immediately */
        if (g_h1_called == 1) {
            term_pass("handler called exactly once");
        } else {
            term_write_int("  handler called count: ", g_h1_called);
            term_fail("handler not called or called multiple times");
        }

        if (g_h1_data_size == 13)
            term_pass("handler data_size correct");
        else
            term_fail("handler data_size wrong");

        /* Clean up: unregister and destroy host thread */
        mailbox_unregister_handler(host_tcb->mailbox, handler_one);
        thread_exit(host_tid);
        skip_test4:
        timer_delay_ms(1000);
        ;
    }

    /* -------------------------------------------------------------- *
     *  Test 5 — Multiple handlers on one mailbox                      *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 5] Multiple handlers on one mailbox\n");
        reset_handler_state();

        int host_tid = thread_create(TASK_PRIV_KERNEL, handler_host_thread, 0);
        if (host_tid < 0) {
            term_fail("thread_create for handler host");
            goto skip_test5;
        }

        tcb* host_tcb = thread_get_by_tid(host_tid);
        if (!host_tcb || !host_tcb->mailbox) {
            term_fail("host TCB or mailbox NULL");
            goto skip_test5;
        }

        /* Register two handlers */
        mailbox_register_handler(host_tcb->mailbox, handler_one);
        mailbox_register_handler(host_tcb->mailbox, handler_two);
        term_pass("registered two handlers");

        /* Send one mail */
        mail* m = mailbox_alloc_mail();
        if (!m) { term_fail("alloc_mail"); goto skip_test5; }
        const char* msg = "multi_handler";
        for (size_t i = 0; msg[i]; i++)
            m->data[i] = msg[i];
        m->data_size   = 13;
        m->sender_pid  = my_pid;
        m->sender_tid  = thread_get_tid();
        m->receiver_pid = my_pid;
        m->receiver_tid = host_tid;
        mailbox_send(m);

        /* Both handlers should have been called exactly once */
        if (g_h1_called == 1 && g_h2_called == 1) {
            term_pass("both handlers called exactly once");
        } else {
            term_write_int("  handler1 count: ", g_h1_called);
            term_write_int("  handler2 count: ", g_h2_called);
            term_fail("handlers not both called once");
        }

        /* Clean up */
        mailbox_unregister_handler(host_tcb->mailbox, handler_one);
        mailbox_unregister_handler(host_tcb->mailbox, handler_two);
        thread_exit(host_tid);
        skip_test5:
        timer_delay_ms(1000);
        ;
    }

    /* -------------------------------------------------------------- *
     *  Test 6 — Unregister handler, verify it's no longer called      *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 6] Unregister handler\n");
        reset_handler_state();

        int host_tid = thread_create(TASK_PRIV_KERNEL, handler_host_thread, 0);
        if (host_tid < 0) {
            term_fail("thread_create");
            goto skip_test6;
        }

        tcb* host_tcb = thread_get_by_tid(host_tid);
        if (!host_tcb || !host_tcb->mailbox) {
            term_fail("host TCB/mailbox NULL");
            goto skip_test6;
        }

        mailbox_register_handler(host_tcb->mailbox, handler_one);

        /* Unregister it */
        int unreg_ret = mailbox_unregister_handler(host_tcb->mailbox, handler_one);
        term_write_int("  unregister returned: ", unreg_ret);
        if (unreg_ret == 0)
            term_pass("unregister_handler");
        else
            term_fail("unregister_handler");

        /* Send a mail — handler should NOT be called */
        mail* m = mailbox_alloc_mail();
        if (!m) { term_fail("alloc_mail"); goto skip_test6; }
        const char* msg = "should_not_arrive";
        for (size_t i = 0; msg[i]; i++)
            m->data[i] = msg[i];
        m->data_size   = 16;
        m->sender_pid  = my_pid;
        m->sender_tid  = thread_get_tid();
        m->receiver_pid = my_pid;
        m->receiver_tid = host_tid;
        mailbox_send(m);

        /*
         * No handler → mail is queued in host's mailbox.
         * Handler should NOT have been called.
         */
        if (g_h1_called == 0)
            term_pass("handler NOT called after unregister");
        else
            term_fail("handler WAS called after unregister");

        /* Clean up */
        thread_exit(host_tid);
        skip_test6:
        timer_delay_ms(1000);
        ;
    }

    /* -------------------------------------------------------------- *
     *  Test 7 — Broadcast to threads with handlers                    *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 7] Broadcast (MAIL_ANY_TID) to handlers\n");
        reset_handler_state();

        /* Create two host threads, register handlers on both */
        int host1_tid = thread_create(TASK_PRIV_KERNEL, handler_host_thread, 0);
        int host2_tid = thread_create(TASK_PRIV_KERNEL, handler_host_thread, 0);
        if (host1_tid < 0 || host2_tid < 0) {
            term_fail("thread_create for broadcast hosts");
            goto skip_test7;
        }

        tcb* host1 = thread_get_by_tid(host1_tid);
        tcb* host2 = thread_get_by_tid(host2_tid);
        if (!host1 || !host1->mailbox || !host2 || !host2->mailbox) {
            term_fail("host TCB/mailbox NULL");
            goto skip_test7;
        }

        mailbox_register_handler(host1->mailbox, handler_one);
        mailbox_register_handler(host2->mailbox, handler_two);

        /* Send a broadcast mail */
        mail* m = mailbox_alloc_mail();
        if (!m) { term_fail("alloc_mail"); goto skip_test7; }
        const char* msg = "broadcast";
        for (size_t i = 0; msg[i]; i++)
            m->data[i] = msg[i];
        m->data_size    = 9;
        m->sender_pid   = my_pid;
        m->sender_tid   = thread_get_tid();
        m->receiver_pid = MAIL_ANY_PID;
        m->receiver_tid = MAIL_ANY_TID;
        mailbox_send(m);

        /* Both handlers should have been called */
        if (g_h1_called == 1 && g_h2_called == 1) {
            term_pass("broadcast: both handlers called");
        } else {
            term_write_int("  handler1 count: ", g_h1_called);
            term_write_int("  handler2 count: ", g_h2_called);
            term_fail("broadcast handlers not both called");
        }

        /* Clean up */
        mailbox_unregister_handler(host1->mailbox, handler_one);
        mailbox_unregister_handler(host2->mailbox, handler_two);
        thread_exit(host1_tid);
        thread_exit(host2_tid);
        skip_test7:
        timer_delay_ms(1000);
        ;
    }

    /* -------------------------------------------------------------- *
     *  Test 8 — Error: send to non-existent receiver                  *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 8] Send to non-existent receiver\n");

        mail* m = mailbox_alloc_mail();
        if (!m) {
            term_fail("alloc_mail");
            goto skip_test8;
        }
        m->receiver_pid = my_pid;
        m->receiver_tid = 0x7FFFFFFF;  /* unlikely to be a valid TID */

        int ret = mailbox_send(m);
        term_write_int("  mailbox_send returned: ", ret);
        if (ret != 0)
            term_pass("send to bad TID returns error");
        else
            term_fail("send to bad TID should NOT return 0");

        skip_test8:
        timer_delay_ms(1000);
        ;
    }

    /* -------------------------------------------------------------- *
     *  Test 9 — Error: NULL parameters                                *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 9] NULL parameter checks\n");

        int ret;

        ret = mailbox_send(0);
        if (ret != 0)
            term_pass("mailbox_send(NULL) returns error");
        else
            term_fail("mailbox_send(NULL)");

        ret = mailbox_register_handler(0, handler_one);
        if (ret != 0)
            term_pass("register_handler(NULL mb) returns error");
        else
            term_fail("register_handler(NULL mb)");

        ret = mailbox_register_handler((mailbox*)1, 0);
        if (ret != 0)
            term_pass("register_handler(NULL handler) returns error");
        else
            term_fail("register_handler(NULL handler)");

        ret = mailbox_unregister_handler(0, handler_one);
        if (ret != 0)
            term_pass("unregister_handler(NULL mb) returns error");
        else
            term_fail("unregister_handler(NULL mb)");

        ret = mailbox_unregister_handler((mailbox*)1, 0);
        if (ret != 0)
            term_pass("unregister_handler(NULL handler) returns error");
        else
            term_fail("unregister_handler(NULL handler)");
        timer_delay_ms(1000);
    }

    /* -------------------------------------------------------------- *
     *  All done                                                        *
     * -------------------------------------------------------------- */
    terminal_write("\n========== Mailbox API Test Suite COMPLETE ==========\n");
    terminal_write("Returning to menu in 5 seconds...\n");
    timer_delay_ms(5000);
    test_finished_flag = 1;
    proc_exit(my_pid);
}
