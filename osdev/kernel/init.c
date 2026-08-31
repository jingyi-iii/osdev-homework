#include "kernel/init.h"
#include "kernel/process.h"
#include "kernel/capability.h"
#include "kernel/multiboot.h"
#include "kernel/log.h"
#include "drivers/terminal_server.h"
#include "drivers/kb_server.h"
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
    return 0;
}

/* ---- Boot prompt keyboard input ------------------------------------ */

static volatile int boot_key = 0;

static void boot_kb_handler(const char* data, size_t size)
{
    (void)size;
    if (data && data[0] && !boot_key)
        boot_key = (unsigned char)data[0];
}

void init_thread(void)
{
    kb_server_init();
    terminal_init();
    timer_server_init();
    log_server_init();

    /* ---- Boot prompt: ask the user whether to run the user-mode portal
     * test before loading it.  terminal_* write VGA directly (kernel-image
     * driver); the kb server delivers keypresses via kb_register_callback.
     * ---- */
    terminal_switch_to_text_mode();
    terminal_flush(0);
    terminal_write("\n========================================\n");
    terminal_write("         USER PORTAL TEST LAUNCHER       \n");
    terminal_write("========================================\n\n");
    terminal_write("  Press 'y' to load and run portal_test.elf\n");
    terminal_write("  (any other key skips the test)\n\n");

    kb_register_callback(boot_kb_handler);
    while (!boot_key)
        thread_yield();
    kb_unregister_callback(boot_kb_handler);

    if (boot_key == 'y' || boot_key == 'Y') {
        terminal_write("[launcher] running portal_test.elf\n");
        load_user_elf_by_name("portal_test.elf");
    } else {
        terminal_write("[launcher] portal test skipped\n");
    }

    proc_exit(proc_get_pid());
}