#include "drawdev.h"

class DrawDev {
private:
    DrawDev(void) = default;
    ~DrawDev(void) = default;

public:
    IODEV_CPP_BIND_CLASS(DrawDev);

    static DrawDev* GetInstance(void)
    {
        static DrawDev inst;
        return &inst;
    }

    int Init(void);
    int Read(char* buf, size_t size);
    int Write(const char* buf, size_t size);
    int Ctrl(int cmd, void* arg);
    int Shutdown(void);
};

int DrawDev::Init(void){ return 0; }
int DrawDev::Read(char* buf, size_t size){ (void)buf; (void)size; return 0; }
int DrawDev::Write(const char* buf, size_t size){ (void)buf; (void)size; return 0; }
int DrawDev::Shutdown(void){ return 0; }

int DrawDev::Ctrl(int cmd, void* arg)
{
    switch (cmd) {
    case GRAPHIC_MODE:
        break;
    case CHAR_MODE:
        break;
    }

    return 0;
}

extern "C" {

int drawdev_init(iodev **out_dev)
{
    if (!out_dev)
        return -1;

    iodev* dev = nullptr;
    DrawDev* instance = DrawDev::GetInstance();

    io_alloc_dev("temp_dev", &dev);
    if (dev) {
        instance->_bind_c_interface(dev);
    }

    *out_dev = dev;
    return (dev != nullptr) ? 0 : -1;
}

}