#ifndef __STRING_H__
#define __STRING_H__

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t strlen(const char *s);
int snprintf(char *str, size_t size, const char *format, ...);
void memcpy(void *dest, const void *src, uint32_t size);
void memset(void* dest, char chr, size_t size);

#ifdef __cplusplus
}
#endif
#endif
