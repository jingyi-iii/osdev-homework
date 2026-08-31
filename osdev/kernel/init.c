#include "kernel/init.h"
#include "kernel/process.h"
#include "kernel/capability.h"
#include "kernel/multiboot.h"
#include "kernel/log.h"
#include "kernel/kterm.h"
#include "lib/elf.h"
#include "lib/string.h"

extern void kb_server_init(void);
extern void terminal_init(void);
extern void log_server_init(void);
extern void timer_server_init(void);

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

/* ---- GRUB multiboot module helpers ---------------------------------- */

/* Match a module's cmdline string (e.g. "/boot/hello.elf") against a
 * plain basename, so the grub.cfg module order does not matter. */
static int modname_is(const char* path, const char* name)
{
    const char* base;

    if (!path || !name)
        return 0;
    base = path;
    for (const char* p = path; *p; p++)
        if (*p == '/')
            base = p + 1;
    return strcmp(base, name) == 0;
}

/*
 * Find the GRUB module whose cmdline basename is @name and load it as a
 * user process via proc_load_from_elf().  Returns the pid, or 0 when the
 * module is missing (the system keeps booting without it).
 */
static i32 load_user_elf_by_name(const char* name)
{
    int n = mboot_module_count();

    for (int i = 0; i < n; i++) {
        if (!modname_is(mboot_module_name(i), name))
            continue;

        u8* start = 0;
        u8* end = 0;
        if (mboot_module_get(i, &start, &end) != 0)
            break;

        i32 pid = proc_load_from_elf(start, end, 0);
        if (pid > 0) {
            grant_demo_caps(get_process_by_pid(pid));
            proc_unblock(pid);
        }
        return pid;
    }

    LOG("user elf '%s': not found among %d GRUB modules", name, n);
    /* Visible on-screen hint: the module is a GRUB multiboot module, so
     * booting via `-kernel` (no modules) silently loads nothing. */
    kterm_write("[launcher] ERROR: '");
    kterm_write(name);
    kterm_write("' not found among GRUB modules\n");
    kterm_write("           (boot the ISO, e.g. make run -- not -kernel)\n");
    return 0;
}

/* ---- Boot: run the portal test directly ---------------------------- */

void init_thread(void)
{
    kb_server_init();
    terminal_init();
    kterm_init();
    timer_server_init();
    log_server_init();

    /* ---- Boot: run the user-mode portal test directly (no keypress
     * prompt).  The kernel's own output goes through kterm_* (pure port
     * I/O on the VGA text buffer, decoupled from the user-mode terminal
     * server).  terminal_init() above directly starts the ring-3 terminal
     * server: it publishes the console portal (PORTAL_ID_CONSOLE) that
     * portal_test.elf prints through.
     * ---- */
    kterm_switch_to_text_mode();
    kterm_clear();
    kterm_write("[launcher] running portal_test.elf\n");
    load_user_elf_by_name("portal_test.elf");

    proc_exit(proc_get_pid());
}