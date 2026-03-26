#ifndef __LOGMGR_H__
#define __LOGMGR_H__

#include "list.h"
#include "module.h"
#include "iodev.h"
#include "arch_regs.h"
#include "spinlock.h"
#include "time_iodev.h"

class LogMgr {
private:
    const uint32_t mComPort = 0x3f8;
    spinlock* mLock;

    LogMgr(void) { Init(); }
    ~LogMgr(void) { Shutdown(); }
    
public:
    IODEV_CPP_BIND_CLASS(LogMgr);

    static LogMgr* GetInstance(void)
    {
        static LogMgr inst;
        return &inst;
    }

    int Init(void);
    int Shutdown(void);
    int Ctrl(int cmd, void* arg) { return 0; }
    int Read(char* buf, size_t size) { return 0; }
    int Write(const char* buf, size_t size);
};


#ifdef __cplusplus
extern "C" {
#endif
#include "string.h"

extern iodev* glogdev;
extern iodev* gtmrdev;
#define LOG_DBG(dev, fmt, ...)                                                                                      \
    do {                                                                                                            \
        if (!glogdev)                                                                                               \
            break;                                                                                                  \
        if (!dev || !dev->type || !dev->name)                                                                       \
            break;                                                                                                  \
        char log_buf[256];                                                                                          \
        char tmr_buf[32];                                                                                           \
        gtmrdev->read(gtmrdev, tmr_buf, 32);                                                                        \
        snprintf(log_buf, sizeof(log_buf), "%s [%4s] [%8s] " fmt "\n", tmr_buf, dev->type, dev->name, ##__VA_ARGS__); \
        glogdev->write(glogdev, log_buf, strlen(log_buf));                                                          \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif
