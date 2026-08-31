#include "lib/module.h"
#include "kernel/log.h"
#include "drivers/terminal_server.h"
#include "kernel/process.h"
#include "kernel/init.h"
#include "drivers/platform_devices.h"
#include "kernel/process.h"
#include "lib/string.h"

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

/* ---- crash command for testing exception handlers ---- */
/* non-static so GCC always emits a frame pointer (required by ABI for
 * externally-callable functions, and terminal dispatches via fn pointer) */
__attribute__((noinline))
void crash_cmd(const char *args)
{
    if (!args || !args[0]) {
        terminal_write_color("crash: div0 | ud | pf | gp | bp\n", 0x0E);
        return;
    }

    if (strcmp(args, "div0") == 0) {
        terminal_write_color("Triggering #DE (Divide Error)...\n", 0x0E);
        volatile int zero = 0;
        volatile int x = 1 / zero;
        (void)x;
    } else if (strcmp(args, "ud") == 0) {
        __asm__ volatile ("ud2");
        terminal_write_color("Triggering #UD (Invalid Opcode)...\n", 0x0E);
    } else if (strcmp(args, "pf") == 0) {
        terminal_write_color("Triggering #PF (Page Fault) via NULL deref...\n", 0x0E);
        *(volatile int *)0xDEAD0000 = 0;
    } else if (strcmp(args, "gp") == 0) {
        terminal_write_color("Triggering #GP (General Protection Fault)...\n", 0x0E);
        /* write to a non-writable segment — simplest: cli + hlt in ring3
         * but we're in ring0, so use a bogus port access */
        __asm__ volatile ("cli; hlt");
    } else if (strcmp(args, "bp") == 0) {
        terminal_write_color("Triggering #BP (Breakpoint)...\n", 0x0E);
        __asm__ volatile ("int3");
    } else {
        terminal_write_color("crash: unknown type. Try: div0 ud pf gp bp\n", 0x0E);
    }
}

void kernel_start(void)
{
    /*
     * Phase 1: Set up platform bus and log early so we can trace
     * the paging bootstrap.
     */
    platform_bus_init();
    platform_devices_init();
    log_init();

    /*
     * Phase 2: Initialize paging — enables CR0.PG, sets up the kernel
     * page directory, and brings up the physical memory manager.
     * 64 MB is a safe default for QEMU; GRUB multiboot info could
     * provide the real value in a production kernel.
     */
    arch_paging_init(64 * 1024 * 1024, 0);

    /*
     * Phase 3: Rest of the kernel subsystems.
     */
    kernel_do_initcalls();
    proc_create(PROC_PRIV_KERNEL, init_thread, 0);
}

