#include "terminal_driver.h"
#include "module.h"
#include "kb_driver.h"
#include "timer_driver.h"
#include "log_driver.h"
#include "process.h"

/* Check if the compiler thinks you are targeting the wrong operating system. */
#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

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

    kb_register_callback(kb_read);

    for ( ;; ) {
        while (*ptr_msg) {
            *ptr_gbuf = (0xe << 8) | *ptr_msg;
            ptr_msg += 1;
            ptr_gbuf += 1;
        }

        *ptr_gbuf = (0xe << 8) | ((*ptr_gbuf + 1) & 0xff);
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
            block(0);
        } else if (count >= 400000) {
            unblock(0);
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

extern init_call_t __start_initcall[];
extern init_call_t __stop_initcall[];
static void kernel_do_initcalls(void)
{
    init_call_t *call_ptr = 0;
    
    for (call_ptr = __start_initcall; call_ptr < __stop_initcall; call_ptr++) {
        (*call_ptr)();
    }
}

void kernel_start(void) 
{
    platform_bus_init();

    process_evn_setup();
    log_init();

    kernel_do_initcalls();

	terminal_flush();
	for (int i = 0; i < 100; i++)
		terminal_write("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

    create_proc(PROC_PRIV_KERNEL, timer_process);
    create_proc(PROC_PRIV_USER, timer_process2);
}

