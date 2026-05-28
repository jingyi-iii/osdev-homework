#include "lib/module.h"
#include "drivers/log_driver.h"
#include "drivers/kb_driver.h"
#include "kernel/process.h"
#include "kernel/init.h"
#include "drivers/platform_devices.h"
#include "kernel/process.h"

/* Check if the compiler thinks you are targeting the wrong operating system. */
#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

extern init_call_t __start_initcall[];
extern init_call_t __stop_initcall[];
extern init_call_t __start_exitcall[];
extern init_call_t __stop_exitcall[];
static void kernel_do_initcalls(void)
{
    init_call_t *call_ptr = 0;
    
    for (call_ptr = __start_initcall; call_ptr < __stop_initcall; call_ptr++) {
        (*call_ptr)();
    }
}

static void kernel_do_exitcalls(void)
{
    init_call_t *call_ptr = 0;

    for (call_ptr = __start_exitcall; call_ptr < __stop_exitcall; call_ptr++) {
        (*call_ptr)();
    }
}

void kernel_start_noirq(void)
{
    platform_bus_init();
    platform_devices_init();

    log_init();
    kb_init();
    kernel_do_initcalls();
    p_create(PROC_PRIV_KERNEL, init_thread);
}

