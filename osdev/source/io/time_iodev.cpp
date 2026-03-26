#include "time_iodev.h"
#include "logmgr.h"
#include "arch_regs.h"

// Time format: "YYYY-MM-DD HH:MM:SS\n"
#define TIME_STRING_SIZE  21

/* Port I/O functions - implemented in arch layer */
extern "C" {
    void arch_outb(uint16_t port, uint8_t data);
    uint8_t arch_inb(uint16_t port);
}

uint8_t TimeDevice::ReadRegister(uint8_t reg)
{
    arch_outb(CMOS_ADDR, reg & 0x7F);  /* NMI bit cleared */
    return arch_inb(CMOS_DATA);
}

uint8_t TimeDevice::BcdToBin(uint8_t bcd)
{
    return ((bcd / 16) * 10) + (bcd & 0x0F);
}

void TimeDevice::UpdateRtcTime(void)
{
    uint8_t last_second;
    uint8_t last_minute;
    uint8_t last_hour;
    uint8_t last_day;
    uint8_t last_month;
    uint8_t last_year;
    uint8_t last_century;

    /* Read until values are consistent (avoid rollover during read) */
    do {
        last_second   = ReadRegister(RTC_SECOND);
        last_minute   = ReadRegister(RTC_MINUTE);
        last_hour     = ReadRegister(RTC_HOUR);
        last_day      = ReadRegister(RTC_DAY);
        last_month    = ReadRegister(RTC_MONTH);
        last_year     = ReadRegister(RTC_YEAR);
        last_century  = ReadRegister(RTC_CENTURY);
        
        /* Re-read second to check for consistency */
        uint8_t check_second = ReadRegister(RTC_SECOND);
        if (check_second == last_second)
            break;
            
        last_second = check_second;
    } while (1);

    /* Check if RTC is in BCD or binary mode */
    uint8_t reg_b = ReadRegister(RTC_REG_B);
    
    if (!(reg_b & RTC_BCD)) {
        /* BCD mode - convert to binary */
        mRtcTime.second = BcdToBin(last_second & 0x7F);
        mRtcTime.minute = BcdToBin(last_minute & 0x7F);
        mRtcTime.hour   = BcdToBin(last_hour & 0x3F);
        mRtcTime.day    = BcdToBin(last_day & 0x3F);
        mRtcTime.month  = BcdToBin(last_month & 0x1F);
        mRtcTime.year   = BcdToBin(last_year & 0xFF);
        mRtcTime.century = BcdToBin(last_century & 0xFF);
    } else {
        /* Binary mode */
        mRtcTime.second = last_second & 0x7F;
        mRtcTime.minute = last_minute & 0x7F;
        mRtcTime.hour   = last_hour & 0x3F;
        mRtcTime.day    = last_day & 0x3F;
        mRtcTime.month  = last_month & 0x1F;
        mRtcTime.year   = last_year & 0xFF;
        mRtcTime.century = last_century & 0xFF;
    }

    /* Handle 12-hour format if needed */
    if (!(reg_b & RTC_24HOUR) && (mRtcTime.hour & 0x80)) {
        /* PM - add 12 hours (except for 12 PM which stays 12) */
        if ((mRtcTime.hour & 0x7F) != 12) {
            mRtcTime.hour = (mRtcTime.hour & 0x7F) + 12;
        }
    }
}

int TimeDevice::Init(void)
{
    mTickCount = 0;
    mFrequency = 100;  /* Default 100 Hz tick rate */
    UpdateRtcTime();
    return 0;
}

int TimeDevice::Read(char* buf, size_t size)
{
    if (!buf || size == 0)
        return -1;

    /* Update RTC time before reading */
    UpdateRtcTime();

    /* Format: "YYYY-MM-DD HH:MM:SS\n" */
    char time_str[TIME_STRING_SIZE];
    int len = 0;
    char* p = time_str;

    /* Century and Year */
    uint8_t century = mRtcTime.century;
    uint8_t year = mRtcTime.year;
    p[0] = '0' + (century / 10);
    p[1] = '0' + (century % 10);
    p[2] = '0' + (year / 10);
    p[3] = '0' + (year % 10);
    p[4] = '-';
    
    /* Month */
    p[5] = '0' + (mRtcTime.month / 10);
    p[6] = '0' + (mRtcTime.month % 10);
    p[7] = '-';
    
    /* Day */
    p[8] = '0' + (mRtcTime.day / 10);
    p[9] = '0' + (mRtcTime.day % 10);
    p[10] = ' ';
    
    /* Hour */
    p[11] = '0' + (mRtcTime.hour / 10);
    p[12] = '0' + (mRtcTime.hour % 10);
    p[13] = ':';
    
    /* Minute */
    p[14] = '0' + (mRtcTime.minute / 10);
    p[15] = '0' + (mRtcTime.minute % 10);
    p[16] = ':';
    
    /* Second */
    p[17] = '0' + (mRtcTime.second / 10);
    p[18] = '0' + (mRtcTime.second % 10);
    p[19] = '\0';
    
    len = 19;
    
    if (len > (int)size)
        len = size;
    
    for (int i = 0; i < len; i++)
        buf[i] = time_str[i];

    return len;
}

int TimeDevice::Write(const char* buf, size_t size)
{
    /* Write operation not supported for RTC device */
    /* Could be used to set time in the future */
    (void)buf;
    (void)size;
    return 0;
}

int TimeDevice::Ctrl(int cmd, void* arg)
{
    /* Control commands can be added here for time setting */
    (void)cmd;
    (void)arg;
    return 0;
}

int TimeDevice::Shutdown(void)
{
    mTickCount = 0;
    mFrequency = 0;
    return 0;
}

void TimeDevice::GetTime(rtc_time_t* time)
{
    if (!time)
        return;
    UpdateRtcTime();
    *time = mRtcTime;
}

extern "C" {

int timedevice_init(iodev **out_dev)
{
    if (!out_dev)
        return -1;

    iodev* dev = nullptr;
    TimeDevice* instance = TimeDevice::GetInstance();

    io_alloc_dev("time_dev", &dev);
    if (dev) {
        instance->_bind_c_interface(dev);
        dev->type = "timer";
    }

    *out_dev = dev;
    return (dev != nullptr) ? 0 : -1;
}

}
