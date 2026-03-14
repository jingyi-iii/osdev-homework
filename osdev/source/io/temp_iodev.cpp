#include "temp_iodev.h"

int TempDevice::Initialize(void){ return 0; }
int TempDevice::Read(char* buf, size_t size){ return 0; }
int TempDevice::Write(const char* buf, size_t size){ return 0; }
int TempDevice::Shutdown(void){ return 0; }

extern "C" {

static int dev_init(struct iodev* dev) { return 0; }
static int dev_read(struct iodev* dev, char* buf, size_t size) { return 0; }
static int dev_write(struct iodev* dev, const char* buf, size_t size) { return 0; }
static int dev_shutdown(struct iodev* dev) { return 0; }

int tmpdev_init(iodev **out_dev)
{
    if (!out_dev)
        return -1;

    iodev* dev = *out_dev;
    int ret = 0;

    ret = io_alloc_dev("temp_dev", TempDevice::GetInstance(), out_dev);
    if (ret != 0)
        return ret;
    
    dev->init = dev_init;
    dev->read = dev_read;
    dev->write = dev_write;
    dev->shutdown = dev_shutdown;
    
    return 0;
}

}