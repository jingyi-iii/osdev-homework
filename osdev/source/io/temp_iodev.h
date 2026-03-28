#ifndef __TEMP_IODEV_H__
#define __TEMP_IODEV_H__

#include "iodev.h"

class TempDevice {
private:
    TempDevice(void) = default;
    ~TempDevice(void) = default;

public:
    IODEV_CPP_BIND_CLASS(TempDevice);

    static TempDevice* GetInstance(void)
    {
        static TempDevice inst;
        return &inst;
    }

    int Init(void);
    int Read(char* buf, size_t size);
    int Write(const char* buf, size_t size);
    int Ctrl(int cmd, void* arg);
    int Shutdown(void);
};


#endif
