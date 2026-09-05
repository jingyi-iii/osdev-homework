/*
 * user/ipc_bench.c — IPC ping-pong latency benchmark.
 *
 * Measures the round-trip latency of the two user-facing IPC paths with
 * the TSC (ring-3 `rdtsc` — CR4.TSD is never set, so it is allowed):
 *
 *   1. mailbox : a directed mail is bounced between two threads of THIS
 *                process (one mail object is reused; each round the echo
 *                thread replies to m->sender_tid).  This exercises the
 *                mailbox syscall gate + scheduler wakeups.
 *   2. portal  : a synchronous RPC to a CROSS-PROCESS service — the rtc
 *                server's GET_TIME (user_rtc_time) — the client blocks
 *                until the server replies.
 *
 * The TSC is calibrated against user_rtc_sleep_ms() (PIT-counter delay
 * served by the rtc server), so results are reported in microseconds.
 *
 * Needs only CAP_IPC (mailbox + portal gates).  Loaded as the last GRUB
 * module so namespace / console / log / rtc servers are already up.
 */
#include "userlib.h"

/* rdtsc is unprivileged here (CR4.TSD clear). */
static inline u64 bench_rdtsc(void)
{
    u32 lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* ---- tiny hand formatter (no libc in the freestanding link) ---- */
static char g_buf[256];
static int  g_o;

static void buf_reset(void)          { g_o = 0; }
static void buf_puts(const char* s)  { while (*s) g_buf[g_o++] = *s++; }
static void buf_u64(u64 v)
{
    char d[24];
    int n = 0;
    if (v == 0)
        d[n++] = '0';
    while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n)  g_buf[g_o++] = d[--n];
}
/* like buf_u64 but with thousands separators: 579942709 -> 579,942,709 */
static void buf_u64_sep(u64 v)
{
    char d[24];
    int n = 0;
    if (v == 0)
        d[n++] = '0';
    while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    for (int i = n - 1, p = 0; i >= 0; i--) {
        if (p && (n - p) % 3 == 0)
            g_buf[g_o++] = ',';
        g_buf[g_o++] = d[i];
        p++;
    }
}
static void buf_send(void)           { g_buf[g_o] = 0; user_log_str(g_buf); }

/* Print one result line and return the per-trip latency in microseconds. */
static u64 report(const char* name, int n, u64 sum_cyc, u64 hz)
{
    u64 avg = sum_cyc / (u64)n;             /* cycles per round trip */
    u64 us  = avg * 1000000ull / hz;        /* ... converted to us */
    u64 ms  = us / 1000;
    u64 dec = (us % 1000) / 100;            /* one decimal of the ms */

    buf_reset();
    buf_puts("[ipc-bench] "); buf_puts(name);
    buf_puts(": "); buf_u64((u64)n);
    buf_puts(" round trips, avg ");
    buf_u64_sep(avg);
    buf_puts(" cycles = ");
    buf_u64_sep(us);
    buf_puts(" us = ");
    buf_u64(ms); buf_puts("."); buf_u64(dec);
    buf_puts(" ms / trip\n");
    buf_send();
    return us;
}

/* ---- 1. mailbox ping-pong (two threads of this process) ---------- */

static void echo_thread(void)
{
    for (;;) {
        void* m = user_mail_listen();
        if (!m)
            continue;
        user_mail* mm = (user_mail*)m;
        mm->receiver_tid = mm->sender_tid;   /* bounce back to sender */
        user_mail_send(m);
    }
}

static int bench_get_tid(void)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_THREAD_CTRL_GET_TID;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
    return cfg.tid;
}

static u64 bench_mailbox(int n, int mytid)
{
    user_proc_ctrl tc = {0};

    /* Spawn the echo thread (born TS_PENDING, then unblock). */
    tc.cmd   = U_THREAD_CTRL_CREATE;
    tc.priv  = 1;                        /* TASK_PRIV_USER */
    tc.entry = (void*)(uptr)echo_thread;
    user_syscall(SYSCALL_PROC_THREAD, &tc, sizeof(tc));
    if (tc.tid <= 0)
        return 0;
    tc.cmd = U_THREAD_CTRL_UNBLOCK;      /* tc.tid filled by CREATE */
    user_syscall(SYSCALL_PROC_THREAD, &tc, sizeof(tc));

    /* One mail object is reused for every round: ownership moves
     * sender -> queue -> echo -> queue -> back to us, ref_count stays 1,
     * so no alloc/release churn in the timed loop. */
    void* m = user_mail_alloc();
    if (!m)
        return 0;
    user_mail* mm = (user_mail*)m;
    mm->magic      = 0xbeef0001;
    mm->sender_tid = mytid;
    mm->data_size  = 1;
    mm->data[0]    = 'x';

    u64 sum = 0;
    for (int i = 0; i < n; i++) {
        mm->receiver_tid = tc.tid;       /* echo thread */
        u64 t0 = bench_rdtsc();
        user_mail_send(m);
        void* r = user_mail_listen();    /* blocks until echo replies */
        u64 t1 = bench_rdtsc();
        if (r == m)
            sum += t1 - t0;
    }

    user_mail_release(m);
    return sum;
}

/* ---- 2. portal RPC ping-pong (cross-process, rtc server) --------- */

static u64 bench_portal(int n)
{
    rtc_time t;
    u64 sum = 0;

    /* Warm up: resolve "rtc" in the namespace and let the server settle. */
    if (user_rtc_time(&t) != 0)
        return 0;

    for (int i = 0; i < n; i++) {
        u64 t0 = bench_rdtsc();
        int rc = user_rtc_time(&t);
        u64 t1 = bench_rdtsc();
        if (rc == 0)
            sum += t1 - t0;
    }
    return sum;
}

/* ---- TSC calibration: cycles per second via a known PIT sleep ------ */

static u64 calibrate_hz(void)
{
    user_rtc_sleep_ms(50);                 /* settle */

    u64 t0 = bench_rdtsc();
    if (user_rtc_sleep_ms(50) != 0)
        return 0;
    u64 t1 = bench_rdtsc();

    return (t1 - t0) * 20;                 /* 50 ms -> per second */
}

void _start(void)
{
    u64 hz;

    user_log_str("[ipc-bench] IPC ping-pong latency benchmark\n");
    user_log_str("[ipc-bench] a 'round trip' = caller sends a message and\n");
    user_log_str("[ipc-bench]   waits until the reply comes back\n");
    user_log_str("[ipc-bench] units: us = microsecond = 1/1,000,000 s\n");

    hz = calibrate_hz();
    if (hz == 0) {
        user_log_str("[ipc-bench] calibration failed (rtc server not up?)\n");
        for (;;)
            user_yield();
    }

    buf_reset();
    buf_puts("[ipc-bench] TSC ~ "); buf_u64_sep(hz);
    buf_puts(" Hz (converts cycles -> us)\n");
    buf_send();

    /* Mailbox: a few rounds are enough — each involves scheduler ticks. */
    u64 mail_us = 0, portal_us = 0;
    {
        u64 cyc = bench_mailbox(8, bench_get_tid());
        if (cyc)
            mail_us = report("mailbox ping-pong (2 threads)", 8, cyc, hz);
        else
            user_log_str("[ipc-bench] mailbox bench skipped/failed\n");
    }

    /* Portal RPC to the rtc server (cross-process). */
    {
        u64 cyc = bench_portal(40);
        if (cyc)
            portal_us = report("portal rpc (rtc GET_TIME)", 40, cyc, hz);
        else
            user_log_str("[ipc-bench] portal bench skipped/failed\n");
    }

    if (mail_us && portal_us) {
        buf_reset();
        buf_puts("[ipc-bench] => portal is ~"); buf_u64(mail_us / portal_us);
        buf_puts("x faster than mailbox per round trip\n");
        buf_send();
    }

    user_log_str("[ipc-bench] done\n");

    user_thread_exit(user_thread_get_tid());
}
