#include "drivers/log_server.h"
#include "sync/spinlock.h"
#include "kernel/process.h"
#include "kernel/io.h"

#define SERIAL_COM1_BASE   0x3F8
#define SERIAL_LSR_OFF     5      /* Line Status Register offset  */
#define LSR_THR_EMPTY      0x20   /* Transmitter Holding Register empty */

struct log_device {
    spinlock* lock;
    u16 io_port;
    int ready;
};

static struct log_device log_device = {
    .lock    = NULL,
    .io_port = SERIAL_COM1_BASE,
    .ready   = 0,
};

/* ---- serial port init (port I/O via the io layer, no probing) ------- */
static void serial_port_init(u16 port)
{
    iowrite8(port + 1, 0x00);    /* Disable all interrupts          */
    iowrite8(port + 3, 0x80);    /* Enable DLAB (baud rate divisor) */
    iowrite8(port + 0, 0x03);    /* Divisor lo = 3  → 38400 baud    */
    iowrite8(port + 1, 0x00);    /* Divisor hi                      */
    iowrite8(port + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    iowrite8(port + 2, 0xC7);    /* Enable FIFO, 14-byte threshold  */
    iowrite8(port + 4, 0x0B);    /* IRQs enabled, RTS/DSR set       */
    iowrite8(port + 4, 0x0F);    /* Normal operation (not loopback) */
}

/* ---- direct serial write ----------------------------------------------
 * Works in every context: early kernel boot (before the log server has
 * started), CPL0 and CPL3 (port I/O goes through the io layer, kernel/io.c,
 * which routes ring-3 access through the syscall gate).  spinlock_lock(NULL)
 * is a safe no-op, so this is fine even before the lock is allocated (very
 * early boot is single-threaded). */
static void log_write_direct(const char* buf, size_t size)
{
    spinlock_lock(log_device.lock);
    for (size_t i = 0; i < size; i++) {
        while ((ioread8(log_device.io_port + SERIAL_LSR_OFF) & LSR_THR_EMPTY) == 0)
            ;
        iowrite8(log_device.io_port, (u8)buf[i]);
    }
    spinlock_unlock(log_device.lock);
}

/*
 * Unified log handler (LOG()).
 *
 * Runs in the caller's context, so arch_running_ring3() reflects the real
 * privilege.  Both CPL0 (KLOG flow) and CPL3 (ULOG flow) get a timestamp,
 * but only once the timer driver has been probed — during early boot, with
 * no timer yet, the timestamp is skipped so boot logging still works.
 */
void log_handler(void* context)
{
    log_data* p = (log_data*)context;
    if (!p || !p->log || p->size == 0)
        return;

    const char* tag = arch_running_ring3() ? "ULOG: " : "KLOG: ";
    char out_buf[512] = {0};

    if (timer_is_ready()) {
        char tmr_buf[32] = {0};
        timer_read_time_str(tmr_buf, sizeof(tmr_buf));
        snprintf(out_buf, sizeof(out_buf), "%s %s%s", tmr_buf, tag, p->log);
    } else {
        snprintf(out_buf, sizeof(out_buf), "%s%s", tag, p->log);
    }

    log_write_direct(out_buf, strlen(out_buf));
}

/* ======================= user-mode log server ======================= */

static void log_server_loop(void)
{
    /* LOG() writes the serial port directly from any privilege level, so
     * this server thread simply idles, keeping the server process alive. */
    for (;;)
        thread_yield();
}

int log_server_start(struct device* dev)
{
    (void)dev;

    if (!log_device.lock) {
        log_device.lock = spinlock_alloc();
        if (!log_device.lock)
            return E_LIMIT;
    }

    /* Idempotent — the port was already brought up early by log_init(). */
    serial_port_init(log_device.io_port);
    log_device.ready = 1;

    LOG("log_server started");

    log_server_loop();   /* never returns */
    return 0;
}

int log_server_stop(struct device* dev)
{
    (void)dev;
    log_device.ready = 0;
    return 0;
}

static struct driver log_server = {
    .class = DRIVER_CLASS_USER,
    .type  = "log",
    .start = log_server_start,
    .stop  = log_server_stop,
};

/* Early kernel phase: bring up COM1 right away so LOG() works before the
 * user-mode server is started (and before paging / scheduler / heap). */
void log_init(void)
{
    if (!log_device.lock) {
        log_device.lock = spinlock_alloc();
        if (!log_device.lock)
            return;
    }

    serial_port_init(log_device.io_port);
    log_device.ready = 1;
}

/* Register the user-mode log server.  Called from init_thread() once the
 * scheduler is up (like kb_server_init / terminal_init). */
void log_server_init(void)
{
    platform_driver_register(&log_server);
}

void log_exit(void)
{
    platform_driver_unregister(&log_server);
}
