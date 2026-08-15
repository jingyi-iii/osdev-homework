#ifndef LOG_SERVER_H
#define LOG_SERVER_H

#include "stddef.h"
#include "drivers/platform_bus.h"
#include "drivers/timer_server.h"
#include "arch_irq.h"
#include "lib/string.h"

typedef struct log_data {
    const char* log;
    size_t size;
} log_data;

void log_init(void);
void log_exit(void);
void log_server_init(void);
void log_handler(void* context);

/*
 * Unified log entry point.
 *
 * LOG() is callable from CPL0 and CPL3 at any time — even before the
 * user-mode log server starts (early kernel boot): log_handler() writes
 * the serial port directly, so no device probing is required.
 *
 * Both kernel (KLOG) and user (ULOG) flows get a timestamp — but only
 * once the timer driver has been probed; before that it is skipped.
 */
#define LOG(fmt, ...)                                                                                                      \
    do {                                                                                                                    \
        char log_buf[256] = {0};                                                                                            \
        snprintf(log_buf, sizeof(log_buf), fmt "\n", ##__VA_ARGS__);                                                       \
        log_data log = {0};                                                                                                 \
        log.log = log_buf;                                                                                                  \
        log.size = strlen(log_buf);                                                                                         \
        log_handler(&log);                                                                                                  \
    } while (0)

#endif
