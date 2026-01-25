#ifndef ARCH_UART
#define ARCH_UART

#ifdef __cplusplus
extern "C" {
#endif

void write_serial(char a);
void write_serial_string(const char* ptr_str);

#ifdef __cplusplus
}
#endif
#endif