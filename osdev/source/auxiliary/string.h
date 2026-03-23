#ifndef __STRING_H__
#define __STRING_H__

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

size_t strlen(const char *s);
int snprintf(char *str, size_t size, const char *format, ...);

#endif
