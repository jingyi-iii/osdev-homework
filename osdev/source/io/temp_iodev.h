#ifndef __TEMP_IODEV_H__
#define __TEMP_IODEV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "iodev.h"

int tmpdev_init(iodev **out_dev);

#ifdef __cplusplus
}
#endif

class TempDevice {
private:
    TempDevice(void) = default;
    ~TempDevice(void) = default;
    
public:
    static TempDevice* GetInstance(void)
    {
        static TempDevice inst;
        return &inst;
    }

    int Initialize(void);
    int Read(char* buf, size_t size);
    int Write(const char* buf, size_t size);
    int Shutdown(void);
};


#endif
