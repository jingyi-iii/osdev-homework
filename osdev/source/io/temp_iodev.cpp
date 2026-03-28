#include "temp_iodev.h"

int TempDevice::Init(void){ return 0; }
int TempDevice::Read(char* buf, size_t size){ (void)buf; (void)size; return 0; }
int TempDevice::Write(const char* buf, size_t size){ (void)buf; (void)size; return 0; }
int TempDevice::Ctrl(int cmd, void* arg){ (void)cmd; (void)arg; return 0; }
int TempDevice::Shutdown(void){ return 0; }

extern "C" {

int tmpdev_init(iodev **out_dev)
{
    if (!out_dev)
        return -1;

    iodev* dev = nullptr;
    TempDevice* instance = TempDevice::GetInstance();

    io_alloc_dev("temp_dev", &dev);
    if (dev) {
        instance->_bind_c_interface(dev);
    }

    *out_dev = dev;
    return (dev != nullptr) ? 0 : -1;
}

}