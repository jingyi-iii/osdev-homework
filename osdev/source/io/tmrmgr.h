#ifndef __TMRMGR_H__
#define __TMRMGR_H__

#include <stdint.h>
#include "iodev.h"

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

class TimeDevice {
private:
    TimeDevice(void) = default;
    ~TimeDevice(void) = default;

    uint64_t mTickCount;
    uint64_t mFrequency;
    rtc_time_t mRtcTime;

    inline void OutByte(uint16_t port, uint8_t data);
    inline uint8_t InByte(uint16_t port);
    uint8_t ReadRegister(uint8_t reg);
    uint8_t BcdToBin(uint8_t bcd);
    void UpdateRtcTime(void);

public:
    IODEV_CPP_BIND_CLASS(TimeDevice);

    static TimeDevice* GetInstance(void)
    {
        static TimeDevice inst;
        return &inst;
    }

    int Init(void);
    int Read(char* buf, size_t size);
    int Write(const char* buf, size_t size);
    int Ctrl(int cmd, void* arg);
    int Shutdown(void);

    uint64_t GetTickCount(void) const { return mTickCount; }
    uint64_t GetFrequency(void) const { return mFrequency; }
    void IncrementTick(void) { mTickCount++; }

    void GetTime(rtc_time_t* time);
};

#endif
