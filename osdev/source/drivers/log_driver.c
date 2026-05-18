#include "log_driver.h"
#include "irq.h"

struct log_device {
    struct platform_device plat_dev;
    spinlock* lock;
    irq* irq;
    uint16_t io_port;
};
struct log_device* g_log_device = 0;

static void log_write(struct log_device* dev, const char* buf, size_t size)
{
    struct platform_bus_ops* ops = platform_device_get_ops(&dev->plat_dev);

    spinlock_lock(dev->lock);
    for (size_t i = 0; i < size; i++) {
        while ((ops->in_port8(dev->io_port + 5) & 0x20) == 0);
        ops->out_port8(dev->io_port, (uint8_t)buf[i]);
    }
    spinlock_unlock(dev->lock);
}

void log_handler(void* context)
{
    if (!g_log_device)
        return;

    log_data* p = (log_data*)context;
    if (p) {
        log_write(g_log_device, p->log, p->size);
    }
}

static int log_probe(struct device* dev)
{
    struct platform_device* device = to_platform_device(dev);
    struct platform_resource* irq_res = platform_device_get_resource(
        device, PLAT_RES_IRQ, 0);
    struct platform_resource* port_res = platform_device_get_resource(
        device, PLAT_RES_IO, 0);
    struct platform_bus_ops* ops = platform_device_get_ops(device);
    struct log_device* log_device = container_of(device, struct log_device, plat_dev);

    log_device->lock = spinlock_alloc();
    log_device->io_port = port_res->io.base;
    int ret = irq_request(&log_device->irq, "log",
        irq_res->irq.major, irq_res->irq.minor, log_handler, log_device);
    if (!ret) {
        irq_unmask(log_device->irq);
    }

    if (ops) {
        ops->out_port8(log_device->io_port + 1, 0x00);    // Disable all interrupts
        ops->out_port8(log_device->io_port + 3, 0x80);    // Enable DLAB (set baud rate divisor)
        ops->out_port8(log_device->io_port + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
        ops->out_port8(log_device->io_port + 1, 0x00);    //                  (hi byte)
        ops->out_port8(log_device->io_port + 3, 0x03);    // 8 bits, no parity, one stop bit
        ops->out_port8(log_device->io_port + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
        ops->out_port8(log_device->io_port + 4, 0x0B);    // IRQs enabled, RTS/DSR set
        ops->out_port8(log_device->io_port + 4, 0x1E);    // Set in loopback mode, test the serial chip
        ops->out_port8(log_device->io_port + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

        // Check if serial is faulty (i.e: not same byte as sent)
        if(ops->in_port8(log_device->io_port + 0) != 0xAE) {
            return -1;
        }

        // If serial is not faulty set it in normal operation mode
        // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
        ops->out_port8(log_device->io_port + 4, 0x0F);
    }

    g_log_device = log_device;

    return 0;
}

static int log_remove(struct device *dev)
{
    struct platform_device* device = to_platform_device(dev);
    struct log_device* log_device = container_of(device, struct log_device, plat_dev);

    irq_release(log_device->irq);
    spinlock_release(log_device->lock);

    return 0;
}

struct driver log_driver = {
    .type = "log",
    .probe = log_probe,
    .remove = log_remove,
};

struct log_device log_device = {
    .plat_dev = {
        .dev = {
            .name = "log",
            .type = "log",
        },
        .num_res = 2,
        .resources[0] = {
            .type = PLAT_RES_IRQ,
            .irq.major = 100,
            .irq.minor = 1,
        },
        .resources[1] = {
            .type = PLAT_RES_IO,
            .io.base = 0x3F8,
            .io.size = 2,   // 2 bytes
        },
    },
};

void log_init(void)
{
    platform_driver_register(&log_driver);
    platform_device_register(&log_device.plat_dev.dev);
}

void log_exit(void)
{
    platform_driver_unregister(&log_driver);
    platform_device_unregister(&log_device.plat_dev.dev);
}
