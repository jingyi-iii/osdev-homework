#include "kernel/init.h"
#include "drivers/terminal_driver.h"
#include "kernel/process.h"

/* Demo thread entry points (defined in demo/) */
extern void airplane_thread(void);

void init_thread(void)
{
    terminal_flush(0);

    proc_create(PROC_PRIV_KERNEL, airplane_thread);

    proc_exit(0);
}