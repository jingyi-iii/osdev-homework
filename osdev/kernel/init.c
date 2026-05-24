#include "kernel/init.h"
#include "drivers/terminal_driver.h"

/* Demo thread entry points (defined in demo/) */
extern void timer_thread(void);
extern void timer_thread2(void);

void init_thread(void)
{
    terminal_flush(0);

    thread_create(THREAD_PRIV_KERNEL, timer_thread);
    thread_create(THREAD_PRIV_USER, timer_thread2);

    thread_exit(0);
}