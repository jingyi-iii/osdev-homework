/*
 * user/portal_test.c — standalone user-mode ELF that exercises the portal
 * RPC subsystem.
 *
 * Loaded by the kernel as a GRUB multiboot module, after the user confirms
 * with a keypress (see kernel/init.c).  Runs two tests:
 *
 *   Test 1 (console portal): client-side portal_call() to the terminal
 *          server's console portal (resolved via the namespace) to print
 *          text.
 *   Test 2 (self portal): the main thread spawns a server thread that owns
 *          a private portal; the main thread portal_calls it with an
 *          inline payload; the server verifies the payload and echoes a
 *          magic value back in the reply.
 *
 * This ELF links only userlib + fixed syscalls — no kernel symbols.
 */

#include "userlib.h"

#define PT_MAGIC 0x1234

/* ---- Test 2 shared state (same address space: threads) ---- */
static volatile int srv_ready = 0;
static u32 srv_portal_id = 0;

static const char pt_payload[] = "portal self-test payload";

/* Print through the terminal server's console portal. */
static void pt_print(const char* s)
{
    console_putstr(s);
}

/* ------------------------------------------------------------------ *
 *  Server side (user-side wrappers around SYSCALL_PORTAL).
 * ------------------------------------------------------------------ */
static void server_wait_and_reply(void)
{
    user_portal_ctrl cfg = {0};

    /* Block until a request arrives, then dequeue it. */
    cfg.cmd = U_PORTAL_CTRL_WAIT;
    cfg.client_id = srv_portal_id;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

    cfg.cmd = U_PORTAL_CTRL_GET_REQ;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    if (!cfg.req)
        return;

    /* Verify the shm-mapped payload byte-for-byte. */
    const char* va = (const char*)cfg.va;
    u32 size = (u32)cfg.va_size;
    int ok = (size == sizeof(pt_payload) - 1);
    for (u32 i = 0; ok && i < size; i++)
        if (va[i] != pt_payload[i])
            ok = 0;

    cfg.cmd = U_PORTAL_CTRL_REPLY;
    cfg.ret = ok ? PT_MAGIC : -1;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
}

static void portal_server_thread(void* param)
{
    (void)param;

    user_portal_ctrl cfg = {0};
    cfg.cmd = U_PORTAL_CTRL_INIT;
    cfg.server_id = 0;              /* auto-assigned portal id */
    if (user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg)) != 0)
        return;

    srv_portal_id = (u32)(uptr)cfg.out;
    srv_ready = 1;

    for (;;)
        server_wait_and_reply();
}

/* ------------------------------------------------------------------ *
 *  Tests
 * ------------------------------------------------------------------ */
static void test_console_portal(void)
{
    pt_print("\n[portal-test] === Test 1: console portal (via namespace) ===\n");

    static const char hello[] = "hello via console portal";
    u32 cid = 0, mt = 0, mm = 0;
    int tries = 0;

    /* Resolve the console portal through the namespace instead of
     * hardcoding a portal id. */
    while (tries++ < 10000) {
        if (ns_lookup(NS_NAME_CONSOLE, &cid, &mt, &mm) == 0 && cid)
            break;
        user_yield();
    }

    if (cid && user_portal_call(cid, (void*)hello, sizeof(hello) - 1) == 0)
        pt_print("[portal-test] console portal (ns) ... [PASS]\n");
    else
        pt_print("[portal-test] console portal (ns) ... [FAIL]\n");
}

static void test_self_portal(void)
{
    pt_print("\n[portal-test] === Test 2: self portal RPC ===\n");

    /* Spawn a user thread that owns a private portal. */
    user_proc_ctrl tc = {0};
    tc.cmd = U_THREAD_CTRL_CREATE;
    tc.priv = 1;                        /* TASK_PRIV_USER */
    tc.entry = (void*)(uptr)portal_server_thread;
    tc.param = 0;
    user_syscall(SYSCALL_PROC_THREAD, &tc, sizeof(tc));

    tc.cmd = U_THREAD_CTRL_UNBLOCK;
    user_syscall(SYSCALL_PROC_THREAD, &tc, sizeof(tc));

    while (!srv_ready)
        user_yield();

    char buf[sizeof(pt_payload)];
    for (u32 i = 0; i < sizeof(pt_payload); i++)
        buf[i] = pt_payload[i];

    int ret = user_portal_call(srv_portal_id, buf, sizeof(pt_payload) - 1);
    if (ret == PT_MAGIC)
        pt_print("[portal-test] self portal RPC ... [PASS]\n");
    else
        pt_print("[portal-test] self portal RPC ... [FAIL]\n");
}

void _start(void)
{
    pt_print("\n[portal-test] user-mode portal test starting\n");

    test_console_portal();
    test_self_portal();

    pt_print("[portal-test] all tests done, idling\n");
    for (;;)
        user_yield();
}
