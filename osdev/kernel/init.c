#include "kernel/init.h"
#include "kernel/process.h"
#include "kernel/capability.h"

extern void process_test_main_thread(void);

extern void graphics_module_init(void);
extern void graphics_module_exit(void);
extern void game_proc_main_thread(void);

extern void kb_main_thread(void);

void init_thread(void)
{
     int kbserver_pid = proc_create(PROC_PRIV_USER, kb_main_thread, 0);
     int kb_irq = 0x21;
     cap_grant(get_process_by_pid(kbserver_pid), CAP_IRQ_OWN, &kb_irq);

     proc_create(PROC_PRIV_KERNEL, process_test_main_thread, 0);

     // graphics_module_init();
     // proc_create(PROC_PRIV_KERNEL, game_proc_main_thread, 0);

     proc_exit(proc_get_pid());
}