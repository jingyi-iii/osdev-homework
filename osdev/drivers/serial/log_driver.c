#include "drivers/log_driver.h"
#include "kernel/irq.h"
#include "mm/heap.h"

struct log_device {
    struct platform_device* plat_dev;
    spinlock* lock;
    irq* irq;
    uint16_t io_port;
};
struct log_device* g_log_device = 0;

static void log_write(struct log_device* dev, const char* buf, size_t size)
{
    struct platform_bus_ops* ops = platform_device_get_ops(dev->plat_dev);

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

    if (!g_log_device) {
        g_log_device = (struct log_device*)kmalloc(sizeof(struct log_device));
        if (!g_log_device) {
            return -1;
        }
    }

    g_log_device->plat_dev = device;
    g_log_device->lock = spinlock_alloc();
    g_log_device->io_port = port_res->io.base;
    int ret = irq_request(&g_log_device->irq, "log",
        irq_res->irq.major, irq_res->irq.minor, log_handler, g_log_device);
    if (!ret) {
        irq_unmask(g_log_device->irq);
    }

    if (ops) {
        ops->out_port8(g_log_device->io_port + 1, 0x00);    // Disable all interrupts
        ops->out_port8(g_log_device->io_port + 3, 0x80);    // Enable DLAB (set baud rate divisor)
        ops->out_port8(g_log_device->io_port + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
        ops->out_port8(g_log_device->io_port + 1, 0x00);    //                  (hi byte)
        ops->out_port8(g_log_device->io_port + 3, 0x03);    // 8 bits, no parity, one stop bit
        ops->out_port8(g_log_device->io_port + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
        ops->out_port8(g_log_device->io_port + 4, 0x0B);    // IRQs enabled, RTS/DSR set
        ops->out_port8(g_log_device->io_port + 4, 0x1E);    // Set in loopback mode, test the serial chip
        ops->out_port8(g_log_device->io_port + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)

        // Check if serial is faulty (i.e: not same byte as sent)
        if(ops->in_port8(g_log_device->io_port + 0) != 0xAE) {
            return -1;
        }

        // If serial is not faulty set it in normal operation mode
        // (not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled)
        ops->out_port8(g_log_device->io_port + 4, 0x0F);
    }

    return 0;
}

static int log_remove(struct device *dev)
{
    if (g_log_device) {
        irq_release(g_log_device->irq);
        spinlock_release(g_log_device->lock);
        kfree(g_log_device);
        g_log_device = 0;
    }

    return 0;
}

struct driver log_driver = {
    .type = "log",
    .probe = log_probe,
    .remove = log_remove,
};

void log_init(void)
{
    platform_driver_register(&log_driver);
}

void log_exit(void)
{
    platform_driver_unregister(&log_driver);
}
