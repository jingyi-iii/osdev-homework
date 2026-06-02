#include "kernel/init.h"
#include "kernel/process.h"

extern void game_proc_main_thread(void);

void init_thread(void)
{
     proc_create(PROC_PRIV_USER, game_proc_main_thread);
     proc_exit(proc_get_pid());
}