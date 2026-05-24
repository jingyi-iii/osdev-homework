#include "drivers/kb_driver.h"
#include "drivers/log_driver.h"
#include "kernel/thread.h"

static void kb_read2(const char* data, size_t size)
{
    (void)data;
    (void)size;

    KLOG("kbdev2");
}

void timer_thread2(void)
{
    const char* ptr_msg = "Timer interrupt triggered modification:    \0";
    uint16_t* ptr_gbuf = (uint16_t*)0xb8000;
    static size_t count = 0;

    kb_register_callback(kb_read2);
    ULOG("hello ulog");

    for ( ;; ) {
        count++;
        if (count == 200000) {
            thread_block(0);
        } else if (count >= 400000) {
            thread_unblock(0);
            count = 0;
        }

        while (*ptr_msg) {
            *ptr_gbuf = (0xe << 8) | *ptr_msg;
            ptr_msg += 1;
            ptr_gbuf += 1;
        }

        *ptr_gbuf = (0xe << 8) | ((*ptr_gbuf + 1) & 0xff);
    }

    while (1);
}
