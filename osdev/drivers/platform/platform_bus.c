/*
 * drivers/platform/platform_bus.c — direct platform server startup.
 *
 * The driver-model bus (kernel/bus.c) is gone.  Each driver server's
 * xxx_init() calls platform_user_server_start() with its own resource
 * table; this helper spawns the ring-3 server process, grants it the
 * capabilities for those resources, and lets it run drv->start().
 */

#include "drivers/platform_bus.h"
#include "kernel/process.h"
#include "kernel/capability.h"
#include "mm/heap.h"
#include "mm/paging.h"   /* PTE_USER_PAGE */
#include <stddef.h>

typedef struct user_server_param {
    struct driver* drv;
} user_server_param;

/* Server process entry: run the driver's start() once, then keep the
 * process alive (blocked forever). */
static void user_server_entry(void)
{
    user_server_param* param = (user_server_param*)thread_get_param();

    if (!param) {
        proc_exit(proc_get_pid());
        return;
    }

    struct driver* drv = param->drv;
    kfree(param);

    if (drv && drv->start)
        drv->start(0);

    /* The server stays alive for the lifetime of the system. */
    thread_block(thread_get_tid());
}

int platform_user_server_start(struct driver* drv,
                               const struct platform_resource* resources,
                               int num_res)
{
    if (!drv)
        return E_INVAL;

    user_server_param* param =
        (user_server_param*)kmalloc(sizeof(*param));
    if (!param)
        return E_NOMEM;
    param->drv = drv;

    int pid = proc_create(PROC_PRIV_USER, user_server_entry, (void*)param);
    if (pid < 0) {
        kfree(param);
        return pid;
    }

    pcb* proc = get_process_by_pid(pid);
    if (proc && resources && num_res > 0) {
        for (int i = 0; i < num_res; i++) {
            const struct platform_resource* res = &resources[i];

            /* Translate each hardware resource into a capability for the
             * user-mode driver process. */
            switch (res->type) {
            case PLAT_RES_IRQ:
                cap_grant(proc, CAP_OWN_IRQ, &res->irq.major);
                break;
            case PLAT_RES_IO: {
                cap_io_port iop = { res->io.base, res->io.size };
                cap_grant(proc, CAP_ACCESS_IO, &iop);
                break;
            }
            case PLAT_RES_MEM: {
                cap_mem mem = { res->mem.addr, res->mem.size, PTE_USER_PAGE };
                cap_grant(proc, CAP_MAP_MEM, &mem);
                break;
            }
            case PLAT_RES_IPC:
                cap_grant(proc, CAP_IPC, &res->ipc.grant);
                break;
            default:
                break;
            }
        }
    }

    /* Processes are born TS_PENDING; start the server only after its
     * capabilities are in place. */
    proc_unblock(pid);

    return pid;
}
