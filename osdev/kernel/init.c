#include "kernel/init.h"
#include "drivers/terminal_driver.h"

/* Demo process entry points (defined in demo/) */
extern void timer_process(void);
extern void timer_process2(void);

void init_process(void)
{
    terminal_flush(0);

    proc_create(PROC_PRIV_KERNEL, timer_process);
    proc_create(PROC_PRIV_USER, timer_process2);

    proc_exit(0);
}