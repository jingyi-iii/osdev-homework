#include "drivers/platform_devices.h"
#include "drivers/platform_bus.h"
#include "mm/heap.h"
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

static const struct platform_device_desc device_table[] = {
    {
        .name = "keyboard",
        .type = "keyboard",
        .num_res = 1,
        .resources = {
            { .type = PLAT_RES_IRQ, .irq = { .major = 0x21, .minor = 0 } },
        },
    },
    {
        .name = "keyboard2",
        .type = "keyboard2",
        .num_res = 1,
        .resources = {
            { .type = PLAT_RES_IRQ, .irq = { .major = 0x21, .minor = 0 } },
        },
    },
    {
        .name = "timer",
        .type = "timer",
        .num_res = 0,
    },
    {
        .name = "log",
        .type = "log",
        .num_res = 2,
        .resources = {
            { .type = PLAT_RES_IRQ, .irq = { .major = 100, .minor = 1 } },
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3F8, .size = 2 } },
        },
    },
    {
        .name = "terminal",
        .type = "terminal",
        .num_res = 1,
        .resources = {
            { .type = PLAT_RES_MEM, .mem = { .addr = 0xB8000, .size = 80 * 25 * 2 } },
        },
    },
    {
        .name = "graphics",
        .type = "graphics",
        .num_res = 2,
        .resources = {
            { .type = PLAT_RES_MEM, .mem = { .addr = 0xA0000, .size = 320 * 200 } },
            { .type = PLAT_RES_IO,  .io  = { .base = 0x3D4, .size = 2 } },
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
    ((type *)((char *)(ptr)-(uintptr_t)(&((type *)0)->member)))

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
        if (!res || !dev->dev.dev_data)
            continue;

        switch (res->type) {
        case PLAT_RES_IRQ:
            pcb* proc = get_process_by_pid(*((int*)dev->dev.dev_data));
            cap_grant(proc, CAP_IRQ_OWN, &res->irq.major);
            break;
        default:
            break;
        }
    }

    return 0;
}