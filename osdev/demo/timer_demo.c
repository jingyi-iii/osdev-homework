#include "drivers/kb_driver.h"
#include "drivers/log_driver.h"
#include "kernel/process.h"

static void kb_read(const char* data, size_t size)
{
    KLOG("%s", data, size);
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

        timer_delay_ms(1000); // Delay for 1 second
    }

    while (1);
}
