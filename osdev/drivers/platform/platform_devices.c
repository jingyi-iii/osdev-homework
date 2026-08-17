#include "drivers/platform_devices.h"
#include "drivers/platform_bus.h"
#include "mm/heap.h"
#include "mm/paging.h"
#include "lib/string.h"
#include "kernel/capability.h"
#include "kernel/process.h"

/************************************************************************/
/*                Platform Device Descriptor Table                      */
/*                                                                      */
/* All hardware device resources are defined here, separate from the    */
/* driver logic. Each entry describes a device's name, type, and the    */
/* resources (IRQ, I/O ports, memory regions) it uses.                  */
/*                                                                      */
/* platform_devices_init() iterates this table, creates platform_device */
/* instances on the heap, and registers them on the platform bus. The   */
/* bus then matches them to registered drivers by type name.            */
/************************************************************************/

#define MAX_DESC_RES   8

struct platform_device_desc {
    const char *name;
    const char *type;
    int num_res;
    struct platform_resource resources[MAX_DESC_RES];
};

/*
 * I/O port ranges below become CAP_ACCESS_IO grants for the matching
 * user-mode driver server (see platform_device_grant_capabilities).
 * They must cover every port the server's code touches through
 * ioread*()/iowrite*() (kernel/io.c enforces the grant at the syscall gate).
 */
static const struct platform_device_desc device_table[] = {
    {
        .name = "keyboard",
        .type = "keyboard",
        .num_res = 4,
        .resources = {
            { .type = PLAT_RES_IRQ, .irq = { .major = 0x21, .minor = 0 } },
            { .type = PLAT_RES_IO,  .io  = { .base = 0x60, .size = 5 } },  /* PS/2 data + status/command 0x60-0x64 */
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3F8, .size = 8 } },  /* COM1: ring-3 LOG() output */
            { .type = PLAT_RES_IPC, .ipc = { .grant = 1 } },               /* CAP_IPC: IRQ->mailbox delivery (mailbox_listen) */
        },
    },
    {
        .name = "keyboard2",
        .type = "keyboard2",
        .num_res = 4,
        .resources = {
            { .type = PLAT_RES_IRQ, .irq = { .major = 0x21, .minor = 0 } },
            { .type = PLAT_RES_IO,  .io  = { .base = 0x60, .size = 5 } },
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3F8, .size = 8 } },
            { .type = PLAT_RES_IPC, .ipc = { .grant = 1 } },
        },
    },
    {
        .name = "timer",
        .type = "timer",
        .num_res = 4,
        .resources = {
            { .type = PLAT_RES_IO, .io = { .base = 0x40, .size = 4 } },   /* PIT channels + command 0x40-0x43 */
            { .type = PLAT_RES_IO, .io = { .base = 0x61, .size = 1 } },   /* PPI port B (PIT gate) */
            { .type = PLAT_RES_IO, .io = { .base = 0x70, .size = 2 } },   /* CMOS address + data 0x70-0x71 */
            { .type = PLAT_RES_IO, .io = { .base = 0x3F8, .size = 8 } },  /* COM1: ring-3 LOG() output */
        },
    },
    {
        .name = "log",
        .type = "log",
        .num_res = 1,
        .resources = {
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3F8, .size = 8 } },  /* COM1 0x3F8-0x3FF */
        },
    },
    {
        .name = "terminal",
        .type = "terminal",
        .num_res = 2,
        .resources = {
            { .type = PLAT_RES_MEM, .mem = { .addr = 0xB8000, .size = 80 * 25 * 2 } },
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3C0, .size = 32 } },  /* VGA 0x3C0-0x3DF (seq/gc/ac/dac/crt/status) */
        },
    },
    {
        .name = "graphics",
        .type = "graphics",
        .num_res = 3,
        .resources = {
            { .type = PLAT_RES_MEM, .mem = { .addr = 0xA0000, .size = 320 * 200 } },
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3C0, .size = 32 } },  /* VGA 0x3C0-0x3DF */
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3F8, .size = 8 } },  /* COM1: ring-3 LOG() output */
        },
    },
};

static const int device_count = sizeof(device_table) / sizeof(device_table[0]);

void platform_devices_init(void)
{
    for (int i = 0; i < device_count; i++) {
        const struct platform_device_desc *desc = &device_table[i];

        struct platform_device *pdev;
        pdev = (struct platform_device*)kmalloc(sizeof(struct platform_device));
        if (!pdev)
            continue;

        memset(pdev, 0, sizeof(*pdev));
        pdev->dev.name = desc->name;
        pdev->dev.type = desc->type;
        pdev->num_res = desc->num_res;

        for (int j = 0; j < desc->num_res; j++) {
            pdev->resources[j] = desc->resources[j];
        }

        platform_device_register(&pdev->dev);
    }
}

#define PLATFORM_DEV_OF(ptr, type, member) \
    ((type *)((char *)(ptr)-(uptr)(&((type *)0)->member)))

struct platform_device* get_platform_device(struct device* pdev)
{
    return PLATFORM_DEV_OF(pdev, struct platform_device, dev);
}

int platform_device_grant_capabilities(struct platform_device* dev)
{
    if (!dev)
        return E_INVAL;

    for (int i = 0; i < dev->num_res; i++) {
        struct platform_resource* res = &dev->resources[i];
        if (!res || !dev->server_pid)
            continue;

        pcb* proc = get_process_by_pid(dev->server_pid);
        if (!proc)
            continue;

        /* Translate each hardware resource into a capability for the
         * user-mode driver process. */
        switch (res->type) {
        case PLAT_RES_IRQ:
            cap_grant(proc, CAP_OWN_IRQ, &res->irq.major);
            break;
        case PLAT_RES_IO:
            cap_io_port iop = { res->io.base, res->io.size };
            cap_grant(proc, CAP_ACCESS_IO, &iop);
            break;
        case PLAT_RES_MEM:
            cap_mem mem = { res->mem.addr, res->mem.size, PTE_USER_PAGE };
            cap_grant(proc, CAP_MAP_MEM, &mem);
            break;
        case PLAT_RES_IPC:
            /* The server needs the mailbox IPC service (e.g. kb_server
             * receives IRQ mails via mailbox_listen). */
            cap_grant(proc, CAP_IPC, &res->ipc.grant);
            break;
        default:
            break;
        }
    }

    return 0;
}