#ifndef PLATFORM_DEVICES_H
#define PLATFORM_DEVICES_H

#include "drivers/platform_bus.h"

/* Register all platform devices defined in the device table.
 * Drivers should already be registered before calling this,
 * so that the bus can bind them automatically. */
void platform_devices_init(void);

#endif
