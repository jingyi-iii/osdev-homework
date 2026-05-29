#include "kernel/init.h"
#include "drivers/terminal_driver.h"
#include "kernel/process.h"

/* Demo thread entry points (defined in demo/) */
extern void timer_thread(void);
extern void timer_thread2(void);

void init_thread(void)
{
    terminal_flush(0);

    proc_create(PROC_PRIV_KERNEL, timer_thread);
    proc_create(PROC_PRIV_USER, timer_thread2);

    proc_exit(0);
    while (1);
}