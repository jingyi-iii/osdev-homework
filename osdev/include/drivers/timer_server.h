#ifndef TIMER_SERVER_H
#define TIMER_SERVER_H

#include "lib/types.h"
#include <stddef.h>
#include "drivers/platform_bus.h"

/* CMOS RTC I/O Ports */
#define CMOS_ADDR    0x70
#define CMOS_DATA    0x71

/* RTC Register Indices */
#define RTC_SECOND   0x00
#define RTC_MINUTE   0x02
#define RTC_HOUR     0x04
#define RTC_WEEKDAY  0x06
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_CENTURY  0x32

/* CMOS Status Register B - Bit definitions */
#define RTC_REG_B    0x0B
#define RTC_BCD      0x04   /* 0 = BCD mode, 1 = Binary mode */
#define RTC_24HOUR   0x02   /* 0 = 12-hour, 1 = 24-hour */

typedef struct {
    u8 second;
    u8 minute;
    u8 hour;
    u8 weekday;
    u8 day;
    u8 month;
    u8 year;
    u8 century;
} rtc_time;

/* Get current RTC time */
void timer_get_time(rtc_time* time);

/* Read time as formatted string "YYYY-MM-DD HH:MM:SS" */
/* Returns number of bytes written, or -1 on error */
int timer_read_time_str(char* buf, size_t size);

/* Returns 1 once the timer server has been started (RTC/CMOS usable) */
int timer_is_ready(void);

/* Busy-wait delay using PIT channel 2 (one-shot mode).
 * timer_delay_ms: delay in milliseconds (max ~55ms per shot, loops for longer)
 * timer_delay_us: delay in microseconds (min ~1us resolution via PIT)
 * Plain functions — callable from CPL0 and CPL3 (IOPL=3 allows direct I/O). */
void timer_delay_ms(u32 ms);
void timer_delay_us(u32 us);

/* Register the user-mode timer server (called from init_thread) */
void timer_server_init(void);

#endif /* TIMER_SERVER_H */
