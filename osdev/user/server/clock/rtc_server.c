/*
 * user/server/clock/rtc_server.c — user-mode RTC / sleep server (ring-3
 * ELF).
 *
 * A namespace portal service, registered under "rtc" (like log/terminal).
 * It owns the CMOS RTC ports granted by init.c's grant_rtc_caps():
 *   - CMOS RTC   {0x70, 2}: BCD time/date reads (RTC_CMD_GET_TIME)
 *
 * SLEEP_MS is timed with the TSC: it is invariant and keeps counting
 * while this thread is preempted, so a deadline loop is exact at any
 * PIT frequency.  The TSC rate is calibrated against the RTC's 1 Hz
 * seconds register — a divisor-independent reference, because by the
 * time this server starts the FIRST boot process (pit_ramp.elf) has
 * already raised PIT channel 0 to the final IRQ0 rate, so the old
 * "count the PIT while it is at divisor 65536" method would no longer
 * be exact (its u16 delta math only holds for a 65536-count period).
 *
 * The PIT itself is programmed ONLY by pit_ramp.elf at boot; this
 * server never touches it.
 */
#include "userlib.h"            /* portal/namespace ABI                 */
#include "kernel/io.h"          /* ioread8/iowrite8 (user_service.c)    */
#include "server/server_msgs.h" /* rtc_request / rtc_time / RTC_CMD_*   */
#include <stddef.h>             /* size_t                               */

#define CMOS_ADDR   0x70
#define CMOS_DATA   0x71
#define CMOS_SEC    0x00
#define CMOS_STATB  0x0B

static u8 cmos_read(u8 reg)
{
    iowrite8(CMOS_ADDR, reg);
    return ioread8(CMOS_DATA);
}

static u8 bcd_to_bin(u8 v)
{
    return (u8)((v & 0x0F) + (v >> 4) * 10);
}

/* Current seconds value, normalised (status-B bit 2 selects BCD/binary;
 * the calibration only needs a value that changes once per second). */
static u8 cmos_seconds(void)
{
    u8 s = cmos_read(CMOS_SEC) & 0x7F;

    return (cmos_read(CMOS_STATB) & 0x04) ? s : bcd_to_bin(s);
}

static void rtc_get_time(rtc_time* t)
{
    int binary;
    u8 sec, min, hour, day, mon, year;

    /* Wait for the update-in-progress flag to clear so the fields are
     * self-consistent (bounded: if the RTC never settles, take whatever
     * is there rather than hanging a client forever). */
    for (u32 timeout = 0; timeout < 1000000; timeout++) {
        if ((cmos_read(0x0A) & 0x80) == 0)
            break;
        __asm__ __volatile__("pause" ::: "memory");
    }

    binary = cmos_read(0x0B) & 0x04;  /* status reg B bit 2: 0 = BCD mode */

    sec  = cmos_read(0x00);
    min  = cmos_read(0x02);
    hour = cmos_read(0x04);
    day  = cmos_read(0x07);
    mon  = cmos_read(0x08);
    year = cmos_read(0x09);

    if (!binary) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = bcd_to_bin(hour);
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        year = bcd_to_bin(year);
    }

    t->year   = 2000 + year;    /* RTC gives the 2-digit year */
    t->month  = mon;
    t->day    = day;
    t->hour   = hour;
    t->minute = min;
    t->second = sec;
}

/* ---- sleep: TSC deadline, calibrated against the RTC ------------- */

static u64 rdtsc(void)
{
    u32 lo, hi;

    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

static u64 g_tsc_hz = 0;            /* 0 = not calibrated yet */

/* Calibrate the TSC against one full RTC second: wait for two
 * consecutive seconds-register edges and measure the TSC span between
 * them.  Divisor-independent (the RTC's 1 Hz is the wall clock), so it
 * stays valid after pit_ramp.elf has raised the PIT rate.  The waits
 * are bounded; on failure g_tsc_hz stays 0 and rtc_sleep degrades to a
 * coarse yield loop. */
#define CMOS_EDGE_GUARD   2000000

static void tsc_calibrate(void)
{
    u8 sec, next;
    u64 t0, t1;
    u32 guard;

    sec = cmos_seconds();
    guard = 0;
    while ((next = cmos_seconds()) == sec) {
        if (++guard > CMOS_EDGE_GUARD)
            return;                 /* RTC not ticking? give up */
    }
    t0 = rdtsc();

    sec = next;
    guard = 0;
    while ((next = cmos_seconds()) == sec) {
        if (++guard > CMOS_EDGE_GUARD)
            return;
    }
    t1 = rdtsc();

    if (t1 > t0)
        g_tsc_hz = t1 - t0;         /* TSC ticks per RTC second */
}

/* SLEEP_MS — TSC deadline loop (exact at any PIT divisor and across
 * preemption). */
static void rtc_sleep(u32 ms)
{
    u64 target;

    if (!g_tsc_hz) {
        /* Calibration unavailable (RTC stopped?): coarse fallback so a
         * client still returns rather than hanging. */
        for (u32 i = 0; i < ms; i++)
            for (u32 j = 0; j < 3000; j++)
                user_yield();
        return;
    }

    target = rdtsc() + (g_tsc_hz / 1000) * ms;
    while (rdtsc() < target)
        __asm__ __volatile__("pause" ::: "memory");
}

void _start(void)
{
    user_portal_ctrl cfg = {0};

    cfg.cmd       = U_PORTAL_CTRL_INIT;
    cfg.server_id = 0;                      /* dynamic id, registered below */
    if (user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg)) != 0) {
        for (;;)
            user_yield();
    }

    /* One-time TSC calibration against the RTC's 1 Hz seconds register
     * (~1 s; the PIT rate was already settled by pit_ramp.elf, the
     * first boot process, before this ELF was even loaded). */
    tsc_calibrate();

    /* Publish under "rtc" so clients can resolve the portal id. */
    while (ns_register(NS_NAME_RTC, (u32)(uptr)cfg.out, 0, 0) != 0)
        user_yield();

    for (;;) {
        cfg.cmd = U_PORTAL_CTRL_WAIT;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

        cfg.cmd = U_PORTAL_CTRL_GET_REQ;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
        if (!cfg.req)
            continue;

        cfg.cmd = U_PORTAL_CTRL_REPLY;
        cfg.ret = -3;                       /* malformed / unknown cmd */

        if (cfg.va && cfg.va_size >= sizeof(rtc_request)) {
            rtc_request* r = (rtc_request*)cfg.va;

            if (r->cmd == RTC_CMD_GET_TIME) {
                rtc_get_time(&r->time);
                cfg.ret = 0;
            } else if (r->cmd == RTC_CMD_SLEEP_MS) {
                rtc_sleep(r->sleep_ms);
                cfg.ret = 0;
            }
        }

        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    }
}
