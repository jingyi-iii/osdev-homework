#include "logmgr.h"
#include "list.h"

class LogMgr {
private:
    const uint32_t mComPort = 0x3f8;
    spinlock* mLock;
    list_node mDevList;

    LogMgr(void) { Initialize(); }
    ~LogMgr(void) { Uninitialize(); }
    
public:
    static LogMgr* GetInstance(void)
    {
        static LogMgr inst;
        return &inst;
    }

    int Initialize(void)
    {
        mLock = spinlock_alloc();
        list_init(&mDevList);

        arch_outb(mComPort + 1, 0x00);    // Disable all interrupts
        arch_outb(mComPort + 3, 0x80);    // Enable DLAB (set baud rate divisor)
        arch_outb(mComPort + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
        arch_outb(mComPort + 1, 0x00);    //                  (hi byte)
        arch_outb(mComPort + 3, 0x03);    // 8 bits, no parity, one stop bit
        arch_outb(mComPort + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
        arch_outb(mComPort + 4, 0x0B);    // IRQs enabled, RTS/DSR set
        arch_outb(mComPort + 4, 0x1E);    // Set in loopback mode, test the serial chip
        arch_outb(mComPort + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

        // Check if serial is faulty (i.e: not same byte as sent)
        if(arch_inb(mComPort + 0) != 0xAE) {
            return -1;
        }

        // If serial is not faulty set it in normal operation mode
        // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
        arch_outb(mComPort + 4, 0x0F);
        return 0;
    }

    void Uninitialize(void)
    {
        spinlock_lock(mLock);
        list_for_each(pos, &mDevList) {
            iodev* dev = list_entry(pos, iodev, dev_node);
            io_free_dev(dev);
        }

        spinlock_unlock(mLock);

        if (mLock) {
            spinlock_free(mLock);
            mLock = 0;
        }
    }

    int Write(const char* buf, size_t size)
    {
        spinlock_lock(mLock);
        for (size_t i = 0; i < size; i++) {
            while ((arch_inb(mComPort + 5) & 0x20) == 0);
            arch_outb(mComPort, (uint8_t)buf[i]);
        }
        spinlock_unlock(mLock);

        return 0;
    }

    int AddDevice(iodev* dev)
    {
        if (!dev)
            return -1;
        spinlock_lock(mLock);
        list_add(&dev->dev_node, &mDevList);
        spinlock_unlock(mLock);
        return 0;
    }
};

extern "C" {

static int dev_init(struct iodev* dev) { return 0; }
static int dev_read(struct iodev* dev, char* buf, size_t size) { return 0; }
static int dev_shutdown(struct iodev* dev) { return 0; }

static int dev_write(struct iodev* dev, const char* buf, size_t size)
{
    if (!dev || !dev->context)
        return -1;

    LogMgr* sdev = static_cast<LogMgr*>(dev->context);
    return sdev->Write(buf, size);
}

int logdev_init(iodev **out_dev, const char* name)
{
    if (!out_dev)
        return -1;

    LogMgr* logMgr = LogMgr::GetInstance();

    int ret = io_alloc_dev(name, logMgr, out_dev);
    if (ret != 0)
        return ret;
    
    iodev* dev = *out_dev;
    dev->init = dev_init;
    dev->read = dev_read;
    dev->write = dev_write;
    dev->shutdown = dev_shutdown;

    logMgr->AddDevice(dev);
    
    return 0;
}

}