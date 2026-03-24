#include "terminal.h"
#include "module.h"
#include "process.h"
#include "logmgr.h"
#include "kbmgr.h"

/* Check if the compiler thinks you are targeting the wrong operating system. */
#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

iodev* glogdev = 0;
void kb_read(struct iodev *dev, void* data, size_t size)
{
    LOG_DBG(glogdev, "%s\n", (const char*)data, size);
}

void kb_read2(struct iodev *dev, void* data, size_t size)
{
    LOG_DBG(glogdev, "kbdev2\n");
}

void timer_handler(void)
{
    const char* ptr_msg = "Timer interrupt triggered modification: \0";
    uint16_t* ptr_gbuf = (uint16_t*)0xb8000;

    iodev* dev = 0;
    logdev_init(&dev, "timer_handler");
    glogdev = dev;

    iodev* kbdev = 0;
    kbdev_init(&kbdev, kb_read);

    iodev* kbdev2 = 0;
    kbdev_init(&kbdev2, kb_read2);

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

void timer_handler2(void)
{
    const char* ptr_msg = "Timer interrupt triggered modification:    \0";
    uint16_t* ptr_gbuf = (uint16_t*)0xb8000;

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

extern "C" {

extern init_call_t __start_initcall[];
extern init_call_t __stop_initcall[];

void kernel_do_initcalls(void)
{
    init_call_t *call_ptr = 0;
    
    for (call_ptr = __start_initcall; call_ptr < __stop_initcall; call_ptr++) {
        (*call_ptr)();
    }
}

void kernel_start(void) 
{
	Terminal terminal;
	terminal.Flush();
	for (int i = 0; i < 100; i++)
		terminal.Write("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

    kernel_do_initcalls();

    create_proc(0, timer_handler);
    create_proc(3, timer_handler2);
}
}

