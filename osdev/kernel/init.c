#include "kernel/init.h"
#include "kernel/process.h"
#include "kernel/capability.h"
#include "lib/elf.h"

extern void process_test_main_thread(void);

extern void gfx_server_init(void);
extern void game_proc_main_thread(void);

extern void kb_server_init(void);
extern void terminal_init(void);
extern void log_server_init(void);
extern void timer_server_init(void);

/* Embedded user program — bytes placed by user/hello_embed.S (.incbin). */
extern u8 user_hello_elf_start[];
extern u8 user_hello_elf_end[];

/*
 * Grant the top-level demo/test process everything its ring-3 code needs:
 *   - port ranges (all I/O goes through the capability-checked io syscall
 *     gate, see kernel/io.c):
 *       VGA  {0x3C0, 32}:  terminal_write()/gfx_*() rendering
 *       COM1 {0x3F8, 8}:   LOG() output
 *       PIT/PPI/CMOS:      timer_delay_ms()/timer_get_time() (used by nearly
 *                          every test suite; without them pit_delay_ticks()
 *                          would spin forever on denied port reads)
 *   - CAP_IPC: the test suites use mailbox_* (mailbox_api_test) and
 *     shm_share/shm_unshare (shm_test).
 * Processes the demo spawns inherit a copy (cap_inherit_all in p_create),
 * so all test/game children keep working.  Driver servers get their own
 * grants from the platform device table.
 */
static void grant_demo_caps(pcb* proc)
{
    if (!proc)
        return;

    cap_io_port vga  = { 0x3C0, 32 };   /* VGA ports 0x3C0-0x3DF */
    cap_io_port com1 = { 0x3F8, 8  };   /* COM1 (LOG() output)    */
    cap_io_port pit  = { 0x40, 4  };    /* PIT 0x40-0x43 (timer_delay_*) */
    cap_io_port ppi  = { 0x61, 1  };    /* PPI port B (PIT gate)  */
    cap_io_port cmos = { 0x70, 2  };    /* CMOS 0x70-0x71 (timer_get_time) */

    cap_grant(proc, CAP_ACCESS_IO, &vga);
    cap_grant(proc, CAP_ACCESS_IO, &com1);
    cap_grant(proc, CAP_ACCESS_IO, &pit);
    cap_grant(proc, CAP_ACCESS_IO, &ppi);
    cap_grant(proc, CAP_ACCESS_IO, &cmos);

    /* IPC: the test suites use mailbox_* (mailbox_api_test) and
     * shm_share/shm_unshare (shm_test); children inherit this grant. */
    int ipc_ok = 1;
    cap_grant(proc, CAP_IPC, &ipc_ok);
}

void init_thread(void)
{
    kb_server_init();

    /* case1: test mode */
    terminal_init();
    timer_server_init();
    log_server_init();

     /* Load and run the user-mode ELF demo (user/hello.elf) in its own
      * process — exercises the ELF loader + fixed syscall ABI. */
    //  user_elf_demo();
    i32 pid = proc_load_from_elf(user_hello_elf_start, user_hello_elf_end, 0);
    if (pid > 0) {
        /* The ELF demo prints via the terminal server's console portal
         * (portal_call → shm_share), which requires CAP_IPC. */
        grant_demo_caps(get_process_by_pid(pid));
        proc_unblock(pid);
    }

    pid = proc_create(PROC_PRIV_USER, process_test_main_thread, 0);
    grant_demo_caps(get_process_by_pid(pid));
    proc_unblock(pid);

     // /* case2: game mode */
     // gfx_server_init();
     // pid = proc_create(PROC_PRIV_USER, game_proc_main_thread, 0);
     // grant_demo_caps(get_process_by_pid(pid));

    proc_exit(proc_get_pid());
}