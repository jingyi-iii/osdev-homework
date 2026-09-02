#include "kernel/init.h"
#include "kernel/process.h"
#include "kernel/capability.h"
#include "multiboot.h"
#include "kernel/log.h"
#include "kernel/kterm.h"
#include "lib/elf.h"
#include "lib/string.h"

/*
 * Capability grants for the standalone driver-server ELFs (loaded from
 * GRUB multiboot modules).  The old platform-device table is gone; each
 * server gets exactly the resources its code touches through the io /
 * irq / ipc syscall gates.
 */
static void grant_terminal_caps(pcb* proc)
{
    if (!proc)
        return;

    cap_io_port vga = { 0x3C0, 32 };   /* VGA 0x3C0-0x3DF (cursor, mode) */
    int ipc_ok = 1;                     /* console portal                 */

    cap_grant(proc, CAP_ACCESS_IO, &vga);
    cap_grant(proc, CAP_IPC, &ipc_ok);
}

static void grant_kb_caps(pcb* proc)
{
    if (!proc)
        return;

    u32 irq_21 = 0x21;                  /* keyboard IRQ                  */
    cap_io_port ps2  = { 0x60, 5 };     /* PS/2 data + status 0x60-0x64  */
    cap_io_port com1 = { 0x3F8, 8 };    /* COM1: key diagnostics         */
    int ipc_ok = 1;                     /* IRQ mail delivery             */

    cap_grant(proc, CAP_OWN_IRQ, &irq_21);
    cap_grant(proc, CAP_ACCESS_IO, &ps2);
    cap_grant(proc, CAP_ACCESS_IO, &com1);
    cap_grant(proc, CAP_IPC, &ipc_ok);
}

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
 * so all test/game children keep working.  Driver-server ELFs get their
 * own grants from grant_terminal_caps/grant_kb_caps above.
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

/* hello.elf — first user ELF demo.  It prints via the console portal
 * (portal_call shares the caller's buffer with shm_share, which is behind
 * the CAP_IPC gate) and LOGs via SYSCALL_LOG (no caps needed). */
static void grant_hello_caps(pcb* proc)
{
    if (!proc)
        return;

    int ipc_ok = 1;
    cap_grant(proc, CAP_IPC, &ipc_ok);
}

/* Capability grant for a freshly loaded ELF process. */
typedef void (*caps_grant_fn)(pcb* proc);

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
 * user process via proc_load_from_elf().  @grant is applied to the new
 * process before it is unblocked (NULL = no grants).  Returns the pid,
 * or 0 when the module is missing (the system keeps booting without it).
 */
static i32 load_user_elf_by_name(const char* name, caps_grant_fn grant)
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
            if (grant)
                grant(get_process_by_pid(pid));
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
    /* ---- Boot: the kernel's own output goes through kterm_* (pure port
     * I/O on the VGA text buffer).  Driver servers are standalone user
     * ELFs (user/server/): load them as GRUB modules with their resource
     * grants, then run the portal test.  Loading order matters: the
     * terminal server must publish PORTAL_ID_CONSOLE before portal_test
     * prints through it (processes run in creation order).
     * ---- */
    kterm_switch_to_text_mode();
    kterm_clear();

    kterm_write("[launcher] starting terminal_server.elf\n");
    load_user_elf_by_name("terminal_server.elf", grant_terminal_caps);

    kterm_write("[launcher] starting kb_server.elf\n");
    load_user_elf_by_name("kb_server.elf", grant_kb_caps);

    /* User-mode LOG server: claims SYSCALL_LOG via SYSCALL_SYSCTL, so any
     * process's SYSCALL_LOG trap reaches its ring-0 handler (running with
     * the log server's page tables).  No grants: registration only needs a
     * USER process; the handler writes COM1 via direct port I/O at ring 0. */
    kterm_write("[launcher] starting log_server2.elf\n");
    load_user_elf_by_name("log_server2.elf", 0);

    kterm_write("[launcher] running portal_test.elf\n");
    load_user_elf_by_name("portal_test.elf", grant_demo_caps);

    /* hello.elf — first user ELF demo: console portal + SYSCALL_LOG. */
    kterm_write("[launcher] running hello.elf\n");
    load_user_elf_by_name("hello.elf", grant_hello_caps);

    proc_exit(proc_get_pid());
}