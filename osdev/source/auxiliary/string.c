#include "string.h"

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p != '\0') {
        p++;
    }
    return p - s;
}

int itoa(int num, char *buffer, int base) {
    if (base < 2 || base > 36) return 0;
    
    char temp[32];
    int index = 0;
    unsigned int n;
    int is_negative = 0;
    
    if (num < 0 && base == 10) {
        is_negative = 1;
        n = -num;
    } else {
        n = num;
    }
    
    if (n == 0) {
        buffer[0] = '0';
        return 1;
    }
    
    while (n > 0) {
        int digit = n % base;
        temp[index++] = (digit < 10) ? '0' + digit : 'a' + (digit - 10);
        n /= base;
    }

    if (is_negative) {
        temp[index++] = '-';
    }

    int i = 0;
    while (index > 0) {
        buffer[i++] = temp[--index];
    }
    
    return i;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (size == 0) {
        return vsnprintf(NULL, 0, format, ap);
    }
    
    int written = 0;
    size_t remaining = size - 1;
    char *current = str;
    
    while (*format != '\0' && remaining > 0) {
        if (*format == '%') {
            format++;
            
            switch (*format) {
                case 's': {
                    const char *s = va_arg(ap, const char*);
                    if (s == NULL) s = "(null)";
                    
                    while (*s != '\0' && remaining > 0) {
                        *current++ = *s++;
                        written++;
                        remaining--;
                    }
                    break;
                }
                
                case 'd': {
                    int num = va_arg(ap, int);
                    char buffer[32];
                    int len = itoa(num, buffer, 10);
                    
                    for (int i = 0; i < len && remaining > 0; i++) {
                        *current++ = buffer[i];
                        written++;
                        remaining--;
                    }
                    break;
                }
                
                case 'c': {
                    char c = (char)va_arg(ap, int);
                    if (remaining > 0) {
                        *current++ = c;
                        written++;
                        remaining--;
                    }
                    break;
                }
                
                case '%': {
                    if (remaining > 0) {
                        *current++ = '%';
                        written++;
                        remaining--;
                    }
                    break;
                }
                
                case 'x':
                case 'u':
                default:
                    if (remaining > 0) {
                        *current++ = '%';
                        written++;
                        remaining--;
                    }
                    if (remaining > 0) {
                        *current++ = *format;
                        written++;
                        remaining--;
                    }
                    break;
            }
            format++;
        } else {
            *current++ = *format++;
            written++;
            remaining--;
        }
    }

    if (size > 0) {
        *current = '\0';
    }
    
    return written;
}

int snprintf(char *str, size_t size, const char *format, ...)
{
    va_list args;
    int result;
    
    va_start(args, format);
    result = vsnprintf(str, size, format, args);
    va_end(args);
    
    return result;
}
