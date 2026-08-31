#ifndef LOG_SERVER_H
#define LOG_SERVER_H

#include "stddef.h"
#include "kernel/log.h"            /* LOG() + log_init() — pure port I/O */
#include "drivers/platform_bus.h"

typedef struct log_data {
    const char* log;
    size_t size;
} log_data;

void log_exit(void);
void log_server_init(void);
void log_handler(void* context);

/*
 * LOG() now lives in kernel/log.h: it formats the message and writes it
 * straight to the COM1 port (klog for ring 0, io-layer for ring 3) with
 * no timestamp and no user-mode log server involved.  log_handler() above
 * is kept only for the platform-bus log server driver (drivers/serial/
 * log_server.c), which no longer participates in the LOG() path.
 */

#endif
