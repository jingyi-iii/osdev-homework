#include <stdint.h>
#include "arch_regs.h"

#define PORT 0x3f8          // COM1

int serial_received()
{
   return arch_inb(PORT + 5) & 1;
}

char read_serial()
{
   while (serial_received() == 0);

   return arch_inb(PORT);
}

int is_transmit_empty()
{
   return arch_inb(PORT + 5) & 0x20;
}

void write_serial(char a)
{
   while (is_transmit_empty() == 0);

   arch_outb(PORT, a);
}

void write_serial_string(const char* ptr_str)
{
    while (*ptr_str != '\0') {
        write_serial(*ptr_str);
        ptr_str++;
    }
}

int arch_init_serial(void) {
    arch_outb(PORT + 1, 0x00);    // Disable all interrupts
    arch_outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    arch_outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    arch_outb(PORT + 1, 0x00);    //                  (hi byte)
    arch_outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    arch_outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    arch_outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    arch_outb(PORT + 4, 0x1E);    // Set in loopback mode, test the serial chip
    arch_outb(PORT + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

    // Check if serial is faulty (i.e: not same byte as sent)
    if(arch_inb(PORT + 0) != 0xAE) {
       return 1;
    }

    // If serial is not faulty set it in normal operation mode
    // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
    arch_outb(PORT + 4, 0x0F);

    write_serial_string("UART log enabled\n");
   
    return 0;
}
