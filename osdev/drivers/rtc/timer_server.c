#include "drivers/timer_server.h"
#include "drivers/log_server.h"
#include "user/uspinlock.h"
#include "kernel/process.h"
#include "kernel/io.h"

struct timer_device {
    uspinlock lock;
    u16 cmos_addr;
    u16 cmos_data;
    rtc_time cached_time;  /* cached RTC time */
    int ready;               /* set once the user-mode server has started */
};

struct timer_device timer_device = {
    .lock = USPINLOCK_INIT,
    .cmos_addr = 0x70,
    .cmos_data = 0x71,
    .cached_time = {0},
    .ready = 0,
};

static u8 timer_read_reg(struct timer_device* dev, u8 reg)
{
    u8 value = 0;

    /* Locking is done by the callers (timer_get_time / timer_read_time_str
     * hold dev->lock across the whole cached_time update) so the register
     * reads are consistent and the cached copy cannot tear. */
    iowrite8(dev->cmos_addr, reg & 0x7F);  /* NMI bit cleared */
    value = ioread8(dev->cmos_data);

    return value;
}

static u8 bcd_to_bin(u8 bcd)
{
    return ((bcd / 16) * 10) + (bcd & 0x0F);
}

static void timer_update_rtc_time(struct timer_device* dev)
{
    u8 last_second, last_minute, last_hour;
    u8 last_day, last_month, last_year, last_century;

    /* Read until values are consistent (avoid rollover during read) */
    do {
        last_second  = timer_read_reg(dev, RTC_SECOND);
        last_minute  = timer_read_reg(dev, RTC_MINUTE);
        last_hour    = timer_read_reg(dev, RTC_HOUR);
        last_day     = timer_read_reg(dev, RTC_DAY);
        last_month   = timer_read_reg(dev, RTC_MONTH);
        last_year    = timer_read_reg(dev, RTC_YEAR);
        last_century = timer_read_reg(dev, RTC_CENTURY);

        /* Re-read second to check for consistency */
        u8 check_second = timer_read_reg(dev, RTC_SECOND);
        if (check_second == last_second)
            break;

        last_second = check_second;
    } while (1);

    /* Check if RTC is in BCD or binary mode */
    u8 reg_b = timer_read_reg(dev, RTC_REG_B);
    int is_pm = 0;

    if (reg_b & RTC_BCD) {
        /* Binary mode */
        dev->cached_time.second  = last_second & 0x7F;
        dev->cached_time.minute  = last_minute & 0x7F;
        /* In 12-hour mode bit 7 of the hours register is the PM flag;
         * capture it before masking the hour value. */
        is_pm = (last_hour & 0x80) ? 1 : 0;
        dev->cached_time.hour    = last_hour & 0x3F;
        dev->cached_time.day     = last_day & 0x3F;
        dev->cached_time.month   = last_month & 0x1F;
        dev->cached_time.year    = last_year & 0xFF;
        dev->cached_time.century = last_century & 0xFF;
    } else {
        /* BCD mode - convert to binary */
        dev->cached_time.second  = bcd_to_bin(last_second & 0x7F);
        dev->cached_time.minute  = bcd_to_bin(last_minute & 0x7F);
        is_pm = (last_hour & 0x80) ? 1 : 0;
        dev->cached_time.hour    = bcd_to_bin(last_hour & 0x3F);
        dev->cached_time.day     = bcd_to_bin(last_day & 0x3F);
        dev->cached_time.month   = bcd_to_bin(last_month & 0x1F);
        dev->cached_time.year    = bcd_to_bin(last_year & 0xFF);
        dev->cached_time.century = bcd_to_bin(last_century & 0xFF);
    }

    /* Convert 12-hour format to 24-hour when the RTC is in 12-hour mode.
     * 12 PM stays 12, 12 AM becomes 0, other PM hours get +12. */
    if (!(reg_b & RTC_24HOUR)) {
        if (is_pm) {
            if (dev->cached_time.hour != 12)
                dev->cached_time.hour += 12;
        } else {
            if (dev->cached_time.hour == 12)
                dev->cached_time.hour = 0;
        }
    }

    /* Beijing time: +8 hours (UTC+8), wrapping past midnight with
     * full day/month/year rollover. */
    dev->cached_time.hour += 8;
    if (dev->cached_time.hour >= 24) {
        static const u8 mdays[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
        int month;
        int dim;

        dev->cached_time.hour -= 24;
        dev->cached_time.day++;

        month = (dev->cached_time.month >= 1 && dev->cached_time.month <= 12)
                ? dev->cached_time.month : 1;
        dim = mdays[month - 1];
        if (month == 2 && (dev->cached_time.year % 4) == 0)
            dim = 29;   /* leap year (adequate for 2-digit years) */

        if (dev->cached_time.day > (u8)dim) {
            dev->cached_time.day = 1;
            dev->cached_time.month++;
            if (dev->cached_time.month > 12) {
                dev->cached_time.month = 1;
                dev->cached_time.year++;
            }
        }
    }
}

void timer_get_time(rtc_time* time)
{
    if (!time)
        return;

    uspin_lock(&timer_device.lock);
    timer_update_rtc_time(&timer_device);
    *time = timer_device.cached_time;
    uspin_unlock(&timer_device.lock);
}

int timer_read_time_str(char* buf, size_t size)
{
    if (!buf || size == 0)
        return E_INVAL;

    /* Need at least 20 bytes for "YYYY-MM-DD HH:MM:SS\0" */
    if (size < 20)
        return -1;

    uspin_lock(&timer_device.lock);
    timer_update_rtc_time(&timer_device);
    rtc_time* t = &timer_device.cached_time;
    buf[0]  = '0' + (t->century / 10);
    buf[1]  = '0' + (t->century % 10);
    buf[2]  = '0' + (t->year / 10);
    buf[3]  = '0' + (t->year % 10);
    buf[4]  = '-';
    buf[5]  = '0' + (t->month / 10);
    buf[6]  = '0' + (t->month % 10);
    buf[7]  = '-';
    buf[8]  = '0' + (t->day / 10);
    buf[9]  = '0' + (t->day % 10);
    buf[10] = ' ';
    buf[11] = '0' + (t->hour / 10);
    buf[12] = '0' + (t->hour % 10);
    buf[13] = ':';
    buf[14] = '0' + (t->minute / 10);
    buf[15] = '0' + (t->minute % 10);
    buf[16] = ':';
    buf[17] = '0' + (t->second / 10);
    buf[18] = '0' + (t->second % 10);
    buf[19] = '\0';
    uspin_unlock(&timer_device.lock);

    return 19;
}

/* True once timer_server_start() has run */
int timer_is_ready(void)
{
    return timer_device.ready;
}

/* ---- PIT (8253/8254) based delay support ---- */

#define PIT_CHANNEL2    0x42
#define PIT_COMMAND     0x43
#define PIT_PPI_PORT    0x61

/* PIT base frequency: 1.193182 MHz */
#define PIT_FREQUENCY   1193182

/* Read-back command: latch status for channel 2 (bits 7-6=11, bit 5=0, bit 4=1, bit 3=1) */
#define PIT_RB_CH2_STATUS  0xEC

/*
 * Program PIT channel 2 for a one-shot delay of `ticks` PIT cycles.
 * Uses mode 0 (interrupt on terminal count) and polls the OUT pin
 * via the read-back status command.  Port I/O goes through the io layer
 * (kernel/io.c), so it works at both CPL0 and CPL3.
 */
static void pit_delay_ticks(u16 ticks)
{
    /* Enable PIT channel 2 gate via PPI port B (bit 0).
     * Save original state so we can restore it. */
    u8 ppi_save = (u8)ioread8(PIT_PPI_PORT);
    iowrite8(PIT_PPI_PORT, ppi_save | 0x01);

    /* Program channel 2: mode 0 (one-shot), binary, lo/hi bytes */
    iowrite8(PIT_COMMAND, 0xB0);
    iowrite8(PIT_CHANNEL2, ticks & 0xFF);
    iowrite8(PIT_CHANNEL2, (ticks >> 8) & 0xFF);

    /* Poll OUT pin via read-back status until terminal count is reached.
     * In mode 0, OUT goes high when the counter reaches 0 and stays high. */
    do {
        iowrite8(PIT_COMMAND, PIT_RB_CH2_STATUS);
    } while (!(ioread8(PIT_CHANNEL2) & 0x80));

    /* Restore PPI port B to original state */
    iowrite8(PIT_PPI_PORT, ppi_save);
}

void timer_delay_ms(u32 ms)
{
    if (ms == 0)
        return;

    /* ticks = frequency * ms / 1000,  keep within u32 range */
    u32 ticks = (u32)(((u64)PIT_FREQUENCY * ms) / 1000);

    while (ticks > 0) {
        u16 chunk;
        if (ticks > 65535) {
            chunk = 65535;
            ticks -= 65535;
        } else {
            chunk = (u16)ticks;
            ticks = 0;
        }
        pit_delay_ticks(chunk);
    }
}

void timer_delay_us(u32 us)
{
    if (us == 0)
        return;

    /* ticks = frequency * us / 1000000, minimum 1 tick (~0.84 us) */
    u32 ticks = (u32)(((u64)PIT_FREQUENCY * us) / 1000000);
    if (ticks == 0)
        ticks = 1;

    while (ticks > 0) {
        u16 chunk;
        if (ticks > 65535) {
            chunk = 65535;
            ticks -= 65535;
        } else {
            chunk = (u16)ticks;
            ticks = 0;
        }
        pit_delay_ticks(chunk);
    }
}

/* ======================= user-mode timer server ======================= */

static void timer_server_loop(void)
{
    /* All timer_* APIs are plain functions callable from CPL0 and CPL3
     * (port I/O via the io layer, kernel/io.c), so this server thread
     * simply idles, keeping the server process alive. */
    for (;;)
        thread_yield();
}

int timer_server_start(struct device* dev)
{
    (void)dev;

    /* The lock is embedded (USPINLOCK_INIT); the server needs no kernel
     * lock object. */
    timer_device.cmos_addr = 0x70;
    timer_device.cmos_data = 0x71;
    timer_device.ready = 1;

    LOG("timer_server started");

    timer_server_loop();   /* never returns */
    return 0;
}

int timer_server_stop(struct device* dev)
{
    (void)dev;
    timer_device.ready = 0;
    return 0;
}

static struct driver timer_server = {
    .class = DRIVER_CLASS_USER,
    .type  = "timer",
    .start = timer_server_start,
    .stop  = timer_server_stop,
};

/* Hardware resources the timer server process needs. */
static const struct platform_resource timer_server_resources[] = {
    { .type = PLAT_RES_IO, .io = { .base = 0x40, .size = 4 } },   /* PIT channels + command 0x40-0x43 */
    { .type = PLAT_RES_IO, .io = { .base = 0x61, .size = 1 } },   /* PPI port B (PIT gate) */
    { .type = PLAT_RES_IO, .io = { .base = 0x70, .size = 2 } },   /* CMOS address + data 0x70-0x71 */
    { .type = PLAT_RES_IO, .io = { .base = 0x3F8, .size = 8 } },  /* COM1: ring-3 LOG() output */
};

/* Start the user-mode timer server directly (no platform bus probing).
 * Called from init_thread() once the scheduler is up (like terminal_init
 * / log_server_init). */
void timer_server_init(void)
{
    platform_user_server_start(&timer_server, timer_server_resources,
                               sizeof(timer_server_resources) /
                               sizeof(timer_server_resources[0]));
}
