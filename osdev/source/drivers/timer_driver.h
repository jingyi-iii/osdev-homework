#ifndef TIMER_DRIVER_H
#define TIMER_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include "platform_bus.h"
#include "platform_device.h"

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
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    uint8_t century;
} rtc_time_t;

/* Timer syscall minor number for RING3 access */
#define TIMER_SYSCALL_MINOR     (2)

/* Data structure for timer syscall */
typedef struct timer_syscall_data {
    char* buf;      /* Buffer to write time string "YYYY-MM-DD HH:MM:SS" */
    size_t size;    /* Size of buffer */
} timer_syscall_data;

void timer_init(void);
void timer_exit(void);

/* Get current RTC time */
void timer_get_time(rtc_time_t* time);

/* Read time as formatted string "YYYY-MM-DD HH:MM:SS" */
/* Returns number of bytes written, or -1 on error */
int timer_read_time_str(char* buf, size_t size);

#endif
