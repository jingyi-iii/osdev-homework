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

/* Helper: output a character with padding */
static void pad_output(char **current, size_t *remaining, int *written, int width, char pad_char) {
    while (width > 0) {
        if (*remaining > 0) {
            **current = pad_char;
            (*current)++;
            (*written)++;
            (*remaining)--;
        }
        width--;
    }
}

/* Helper: output a string with padding */
static void pad_string_output(char **current, size_t *remaining, int *written, 
                              const char *s, int width, int precision) {
    int len = 0;
    const char *p = s;
    while (*p != '\0') { len++; p++; }
    
    /* Apply precision (max chars to print) */
    if (precision >= 0 && len > precision) {
        len = precision;
    }
    
    /* Calculate padding needed (right-aligned) */
    int padding = width - len;
    if (padding > 0) {
        pad_output(current, remaining, written, padding, ' ');
    }
    
    /* Output the string */
    p = s;
    while (len > 0 && *p != '\0') {
        if (*remaining > 0) {
            **current = *p;
            (*current)++;
            (*written)++;
            (*remaining)--;
        }
        p++;
        len--;
    }
}

/* Helper: convert unsigned int to hex string */
static int utox(unsigned int num, char *buffer, int uppercase) {
    char temp[16];
    int index = 0;
    
    if (num == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return 1;
    }
    
    while (num > 0) {
        int digit = num % 16;
        temp[index++] = (digit < 10) ? '0' + digit : 
                        (uppercase ? 'A' + (digit - 10) : 'a' + (digit - 10));
        num /= 16;
    }
    
    /* Reverse */
    int i = 0;
    while (index > 0) {
        buffer[i++] = temp[--index];
    }
    buffer[i] = '\0';
    return i;
}

/* Helper: convert unsigned int to decimal string */
static int utoa(unsigned int num, char *buffer) {
    char temp[32];
    int index = 0;
    
    if (num == 0) {
        buffer[0] = '0';
        return 1;
    }
    
    while (num > 0) {
        int digit = num % 10;
        temp[index++] = '0' + digit;
        num /= 10;
    }
    
    /* Reverse */
    int i = 0;
    while (index > 0) {
        buffer[i++] = temp[--index];
    }
    buffer[i] = '\0';
    return i;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    /* Handle size == 0: calculate length only */
    if (size == 0) {
        char dummy_buf[256];
        size_t dummy_remaining = 255;
        char *dummy_current = dummy_buf;
        int dummy_written = 0;
        
        const char *fmt = format;
        va_list ap_copy;
        va_copy(ap_copy, ap);
        
        while (*fmt != '\0') {
            if (*fmt == '%') {
                fmt++;
                
                /* Skip flags */
                while (*fmt == '0' || *fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '#') {
                    fmt++;
                }
                
                /* Skip width */
                while (*fmt >= '0' && *fmt <= '9') {
                    fmt++;
                }
                
                /* Skip precision */
                if (*fmt == '.') {
                    fmt++;
                    while (*fmt >= '0' && *fmt <= '9') {
                        fmt++;
                    }
                }
                
                /* Skip length modifiers */
                if (*fmt == 'l' || *fmt == 'h') {
                    fmt++;
                }
                
                /* Process specifier */
                switch (*fmt) {
                    case 's': {
                        const char *s = va_arg(ap_copy, const char*);
                        if (s == NULL) s = "(null)";
                        while (*s != '\0' && dummy_remaining > 0) {
                            dummy_current++;
                            dummy_written++;
                            dummy_remaining--;
                            s++;
                        }
                        break;
                    }
                    case 'd':
                    case 'i':
                    case 'u':
                    case 'x':
                    case 'X':
                    case 'c':
                    case 'p':
                        va_arg(ap_copy, int);
                        dummy_written += 10; /* Approximate */
                        break;
                    case '%':
                        dummy_written++;
                        break;
                }
                fmt++;
            } else {
                fmt++;
                dummy_written++;
            }
        }
        va_end(ap_copy);
        return dummy_written;
    }

    int written = 0;
    size_t remaining = size - 1;
    char *current = str;

    while (*format != '\0') {
        if (*format == '%') {
            format++;
            
            /* Parse flags */
            int flag_zeropad = 0;
            int flag_leftalign = 0;
            int flag_showsign = 0;
            int flag_altform = 0;  /* For # - alternate form (0x prefix for hex) */
            
            while (*format == '0' || *format == '-' || *format == '+' || 
                   *format == ' ' || *format == '#') {
                if (*format == '0') flag_zeropad = 1;
                else if (*format == '-') flag_leftalign = 1;
                else if (*format == '+') flag_showsign = 1;
                else if (*format == '#') flag_altform = 1;
                format++;
            }
            
            /* Parse width */
            int width = -1;
            if (*format >= '0' && *format <= '9') {
                width = 0;
                while (*format >= '0' && *format <= '9') {
                    width = width * 10 + (*format - '0');
                    format++;
                }
            }
            
            /* Parse precision */
            int precision = -1;
            if (*format == '.') {
                format++;
                precision = 0;
                while (*format >= '0' && *format <= '9') {
                    precision = precision * 10 + (*format - '0');
                    format++;
                }
            }
            
            /* Skip length modifiers (not fully implemented, just skip) */
            if (*format == 'l' || *format == 'h') {
                format++;
            }
            
            /* Process specifier */
            switch (*format) {
                case 's': {
                    const char *s = va_arg(ap, const char*);
                    if (s == NULL) s = "(null)";
                    pad_string_output(&current, &remaining, &written, s, width, precision);
                    break;
                }

                case 'd':
                case 'i': {
                    int num = va_arg(ap, int);
                    char buffer[32];
                    int is_negative = 0;
                    unsigned int abs_num;
                    
                    if (num < 0) {
                        is_negative = 1;
                        abs_num = (unsigned int)(-num);
                    } else {
                        abs_num = (unsigned int)num;
                    }
                    
                    int len = utoa(abs_num, buffer);
                    
                    /* Handle sign */
                    int sign_len = 0;
                    if (is_negative || flag_showsign) {
                        sign_len = 1;
                    }
                    
                    /* Calculate padding */
                    int total_len = len + sign_len;
                    int padding = 0;
                    
                    if (width > total_len) {
                        padding = width - total_len;
                    }
                    
                    /* Left padding (if not left-aligned and not zero-pad for numbers) */
                    if (!flag_leftalign && !flag_zeropad && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                        padding = 0;
                    }
                    
                    /* Sign */
                    if (remaining > 0) {
                        *current++ = is_negative ? '-' : (flag_showsign ? '+' : '\0');
                        if (!is_negative && !flag_showsign) current--;
                        else { written++; remaining--; }
                    }
                    
                    /* Zero padding */
                    if (flag_zeropad && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, '0');
                        padding = 0;
                    }
                    
                    /* Number */
                    for (int i = 0; i < len && remaining > 0; i++) {
                        *current++ = buffer[i];
                        written++;
                        remaining--;
                    }
                    
                    /* Right padding (if left-aligned) */
                    if (flag_leftalign && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                    }
                    break;
                }

                case 'u': {
                    unsigned int num = va_arg(ap, unsigned int);
                    char buffer[32];
                    int len = utoa(num, buffer);
                    
                    int padding = 0;
                    if (width > len) {
                        padding = width - len;
                    }
                    
                    if (!flag_leftalign && !flag_zeropad && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                        padding = 0;
                    }
                    
                    if (flag_zeropad && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, '0');
                        padding = 0;
                    }
                    
                    for (int i = 0; i < len && remaining > 0; i++) {
                        *current++ = buffer[i];
                        written++;
                        remaining--;
                    }
                    
                    if (flag_leftalign && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                    }
                    break;
                }

                case 'x':
                case 'X': {
                    unsigned int num = va_arg(ap, unsigned int);
                    char buffer[32];
                    int len = utox(num, buffer, *format == 'X');
                    
                    /* Handle alternate form (0x prefix) */
                    int prefix_len = 0;
                    if (flag_altform && num != 0) {
                        prefix_len = 2;
                    }
                    
                    int padding = 0;
                    if (width > len + prefix_len) {
                        padding = width - len - prefix_len;
                    }
                    
                    /* Left padding */
                    if (!flag_leftalign && !flag_zeropad && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                        padding = 0;
                    }
                    
                    /* Zero padding (for zero-pad flag, pad after prefix) */
                    /* For alternate form, output prefix first */
                    if (flag_altform && num != 0) {
                        if (remaining > 1) {
                            *current++ = '0';
                            *current++ = (*format == 'X') ? 'X' : 'x';
                            written += 2;
                            remaining -= 2;
                        }
                    }
                    
                    /* Zero padding for the number itself */
                    if (flag_zeropad && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, '0');
                        padding = 0;
                    }
                    
                    /* Number */
                    for (int i = 0; i < len && remaining > 0; i++) {
                        *current++ = buffer[i];
                        written++;
                        remaining--;
                    }
                    
                    /* Right padding */
                    if (flag_leftalign && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                    }
                    break;
                }

                case 'c': {
                    char c = (char)va_arg(ap, int);
                    int padding = width - 1;
                    
                    if (!flag_leftalign && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                    }
                    
                    if (remaining > 0) {
                        *current++ = c;
                        written++;
                        remaining--;
                    }
                    
                    if (flag_leftalign && padding > 0) {
                        pad_output(&current, &remaining, &written, padding, ' ');
                    }
                    break;
                }

                case 'p': {
                    void *ptr = va_arg(ap, void*);
                    unsigned int num = (unsigned int)(uintptr_t)ptr;
                    char buffer[32];
                    int len = utox(num, buffer, 0);
                    
                    /* Always output 0x for pointers */
                    if (remaining > 1) {
                        *current++ = '0';
                        *current++ = 'x';
                        written += 2;
                        remaining -= 2;
                    }
                    
                    /* Pad to 8 digits minimum for pointers */
                    int min_len = 8;
                    int padding = 0;
                    if (len < min_len) {
                        padding = min_len - len;
                    }
                    
                    if (padding > 0 && remaining > 0) {
                        pad_output(&current, &remaining, &written, padding, '0');
                    }
                    
                    for (int i = 0; i < len && remaining > 0; i++) {
                        *current++ = buffer[i];
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

                default:
                    /* Unknown specifier - output as-is */
                    if (remaining > 0) {
                        *current++ = '%';
                        written++;
                        remaining--;
                    }
                    if (remaining > 0 && *format != '\0') {
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
