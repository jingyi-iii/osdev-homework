#include "kernel/init.h"
#include "kernel/process.h"

extern void process_test_main_thread(void);

extern void gfx_server_init(void);
extern void game_proc_main_thread(void);

extern void kb_server_init(void);
extern void terminal_init(void);
extern void log_server_init(void);
extern void timer_server_init(void);

void init_thread(void)
{
     kb_server_init();

     /* case1: test mode */
     terminal_init();
     timer_server_init();
     log_server_init();
     proc_create(PROC_PRIV_KERNEL, process_test_main_thread, 0);

     // /* case2: game mode */
     // gfx_server_init();
     // proc_create(PROC_PRIV_KERNEL, game_proc_main_thread, 0);

     proc_exit(proc_get_pid());
}