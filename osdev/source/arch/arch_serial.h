#ifndef ARCH_SERIAL
#define ARCH_SERIAL

#ifdef __cplusplus
extern "C" {
#endif
int arch_init_serial(void);
char arch_serial_get(void);
void arch_serial_put(char ch);
void arch_serial_write(const char* str);
#ifdef __cplusplus
}
#endif
#endif