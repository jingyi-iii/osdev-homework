#include "timer_driver.h"
#include "irq.h"
#include "string.h"
#include "module.h"

struct timer_device {
    struct platform_device plat_dev;
    spinlock* lock;
    uint16_t cmos_addr;
    uint16_t cmos_data;
    rtc_time_t cached_time;  /* cached RTC time */
};

struct timer_device timer_device = {
    .plat_dev = {
        .dev = {
            .name = "timer",
            .type = "timer",
        },
    },
    .lock = NULL,
    .cmos_addr = 0x70,
    .cmos_data = 0x71,
    .cached_time = {0},
};

static uint8_t timer_read_reg(struct timer_device* dev, uint8_t reg)
{
    uint8_t value = 0;
    struct platform_bus_ops* ops = platform_device_get_ops(&dev->plat_dev);
    if (!ops || !ops->out_port8 || !ops->in_port8)
        return 0;

    spinlock_lock(dev->lock);
    ops->out_port8(dev->cmos_addr, reg & 0x7F);  /* NMI bit cleared */
    value = ops->in_port8(dev->cmos_data);
    spinlock_unlock(dev->lock);

    return value;
}

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return ((bcd / 16) * 10) + (bcd & 0x0F);
}

static void timer_update_rtc_time(struct timer_device* dev)
{
    uint8_t last_second, last_minute, last_hour;
    uint8_t last_day, last_month, last_year, last_century;

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
        uint8_t check_second = timer_read_reg(dev, RTC_SECOND);
        if (check_second == last_second)
            break;

        last_second = check_second;
    } while (1);

    /* Check if RTC is in BCD or binary mode */
    uint8_t reg_b = timer_read_reg(dev, RTC_REG_B);

    if (reg_b & RTC_BCD) {
        /* Binary mode */
        dev->cached_time.second  = last_second & 0x7F;
        dev->cached_time.minute  = last_minute & 0x7F;
        dev->cached_time.hour    = last_hour & 0x3F;
        dev->cached_time.day     = last_day & 0x3F;
        dev->cached_time.month   = last_month & 0x1F;
        dev->cached_time.year    = last_year & 0xFF;
        dev->cached_time.century = last_century & 0xFF;
    } else {
        /* BCD mode - convert to binary */
        dev->cached_time.second  = bcd_to_bin(last_second & 0x7F);
        dev->cached_time.minute  = bcd_to_bin(last_minute & 0x7F);
        dev->cached_time.hour    = bcd_to_bin(last_hour & 0x3F);
        dev->cached_time.day     = bcd_to_bin(last_day & 0x3F);
        dev->cached_time.month   = bcd_to_bin(last_month & 0x1F);
        dev->cached_time.year    = bcd_to_bin(last_year & 0xFF);
        dev->cached_time.century = bcd_to_bin(last_century & 0xFF);
    }

    /* Handle 12-hour format if needed */
    if (!(reg_b & RTC_24HOUR) && (dev->cached_time.hour & 0x80)) {
        /* PM - add 12 hours (except for 12 PM which stays 12) */
        if ((dev->cached_time.hour & 0x7F) != 12) {
            dev->cached_time.hour = (dev->cached_time.hour & 0x7F) + 12;
        }
    }

    /* Beijing time: +8 hours */
    dev->cached_time.hour += 8;
}

void timer_get_time(rtc_time_t* time)
{
    if (!time)
        return;
    timer_update_rtc_time(&timer_device);
    *time = timer_device.cached_time;
}

int timer_read_time_str(char* buf, size_t size)
{
    if (!buf || size == 0)
        return -1;

    timer_update_rtc_time(&timer_device);
    rtc_time_t* t = &timer_device.cached_time;

    /* Need at least 20 bytes for "YYYY-MM-DD HH:MM:SS\0" */
    if (size < 20)
        return 0;

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

    return 19;
}

static int timer_probe(struct device* dev)
{
    struct platform_device* device = to_platform_device(dev);
    struct timer_device* timer_dev = container_of(device, struct timer_device, plat_dev);

    timer_dev->lock = spinlock_alloc();
    timer_dev->cmos_addr = 0x70;
    timer_dev->cmos_data = 0x71;

    return 0;
}

static int timer_remove(struct device *dev)
{
    struct platform_device* device = to_platform_device(dev);
    struct timer_device* timer_dev = container_of(device, struct timer_device, plat_dev);

    spinlock_release(timer_dev->lock);

    return 0;
}

struct driver timer_driver = {
    .type = "timer",
    .probe = timer_probe,
    .remove = timer_remove,
};

static irq* timer_scall = NULL;

static void timer_syscall_handler(void* context)
{
    timer_syscall_data* data = (timer_syscall_data*)context;
    if (data && data->buf && data->size > 0) {
        timer_read_time_str(data->buf, data->size);
    }
}

void timer_init(void)
{
    platform_driver_register(&timer_driver);
    platform_device_register(&timer_device.plat_dev.dev);

    /* Register timer syscall for RING3 access */
    int ret = irq_request(&timer_scall, "timer_syscall", 100,
                          TIMER_SYSCALL_MINOR, timer_syscall_handler, NULL);
    if (ret == 0 && timer_scall) {
        irq_unmask(timer_scall);
    }
}

void timer_exit(void)
{
    if (timer_scall) {
        irq_release(timer_scall);
        timer_scall = NULL;
    }

    platform_driver_unregister(&timer_driver);
    platform_device_unregister(&timer_device.plat_dev.dev);
}

module_init(timer_init);
