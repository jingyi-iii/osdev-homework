#include "terminal.h"
#include "module.h"
#include "core_api.h"
#include "iodev_api.h"

/* Check if the compiler thinks you are targeting the wrong operating system. */
#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

void kb_read(struct iodev *dev, void* data, size_t size)
{
    LOG_DBG(dev, "%s", (const char*)data, size);
}

void kb_read2(struct iodev *dev, void* data, size_t size)
{
    (void)data;
    (void)size;

    LOG_DBG(dev, "kbdev2");
}

void timer_process(void)
{
    const char* ptr_msg = "Timer interrupt triggered modification: \0";
    uint16_t* ptr_gbuf = (uint16_t*)0xb8000;

    iodev* kbdev = 0;
    kbdev_init(&kbdev, "kb_read", kb_read);

    arch_syscall(0, (void*)"hello ring0");

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

    iodev* kbdev2 = 0;
    kbdev_init(&kbdev2, "kb_read2", kb_read2);

    arch_syscall(3, (void*)"hello ring3");

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

void scall_minor0_handler(void* data)
{
    KLOG("%s", (const char*)data);
}

void scall_minor3_handler(void* data)
{
    KLOG("%s", (const char*)data);
}

extern "C" {

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
    kernel_do_initcalls();
    /* KLOG is available here */

	Terminal terminal;
	terminal.Flush();
	for (int i = 0; i < 100; i++)
		terminal.Write("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

    create_proc(PROC_PRIV_KERNEL, timer_process);
    create_proc(PROC_PRIV_USER, timer_process2);

    irqdev* scall_dev = 0;
    irqdev* scall_dev2 = 0;

    irqdev_init(&scall_dev, "minor0", 100, 0, scall_minor0_handler);
    irqdev_init(&scall_dev2, "minor3", 100, 3, scall_minor3_handler);

    if (scall_dev)
        scall_dev->unmask(scall_dev);
    if (scall_dev2)
        scall_dev2->unmask(scall_dev2);
}
}

