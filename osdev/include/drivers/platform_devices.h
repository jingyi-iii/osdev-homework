#ifndef PLATFORM_DEVICES_H
#define PLATFORM_DEVICES_H

#include "drivers/platform_bus.h"

/* Register all platform devices defined in the device table.
 * Drivers should already be registered before calling this,
 * so that the bus can bind them automatically. */
void platform_devices_init(void);
struct platform_device* get_platform_device(struct device* pdev);
int platform_device_grant_capabilities(struct platform_device* dev);

#endif
