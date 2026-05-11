#include "platform_device.h"
#include <stddef.h>

struct platform_resource* platform_device_get_resource(
    struct platform_device* dev, enum platform_resource_type type, int index)
{
    if (!dev)
        return NULL;

    int count = 0;
    for (int i = 0; i < dev->num_res && i < MAX_PLATFORM_RES; i++) {
        if (dev->resources[i].type == type) {
            if (count == index)
                return &dev->resources[i];
            count++;
        }
    }

    return NULL;
}

struct platform_bus_ops* platform_device_get_ops(struct platform_device* dev)
{
    if (!dev || !dev->dev.bus)
        return NULL;

    return (struct platform_bus_ops*)dev->dev.bus->bus_ops;
}