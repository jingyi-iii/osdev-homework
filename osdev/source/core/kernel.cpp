#include "terminal.h"

/* Check if the compiler thinks you are targeting the wrong operating system. */
#if defined(__linux__)
#error "You are not using a cross-compiler, you will most certainly run into trouble"
#endif

/* This tutorial will only work for the 32-bit ix86 targets. */
#if !defined(__i386__)
#error "This tutorial needs to be compiled with a ix86-elf compiler"
#endif

void timer_handler(void)
{
    const char* ptr_msg = "Timer interrupt triggered modification: \0";
    uint16_t* ptr_gbuf = (uint16_t*)0xb8000;

    while (*ptr_msg) {
        *ptr_gbuf = (0xe << 8) | *ptr_msg;
        ptr_msg += 1;
        ptr_gbuf += 1;
    }

    *ptr_gbuf = (0xe << 8) | ((*ptr_gbuf + 1) & 0xff);
}

extern "C" {

void set_isr(uint16_t irq, void (*handler)());
void enable_8259a_master(uint16_t irq);

void kernel_start(void)
{
	Terminal terminal;
	terminal.Flush();
	for (int i = 0; i < 100; i++)
		terminal.Write("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

    set_isr(0x20, timer_handler);
    enable_8259a_master(0x20);
}
}

