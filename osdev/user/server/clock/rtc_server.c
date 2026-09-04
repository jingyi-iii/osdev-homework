/*
 * user/server/clock/rtc_server.c — user-mode RTC / sleep server (ring-3
 * ELF).
 *
 * A namespace portal service, registered under "rtc" (like log/terminal).
 * It owns two port ranges granted by init.c's grant_rtc_caps():
 *   - CMOS RTC   {0x70, 2}: BCD time/date reads (RTC_CMD_GET_TIME)
 *   - PIT ch 0   {0x40, 4}: latched free-running counter for precise
 *                 busy-loops (RTC_CMD_SLEEP_MS)
 *
 * The PIT is never reprogrammed by the kernel (scheduler IRQ0 runs at the
 * BIOS default, divisor 65536 ≈ 18.2 Hz), so the channel-0 counter
 * free-runs at 1193180 Hz and can be latched (command 0x00 via port 0x43)
 * for atomic 16-bit samples.  One count ≈ 0.838 µs → 1 ms ≈ 1193 counts.
 * Sampling wraps are accumulated in a u32, so arbitrarily long sleeps
 * work (unlike a single modulo-65536 window).
 */
#include "userlib.h"            /* portal/namespace ABI                 */
#include "kernel/io.h"          /* ioread8/iowrite8 (user_service.c)    */
#include "server/server_msgs.h" /* rtc_request / rtc_time / RTC_CMD_*   */
#include <stddef.h>             /* size_t                               */

#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71
#define PIT_CH0    0x40
#define PIT_CMD    0x43
#define PIT_HZ     1193180UL

static u8 cmos_read(u8 reg)
{
    iowrite8(CMOS_ADDR, reg);
    return ioread8(CMOS_DATA);
}

static u8 bcd_to_bin(u8 v)
{
    return (u8)((v & 0x0F) + (v >> 4) * 10);
}

/* Atomic 16-bit sample of the free-running channel-0 counter. */
static u16 pit_latch(void)
{
    u8 lo, hi;

    iowrite8(PIT_CMD, 0x00);        /* latch command: ch 0, latched read */
    lo = ioread8(PIT_CH0);
    hi = ioread8(PIT_CH0);
    return (u16)(((u16)hi << 8) | lo);
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

/* Busy-spin on the latched PIT counter.  The counter decrements at
 * PIT_HZ; consecutive samples are far closer than one 65536 wrap (each
 * ioread8 is a syscall trap of microsecond scale), so the u32
 * accumulation handles sleeps longer than the ~55 ms wrap period. */
static void rtc_sleep(u32 ms)
{
    u32 need = ms * (PIT_HZ / 1000);
    u32 elapsed = 0;
    u16 last = pit_latch();

    while (elapsed < need) {
        u16 now = pit_latch();
        elapsed += (u16)(last - now);   /* counter decrements */
        last = now;
    }
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
