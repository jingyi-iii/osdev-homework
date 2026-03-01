// #include "temp_iodev.h"

// class KeyboardDevice {
// private:
//     const char* name;

// public:
//     KeyboardDevice(const char* devname) : name(devname)
//     {

//     }

//     ~KeyboardDevice()
//     {

//     }
    
//     int init(void)
//     {
//         return 0;
//     }
    
//     int read(uint8_t* buf, size_t size)
//     {
//         return 0;
//     }
    
//     int write(const uint8_t* buf, size_t size)
//     {
//         return 0;
//     }
    
//     int shutdown()
//     {
//         return 0;
//     }
// };

// // C-style function implementations
// extern "C" {

// static int keyboard_init(struct iodev* dev) {
//     if (!dev || !dev->context)
//         return -1;

//     KeyboardDevice* kbd = static_cast<KeyboardDevice*>(dev->context);
//     return kbd->init();
// }

// static int keyboard_read(struct iodev* dev, uint8_t* buf, size_t size) {
//     if (!dev || !dev->context)
//         return -1;

//     KeyboardDevice* kbd = static_cast<KeyboardDevice*>(dev->context);
//     return kbd->read(buf, size);
// }

// static int keyboard_write(struct iodev* dev, const uint8_t* buf, size_t size) {
//     if (!dev || !dev->context)
//         return -1;

//     KeyboardDevice* kbd = static_cast<KeyboardDevice*>(dev->context);
//     return kbd->write(buf, size);
// }

// static int keyboard_shutdown(struct iodev* dev) {
//     if (!dev || !dev->context)
//         return -1;

//     KeyboardDevice* kbd = static_cast<KeyboardDevice*>(dev->context);
//     int ret = kbd->shutdown();
//     delete kbd;  // Clean up C++ object
//     dev->context = nullptr;

//     return ret;
// }

// int keyboard_init(iodev **out_dev)
// {
//     if (!out_dev) return -1;
    
//     // Create C++ device object
//     KeyboardDevice* kbd = new KeyboardDevice("keyboard0");
    
//     // Allocate C iodev structure
//     int ret = io_alloc_dev("keyboard", kbd, out_dev);
//     if (ret != 0) {
//         delete kbd;
//         return ret;
//     }
    
//     // Set function pointers
//     iodev* dev = *out_dev;
//     dev->init = keyboard_init;
//     dev->read = keyboard_read;
//     dev->write = keyboard_write;
//     dev->shutdown = keyboard_shutdown;
    
//     return 0;
// }

// } // extern "C"