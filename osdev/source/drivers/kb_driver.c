#include "kb_driver.h"
#include "logmgr.h"

static int kb_probe(struct device *dev)
{
    ULOG("kb_probe");

    struct platform_device* plat_dev = platform_get_device(dev);
    struct platform_bus_ops* ops = platform_device_get_ops(plat_dev);
    
    if (ops) {
        ops->in_port8(0);
    }

    return 0;
}

static int kb_remove(struct device *dev)
{
    ULOG("kb_remove");
    return 0;
}

struct driver kb_driver = {
    .name = "kb",
    .type = "kb",
    .probe = kb_probe,
    .remove = kb_remove,
};

struct platform_device kb_device = {
    .dev = {
        .name = "kb",
        .type = "kb",
    },
    .num_res = 0,
};

void kb_init(void)
{
    platform_driver_register(&kb_driver);
    platform_device_register(&kb_device.dev);
}

void kb_exit(void)
{
    ULOG("kb_exit");
}
