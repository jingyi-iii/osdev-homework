#include "kernel/init.h"
#include "kernel/process.h"

extern void process_test_main_thread(void);

extern void graphics_module_init(void);
extern void graphics_module_exit(void);
extern void game_proc_main_thread(void);

void init_thread(void)
{
     proc_create(PROC_PRIV_KERNEL, process_test_main_thread);

     // graphics_module_init();
     // proc_create(PROC_PRIV_KERNEL, game_proc_main_thread);

     proc_exit(proc_get_pid());
}