#include "logmgr.h"
#include "iodev_api.h"

int LogMgr::Init(void)
{
    mLock = spinlock_alloc();

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

int LogMgr::Shutdown(void)
{
    spinlock_release(mLock);
    return 0;
}

int LogMgr::Write(const char* buf, size_t size)
{
    spinlock_lock(mLock);
    for (size_t i = 0; i < size; i++) {
        while ((arch_inb(mComPort + 5) & 0x20) == 0);
        arch_outb(mComPort, (uint8_t)buf[i]);
    }
    spinlock_unlock(mLock);

    return 0;
}

extern "C" {
iodev* glogdev = 0;
iodev* gtmrdev = 0;
void logdev_init(void)
{
    LogMgr* logMgr = LogMgr::GetInstance();

    io_alloc_dev("logdev", &glogdev);
    if (glogdev) {
        logMgr->_bind_c_interface(glogdev);
    }

    tmrdev_init(&gtmrdev);
}

module_init(logdev_init);
}