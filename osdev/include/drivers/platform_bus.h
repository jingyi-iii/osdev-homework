#ifndef PLATFORM_BUS_H
#define PLATFORM_BUS_H

/*
 * Platform server startup — decoupled from the driver model.
 *
 * The platform bus no longer registers drivers/devices and probes them.
 * Each driver server's xxx_init() calls platform_user_server_start()
 * directly: the helper creates a ring-3 server process, grants it the
 * hardware capabilities described by the resource array, and lets it run
 * the driver's start() entry.
 */

#include "lib/types.h"
#include "kernel/device.h"

enum platform_resource_type {
    PLAT_RES_IO,
    PLAT_RES_IRQ,
    PLAT_RES_MEM,
    PLAT_RES_IPC,
};

struct platform_resource {
    enum platform_resource_type type;
    union {
        struct {
            u16 base;
            u16 size;
        } io;

        struct {
            u32 major;
            u32 minor;
        } irq;

        struct {
            u32 addr;
            u32 size;
        } mem;

        /* Boolean IPC permission (CAP_IPC).  Grant = 1. */
        struct {
            int grant;
        } ipc;
    };
};

struct platform_bus_ops {
    int (*in_port8)(u16 port);
    int (*in_port16)(u16 port);
    int (*in_port32)(u16 port);
    int (*out_port8)(u16 port, u8 data);
    int (*out_port16)(u16 port, u16 data);
    int (*out_port32)(u16 port, u32 data);
};

/*
 * Start a user-mode driver server directly (no bus probing).
 *
 * Creates a PROC_PRIV_USER process whose entry runs drv->start(0), grants
 * it the capabilities described by @resources (I/O ports, IRQ, memory,
 * IPC) before unblocking, and returns the new pid (or a negative error).
 */
int platform_user_server_start(struct driver* drv,
                               const struct platform_resource* resources,
                               int num_res);

#endif
