#ifndef __LOGMGR_H__
#define __LOGMGR_H__

#include "list.h"
#include "module.h"
#include "iodev.h"
#include "arch_regs.h"
#include "spinlock.h"

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
    int Ctrl(int cmd, void* arg) { (void)cmd; (void)arg; return 0; }
    int Read(char* buf, size_t size) { (void)buf; (void)size; return 0; }
    int Write(const char* buf, size_t size);
};

#endif
