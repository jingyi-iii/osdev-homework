#ifndef LOG_DRIVER_H
#define LOG_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stddef.h"
#include "platform_bus.h"
#include "platform_device.h"
#include "timer_driver.h"

typedef struct log_data {
    const char* log;
    size_t size;
} log_data;

void log_init(void);
void log_exit(void);
void log_handler(void* context);

#define KLOG(fmt, ...)                                                                                                      \
    do {                                                                                                                    \
        char log_buf[256] = {0};                                                                                            \
        char tmr_buf[32] = {0};                                                                                             \
        timer_read_time_str(tmr_buf, sizeof(tmr_buf));                                                                      \
        snprintf(log_buf, sizeof(log_buf), "%s KLOG: " fmt "\n", tmr_buf, ##__VA_ARGS__);                                   \
        log_data log = {0};                                                                                                 \
        log.log = log_buf;                                                                                                  \
        log.size = strlen(log_buf);                                                                                         \
        log_handler(&log);                                                                                                  \
    } while (0)

#define ULOG(fmt, ...)                                                                                                      \
    do {                                                                                                                    \
        char log_buf[256] = {0};                                                                                            \
        snprintf(log_buf, sizeof(log_buf), "ULOG: " fmt "\n", ##__VA_ARGS__);                                               \
        log_data log = {0};                                                                                                 \
        log.log = log_buf;                                                                                                  \
        log.size = strlen(log_buf);                                                                                         \
        arch_syscall(1, &log);                                                                                              \
    } while (0)


#ifdef __cplusplus
}
#endif

#endif
