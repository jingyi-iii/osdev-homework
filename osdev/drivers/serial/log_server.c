#include "drivers/log_server.h"
#include "drivers/timer_server.h"
#include "kernel/process.h"
#include "kernel/io.h"
#include "kernel/klog.h"
#include "user/uspinlock.h"

#define SERIAL_COM1_BASE   0x3F8
#define SERIAL_LSR_OFF     5      /* Line Status Register offset  */
#define LSR_THR_EMPTY      0x20   /* Transmitter Holding Register empty */

/*
 * User-mode (ring-3) log writer.
 *
 * log_device is gone: no kernel struct, no kernel spinlock.  The COM1
 * port is owned and initialised by klog (kernel/klog.c); ring-3 LOG
 * output is written straight to the port through the io layer, which
 * routes ring-3 port I/O through the syscall gate (CAP_ACCESS_IO).
 * Exclusion between user threads is a user-mode spinlock.
 */
static uspinlock ulog_lock = USPINLOCK_INIT;

static void ulog_write_direct(const char* buf, size_t size)
{
    uspin_lock(&ulog_lock);
    for (size_t i = 0; i < size; i++) {
        int timeout = 0;
        while ((ioread8(SERIAL_COM1_BASE + SERIAL_LSR_OFF) & LSR_THR_EMPTY) == 0) {
            /* If the transmitter never becomes ready (no UART / broken
             * status line), drop the rest instead of hanging forever. */
            if (++timeout > 1000000) {
                uspin_unlock(&ulog_lock);
                return;
            }
            __asm__ __volatile__("pause" ::: "memory");
        }
        iowrite8(SERIAL_COM1_BASE, (u8)buf[i]);
    }
    uspin_unlock(&ulog_lock);
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

    int ring3 = arch_running_ring3();
    const char* tag = ring3 ? "ULOG: " : "KLOG: ";
    char out_buf[512] = {0};

    if (timer_is_ready()) {
        char tmr_buf[32] = {0};
        timer_read_time_str(tmr_buf, sizeof(tmr_buf));
        snprintf(out_buf, sizeof(out_buf), "%s %s%s", tmr_buf, tag, p->log);
    } else {
        snprintf(out_buf, sizeof(out_buf), "%s%s", tag, p->log);
    }

    /* Ring-3 → user-mode writer (uspinlock + direct port I/O);
     * ring-0 → klog (cli-guarded direct port I/O).  Neither path touches
     * a kernel data structure. */
    if (ring3)
        ulog_write_direct(out_buf, strlen(out_buf));
    else
        klog_write(out_buf);
}

/* ======================= user-mode log server ======================= */

static void log_server_loop(void)
{
    /* LOG() writes the serial port directly (klog for ring-0, this
     * server's own direct port writer for ring-3), so this thread simply
     * idles, keeping the server process alive. */
    for (;;)
        thread_yield();
}

int log_server_start(struct device* dev)
{
    (void)dev;

    /* The COM1 port is owned and already brought up by klog_init() at
     * boot; the server itself needs no lock and no kernel data
     * structure. */

    LOG("log_server started");

    log_server_loop();   /* never returns */
    return 0;
}

int log_server_stop(struct device* dev)
{
    (void)dev;
    return 0;
}

static struct driver log_server = {
    .class = DRIVER_CLASS_USER,
    .type  = "log",
    .start = log_server_start,
    .stop  = log_server_stop,
};

/* log_init() is provided by kernel/log.h (pure-IO klog init).  The LOG()
 * macro no longer routes through this server. */

/* Hardware resources the log server process needs. */
static const struct platform_resource log_server_resources[] = {
    { .type = PLAT_RES_IO, .io = { .base = 0x3F8, .size = 8 } },  /* COM1 0x3F8-0x3FF */
    { .type = PLAT_RES_IO, .io = { .base = 0x70, .size = 2 } },  /* CMOS 0x70-0x71: LOG() timestamps */
};

/* Start the user-mode log server directly (no platform bus probing).
 * Called from init_thread() once the scheduler is up (like kb_server_init
 * / terminal_init). */
void log_server_init(void)
{
    platform_user_server_start(&log_server, log_server_resources,
                               sizeof(log_server_resources) /
                               sizeof(log_server_resources[0]));
}

void log_exit(void)
{
    /* Server processes are never stopped in this design. */
}
