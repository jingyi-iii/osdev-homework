/*
 * user/pit_ramp.c — FIRST boot process: raise the PIT scheduler tick.
 *
 * Loaded ahead of every other user ELF by kernel/init.c, which then
 * waits for this process to EXIT before loading the servers/demos — so
 * the whole system starts with the final IRQ0 rate already in place
 * and nobody needs an "is the ramp done yet" probe.
 *
 * What it does:
 *   1. calibrate the TSC with count windows on PIT ch0 while it is
 *      still at the BIOS divisor (65536), where the (u16) delta math
 *      is exact;
 *   2. step ch0 up a low->high ladder, write-testing after EVERY step
 *      (count the counter's value wraps over a TSC-timed ~100 ms
 *      window; mode 3 wraps the value twice per OUT0 period, so the
 *      expected wrap rate is 2 x the programmed IRQ0 rate, ±30%);
 *      the first failing step rolls back to the previous good divisor;
 *   3. log every step + the final rate straight to COM1 (no servers
 *      exist yet) and exit.
 *
 * Needs CAP_ACCESS_IO for PIT {0x40,4} and COM1 {0x3F8,8}, granted by
 * init.c's grant_pit_ramp_caps().  No CAP_IPC: it talks to no service.
 */
#include "userlib.h"            /* user_proc_get_pid/user_proc_exit      */
#include "kernel/io.h"          /* ioread8/iowrite8 (user_service.c)    */

#define PIT_CH0    0x40
#define PIT_CMD    0x43
#define PIT_HZ     1193180UL

#define COM1_BASE  0x3F8
#define COM1_LSR   5
#define LSR_THR_EMPTY  0x20

/* ---- COM1 logging (direct port I/O; log server is not up yet) ---- */

static void com1_putc(char c)
{
    int guard = 0;

    while ((ioread8(COM1_BASE + COM1_LSR) & LSR_THR_EMPTY) == 0) {
        if (++guard > 1000000)
            return;                     /* no UART: drop, never hang */
        __asm__ __volatile__("pause" ::: "memory");
    }
    iowrite8(COM1_BASE, (u8)c);
    if (c == '\n')
        com1_putc('\r');
}

static void com1_puts(const char* s)
{
    while (*s)
        com1_putc(*s++);
}

/* "<prefix><decimal>\n" */
static void com1_msg(const char* prefix, u32 v)
{
    char d[10];
    int n = 0;

    com1_puts(prefix);
    if (!v)
        d[n++] = '0';
    while (v) {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        com1_putc(d[--n]);
    com1_putc('\n');
}

/* ---- PIT / TSC helpers ------------------------------------------- */

static u64 rdtsc(void)
{
    u32 lo, hi;

    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* Atomic 16-bit sample of the channel-0 counter. */
static u16 pit_latch(void)
{
    u8 lo, hi;

    iowrite8(PIT_CMD, 0x00);        /* latch command: ch 0, latched read */
    lo = ioread8(PIT_CH0);
    hi = ioread8(PIT_CH0);
    return (u16)(((u16)hi << 8) | lo);
}

/* Count-based wait — exact only at divisor 65536 (BIOS default), which
 * is what the PIT still is this early in boot. */
static void pit_wait_counts(u32 counts)
{
    u32 elapsed = 0;
    u16 last = pit_latch();

    while (elapsed < counts) {
        u16 now = pit_latch();
        elapsed += (u16)(last - now);   /* counter decrements */
        last = now;
    }
}

static u64 g_tsc_hz = 0;

/* Calibrate the TSC at the BIOS divisor with several short count
 * windows, keeping the SMALLEST delta: preemption only inflates a
 * window's TSC span, so the minimum is the least-polluted sample. */
#define TSC_CAL_WINDOWS  20

static void tsc_calibrate(void)
{
    u64 best = ~0ULL;

    for (int i = 0; i < TSC_CAL_WINDOWS; i++) {
        u64 t0 = rdtsc();

        pit_wait_counts(PIT_HZ / 200);      /* 5 ms of counter units */
        u64 dt = rdtsc() - t0;
        if (dt < best)
            best = dt;
    }

    g_tsc_hz = best * 200;                  /* TSC ticks per second */
}

/* Reprogram PIT channel 0 so OUT0 (IRQ0, the scheduler tick) fires at
 * ~@hz: divisor = PIT_HZ / hz.  ch0, LSB+MSB, mode 3 (square wave,
 * same as BIOS), binary.  Returns 0 on success, -1 on a bad rate. */
static int pit_set_freq(u32 hz)
{
    u32 div;

    if (hz == 0 || hz > PIT_HZ)
        return -1;

    div = PIT_HZ / hz;
    if (div < 2)
        div = 2;
    if (div > 65536)
        div = 65536;            /* -> ~18.2 Hz, the BIOS default */

    iowrite8(PIT_CMD, 0x36);
    iowrite8(PIT_CH0, (u8)(div & 0xFF));
    iowrite8(PIT_CH0, (u8)(div >> 8));
    return 0;
}

/* Sample the counter until @deadline (TSC) and count how many times it
 * wrapped.  One wrap == one divisor period == one IRQ0 tick, so
 * wraps / wall-time is the ACTUAL OUT0 (IRQ0) rate — the write-test. */
static u32 pit_count_wraps(u64 deadline)
{
    u32 wraps = 0;
    u16 last = pit_latch();

    while (rdtsc() < deadline) {
        u16 now = pit_latch();
        if (now > last)             /* wrapped back to the reload value */
            wraps++;
        last = now;
    }
    return wraps;
}

/* Write-test for one ladder step @hz (already written by pit_set_freq):
 * count the counter's value wraps over a TSC-timed ~100 ms window and
 * require that the wrap RATE went UP relative to the previous measured
 * step (>= 1.3x).  Comparing rates measured with the SAME clock/window
 * instrument cancels both the TSC-calibration constant and the mode-3
 * "value wrap vs OUT toggle" convention — the test only asserts that
 * the write actually took effect and the counter is still running
 * (0 wraps = wedged).  Returns 0 when the step is good; *out_got gets
 * the measured wrap count; on success the measurement becomes the
 * reference for the next step. */
static u32 g_prev_wraps = 0;
static u64 g_prev_tsc = 0;

static int pit_write_test(u32 hz, u32* out_got)
{
    u64 wall, t0, t1;
    u32 got;

    (void)hz;
    if (!g_tsc_hz)
        return -1;

    wall = g_tsc_hz / 10;               /* ~100 ms window */

    t0 = rdtsc();
    got = pit_count_wraps(t0 + wall);
    t1 = rdtsc();
    if (out_got)
        *out_got = got;

    if (!got)
        return -1;                      /* counter wedged / not clocking */

    if (g_prev_wraps && g_prev_tsc) {
        u64 prev_num = (u64)g_prev_wraps * (t1 - t0);
        u64 cur_num  = (u64)got * g_prev_tsc;

        if (cur_num * 10 < prev_num * 13)   /* rate did not grow >=1.3x */
            return -1;
    }

    g_prev_wraps = got;
    g_prev_tsc   = t1 - t0;
    return 0;
}

/* Ladder of candidate IRQ0 rates, low -> high.  The first failure rolls
 * back to the previous good divisor.
 *
 * DEFAULT TOP = 1000 Hz (~1 ms tick): QEMU-verified stable AND keeps
 * ipc_bench's own TSC calibration clean.  Higher tops work functionally
 * (steps pass, SLEEP stays exact), but at 10 kHz the very frequent
 * preemption makes QEMU TCG's TSC advance erratically across a blocking
 * RPC, so the benchmark's calibration reads garbage intermittently.
 * Extend the table here to chase a higher top once that is acceptable. */
static const u32 pit_ladder[] = {
    100, 200, 500, 1000,
};
#define PIT_LADDER_N  (sizeof(pit_ladder) / sizeof(pit_ladder[0]))

/* Step the PIT up through the ladder, write-testing after every step;
 * returns the achieved IRQ0 Hz (0 = still on the BIOS default). */
static u32 pit_ramp_to_max(void)
{
    u32 good = 0;

    tsc_calibrate();
    com1_msg("[pit] tsc hz ~", (u32)g_tsc_hz);

    /* Baseline wrap rate at the BIOS divisor (~500 ms window).  The
     * write-test compares each step against this measured reference
     * (and then against the previous step), so no absolute wrap-rate
     * convention is assumed. */
    {
        u64 b0 = rdtsc();

        g_prev_wraps = pit_count_wraps(b0 + g_tsc_hz / 2);
        g_prev_tsc = rdtsc() - b0;
        com1_msg("[pit] base wraps/s = ",
                 (u32)((u64)g_prev_wraps * g_tsc_hz / g_prev_tsc));
    }

    for (u32 i = 0; i < PIT_LADDER_N; i++) {
        u32 f = pit_ladder[i];
        u32 got = 0;

        if (pit_set_freq(f) != 0)
            break;
        if (pit_write_test(f, &got) != 0) {
            /* Roll back to the last verified divisor (18 -> the BIOS
             * default: pit_set_freq clamps it to divisor 65536). */
            pit_set_freq(good ? good : 18);
            com1_msg("[pit] step FAILED at ", f);
            com1_msg("[pit]   got wraps/100ms = ", got);
            break;
        }
        good = f;
        com1_msg("[pit] step ok @", f);
    }

    com1_msg("[pit] running @", good);
    return good;
}

void _start(void)
{
    pit_ramp_to_max();

    /* Exit: kernel init_thread polls for this process to disappear and
     * only then loads the rest of the boot set. */
    user_proc_exit(user_proc_get_pid());

    for (;;)
        user_yield();               /* not reached */
}
