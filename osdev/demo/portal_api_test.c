/*******************************************************************************
 *                                                                             *
 *    Portal API Test Suite — synchronous RPC (portal_call / portal_wait /     *
 *    portal_reply)                                                            *
 *                                                                             *
 *    Spawns a server thread that owns a portal (portal_init + a loop of       *
 *    portal_wait / portal_reply).  The main thread exercises portal_call      *
 *    against that portal:                                                     *
 *      - portal_init / portal_destroy lifecycle                               *
 *      - basic request/response round trip — the payload flows through the    *
 *        shared-memory mapping that portal_call creates (the server reads     *
 *        the request and echoes a modified pattern back)                      *
 *      - repeated calls (exercises the per-request done_sem create/destroy)   *
 *      - error paths (unknown portal id, NULL reply, non-shareable buffer)    *
 *                                                                             *
 *    Server and client live in the SAME process, so the shared mapping is     *
 *    created inside one address space.  Plain globals are used for            *
 *    handshaking (all processes share the kernel address space).              *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_server.h"
#include "drivers/timer_server.h"
#include "kernel/process.h"
#include "mm/vmm.h"
#include "ipc/portal.h"
#include "lib/string.h"

#define PORTAL_TEST_PAGES    4
#define PORTAL_TEST_SIZE     (PORTAL_TEST_PAGES * PAGE_SIZE)
#define PORTAL_TEST_PATTERN  0x5A
#define PORTAL_TEST_RESP     0x0CAFE

extern volatile int test_finished_flag;

/* ------------------------------------------------------------------ *
 *  Handshake state (shared between the main thread and the server    *
 *  thread; both live in this test process).                          *
 * ------------------------------------------------------------------ */
static volatile int    portal_server_ready;   /* server published its portal id */
static volatile u32    portal_server_id;      /* portal id owned by the server  */
static volatile int    portal_server_err;     /* server-side error (0 = ok)     */
static volatile int    portal_server_rx_ok;   /* server saw the request payload */
static volatile int    portal_server_done;    /* server finished a reply        */

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

static void check_flush(void)
{
    if (terminal_get_row() >= 12) {
        timer_delay_ms(1500);
        terminal_flush(0);
    }
}

/* ------------------------------------------------------------------ *
 *  Server thread — owns a portal, services requests forever.          *
 *  Runs as a plain USER thread so the suite works under either        *
 *  KERNEL or USER menu selection.                                     *
 * ------------------------------------------------------------------ */
static struct portal g_server_portal;

static void portal_server_entry(void)
{
    memset(&g_server_portal, 0, sizeof(g_server_portal));
    if (portal_init(&g_server_portal) != 0) {
        portal_server_err = 1;
        portal_server_ready = 1;
        for (;;) thread_yield();
    }

    portal_server_id  = g_server_portal.id;
    portal_server_ready = 1;

    for (;;) {
        portal_req* req = portal_wait(PORTAL_ID_ANY);
        if (!req) {
            portal_server_err = 1;
            continue;
        }

        /* Verify the payload arrived through the shared mapping. */
        volatile unsigned char* p = (volatile unsigned char*)req->shm_va;
        int ok = 1;
        size_t n = (req->shm_size < PORTAL_TEST_SIZE) ? req->shm_size
                                                      : PORTAL_TEST_SIZE;
        for (size_t i = 0; i < n; i++) {
            if (p[i] != (unsigned char)PORTAL_TEST_PATTERN) {
                ok = 0;
                break;
            }
        }
        portal_server_rx_ok = ok;

        /* Echo a modified pattern back through the same mapping. */
        for (size_t i = 0; i < n; i++)
            p[i] = (unsigned char)(PORTAL_TEST_PATTERN + 1);

        req->resp.ret = PORTAL_TEST_RESP;
        portal_reply(req);
        portal_server_done = 1;
    }
}

/* ------------------------------------------------------------------ *
 *  Main test entry                                                    *
 * ------------------------------------------------------------------ */
void portal_api_test_main(void)
{
    pcb* self = get_current_process();
    unsigned char* buf = 0;
    int server_tid = 0;
    int pass = 1;
    int my_pid = proc_get_pid();

    /* Reset handshake state — the suite may be launched repeatedly. */
    portal_server_ready = 0;
    portal_server_id    = 0;
    portal_server_err   = 0;
    portal_server_rx_ok = 0;
    portal_server_done  = 0;

    terminal_write("\n========== Portal API Test Suite ==========\n\n");

    /* -------------------------------------------------------------- *
     *  Test 1 — portal_init / portal_destroy lifecycle                *
     * -------------------------------------------------------------- */
    {
        struct portal p;
        int ret = portal_init(&p);
        if (ret == 0 && p.id != 0) {
            term_pass("portal_init (id assigned)");
            portal_destroy(&p);
            term_pass("portal_destroy (no crash)");
        } else {
            term_fail("portal_init");
            pass = 0;
        }
        ret = portal_init(0);
        if (ret != 0)
            term_pass("portal_init(NULL) rejected");
        else {
            term_fail("portal_init(NULL)");
            pass = 0;
        }
        timer_delay_ms(800);
    }

    /* -------------------------------------------------------------- *
     *  Test 2 — basic RPC round trip                                  *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 2] Basic RPC round trip\n");

        /* The buffer must live in a VMM-mapped region: shm_share (inside
         * portal_call) resolves it with vmm_va_to_pa. */
        buf = (unsigned char*)vmm_alloc_pages(&self->vcb, PORTAL_TEST_PAGES,
                                              PTE_USER_PAGE);
        if (!buf) {
            terminal_write("[PORTAL] FAIL: vmm_alloc_pages\n");
            pass = 0;
            goto done;
        }
        for (int i = 0; i < PORTAL_TEST_SIZE; i++)
            buf[i] = (unsigned char)PORTAL_TEST_PATTERN;

        server_tid = thread_create(TASK_PRIV_USER, portal_server_entry, 0);
        if (server_tid < 0) {
            terminal_write("[PORTAL] FAIL: thread_create(server)\n");
            pass = 0;
            goto done;
        }
        while (!portal_server_ready)
            thread_yield();
        if (portal_server_err) {
            term_fail("server portal_init");
            pass = 0;
            goto done;
        }

        int ret = portal_call(portal_server_id, buf, PORTAL_TEST_SIZE);
        if (ret == PORTAL_TEST_RESP) {
            term_pass("portal_call returned resp.ret");
        } else {
            term_write_int("  resp.ret: ", ret);
            term_fail("portal_call response");
            pass = 0;
        }

        if (portal_server_rx_ok)
            term_pass("server saw payload via shared mapping");
        else {
            term_fail("server payload");
            pass = 0;
        }

        int echo_ok = 1;
        for (int i = 0; i < PORTAL_TEST_SIZE; i++) {
            if (buf[i] != (unsigned char)(PORTAL_TEST_PATTERN + 1)) {
                echo_ok = 0;
                break;
            }
        }
        if (echo_ok)
            term_pass("echo back through shared mapping");
        else {
            term_fail("echo through shared mapping");
            pass = 0;
        }

        timer_delay_ms(800);
    }

    /* -------------------------------------------------------------- *
     *  Test 3 — repeated calls (per-request done_sem churn)           *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 3] Repeated portal_call\n");

        int ok = 1;
        for (int call = 0; call < 5; call++) {
            for (int i = 0; i < PORTAL_TEST_SIZE; i++)
                buf[i] = (unsigned char)PORTAL_TEST_PATTERN;
            portal_server_rx_ok = 0;
            portal_server_done  = 0;

            int ret = portal_call(portal_server_id, buf, PORTAL_TEST_SIZE);
            if (ret != PORTAL_TEST_RESP || !portal_server_rx_ok) {
                ok = 0;
                break;
            }
        }
        if (ok)
            term_pass("5 consecutive calls");
        else {
            term_fail("repeated calls");
            pass = 0;
        }
        timer_delay_ms(800);
    }

    /* -------------------------------------------------------------- *
     *  Test 4 — error paths                                           *
     * -------------------------------------------------------------- */
    {
        check_flush();
        terminal_write("\n");
        terminal_write("[TEST 4] Error paths\n");

        int ret = portal_call(0x7FFFFFFF, buf, PORTAL_TEST_SIZE);
        if (ret != 0)
            term_pass("portal_call(unknown id) rejected");
        else {
            term_fail("portal_call(unknown id)");
            pass = 0;
        }

        ret = portal_reply(0);
        if (ret != 0)
            term_pass("portal_reply(NULL) rejected");
        else {
            term_fail("portal_reply(NULL)");
            pass = 0;
        }

        /* A plain global (kernel image, not a VMM region) cannot be
         * shared; the call must fail instead of hanging. */
        static char non_shareable[16];
        ret = portal_call(portal_server_id, non_shareable, sizeof(non_shareable));
        if (ret != 0)
            term_pass("portal_call(non-shareable buf) rejected");
        else {
            term_fail("portal_call(non-shareable buf)");
            pass = 0;
        }
        timer_delay_ms(800);
    }

done:
    /* Teardown: kill the server thread first (it may be parked in
     * portal_wait), then destroy the portal and free the buffer. */
    if (server_tid > 0)
        thread_exit(server_tid);
    if (g_server_portal.id)
        portal_destroy(&g_server_portal);
    if (buf)
        vmm_free_pages(&self->vcb, buf);

    if (pass)
        terminal_write("\n========== Portal API Test Suite COMPLETE (PASS) ==========\n");
    else
        terminal_write("\n========== Portal API Test Suite COMPLETE (FAIL) ==========\n");
    terminal_write("Returning to menu in 5 seconds...\n");
    timer_delay_ms(5000);
    test_finished_flag = 1;
    proc_exit(my_pid);
}
