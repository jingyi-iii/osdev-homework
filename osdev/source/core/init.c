#include "init.h"
#include "kb_driver.h"
#include "log_driver.h"
#include "terminal_driver.h"

void kb_read(const char* data, size_t size)
{
    KLOG("%s", data, size);
}

void kb_read2(const char* data, size_t size)
{
    (void)data;
    (void)size;

    KLOG("kbdev2");
}

void timer_process(void)
{
    const char* ptr_msg = "Timer interrupt triggered modification: \0";
    uint16_t* ptr_gbuf = (uint16_t*)0xb8000;
    static size_t count = 0;

    kb_register_callback(kb_read);

    for ( ;; ) {
        while (*ptr_msg) {
            *ptr_gbuf = (0xe << 8) | *ptr_msg;
            ptr_msg += 1;
            ptr_gbuf += 1;
        }

        *ptr_gbuf = (0xe << 8) | ((*ptr_gbuf + 1) & 0xff);
        proc_yield();

        count++;
        if (count >= 10) {
            count = 0;
            proc_exit(2);
        }
    }

    while (1);
}

void timer_process2(void)
{
    const char* ptr_msg = "Timer interrupt triggered modification:    \0";
    uint16_t* ptr_gbuf = (uint16_t*)0xb8000;
    static size_t count = 0;

    kb_register_callback(kb_read2);
    ULOG("hello ulog");

    for ( ;; ) {
        count++;
        if (count == 200000) {
            proc_block(0);
        } else if (count >= 400000) {
            proc_unblock(0);
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

void init_process(void)
{
	terminal_flush(0);
    
    proc_create(PROC_PRIV_KERNEL, timer_process);
    proc_create(PROC_PRIV_USER, timer_process2);

    proc_exit(0);
}