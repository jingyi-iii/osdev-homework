/*
 * user/userlib.h — minimal user-mode library.
 *
 * Self-contained: depends only on lib/types.h, kernel/uapi.h and the
 * user-space ns_proto.h (plain headers with no kernel-only dependencies),
 * so user programs link cleanly against the fixed syscall ABI without
 * kernel symbols.
 */
#ifndef USERLIB_H
#define USERLIB_H

#include "lib/types.h"
#include "kernel/uapi.h"
#include "ns_proto.h"            /* namespace protocol (user<->user names/struct) */
#include "server/server_msgs.h"  /* rtc_request/rtc_time and other server wire formats */

/*
 * proc/thread control commands — MUST mirror the kernel's
 * `enum proc_thread_ctrl` (kernel/process.c).
 */
enum {
    U_THREAD_CTRL_CREATE = 0,
    U_THREAD_CTRL_DELETE,
    U_THREAD_CTRL_YIELD,
    U_THREAD_CTRL_BLOCK,
    U_THREAD_CTRL_UNBLOCK,
    U_PROC_CTRL_CREATE,
    U_PROC_CTRL_LOAD_FROM_ELF,  /* kernel-only slot — value must stay in sync */
    U_PROC_CTRL_EXIT,
    U_PROC_CTRL_BLOCK,
    U_PROC_CTRL_UNBLOCK,
    U_PROC_CTRL_GET_PID,
    U_THREAD_CTRL_GET_TID,
};

/*
 * proc/thread syscall config — MUST mirror the kernel's
 * `proc_thread_ctrl_config` (include/kernel/process.h) in layout:
 *   u8 cmd + 3 pad, then i32 pid/tid/priv and two pointers.
 * Both sides use the compiler's natural alignment, so they match.
 */
typedef struct user_proc_ctrl {
    u8   cmd;
    i32  pid;
    i32  tid;
    i32  priv;
    void* entry;
    void* elf_start;   /* kernel-only fields — keep layout in sync */
    void* elf_end;
    void* param;
} user_proc_ctrl;

/*
 * user_syscall — trap into the kernel:
 *   ebx = syscall number, ecx = config, edx = config size, eax = result.
 */
static inline int user_syscall(u32 num, void* arg, u32 size)
{
    int ret;
    __asm__ __volatile__(
        "int  $100\n\t"
        : "=a"(ret)
        : "b"(num), "c"(arg), "d"(size)
        : "memory");
    return ret;
}

/* Yield the CPU via the proc/thread syscall. */
static inline void user_yield(void)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_THREAD_CTRL_YIELD;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

/* Thread control (TASK_PRIV_USER = 1, TASK_PRIV_KERNEL = 0 — kernel-priv
 * threads additionally require CAP_CREATE_KRNL_THREAD, enforced in the
 * gate).  create() returns the new TID (>= 0) or a negative errno; the
 * thread is born TS_PENDING, so unblock() it to let it run. */
static inline i32 user_thread_create(i32 priv, void* entry, void* param)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd   = U_THREAD_CTRL_CREATE;
    cfg.priv  = priv;
    cfg.entry = entry;
    cfg.param = param;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
    return cfg.tid;
}

static inline void user_thread_exit(i32 tid)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_THREAD_CTRL_DELETE;
    cfg.tid = tid;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

static inline void user_thread_block(i32 tid)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_THREAD_CTRL_BLOCK;
    cfg.tid = tid;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

static inline void user_thread_unblock(i32 tid)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_THREAD_CTRL_UNBLOCK;
    cfg.tid = tid;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

static inline i32 user_thread_get_tid(void)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_THREAD_CTRL_GET_TID;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
    return cfg.tid;
}

/* Process control.  create() returns the new PID (>= 0) or a negative
 * errno; the process is born TS_PENDING, so unblock() it to let it run. */
static inline i32 user_proc_create(i32 priv, void* entry, void* param)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd   = U_PROC_CTRL_CREATE;
    cfg.priv  = priv;
    cfg.entry = entry;
    cfg.param = param;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
    return cfg.pid;
}

static inline void user_proc_exit(i32 pid)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_PROC_CTRL_EXIT;
    cfg.pid = pid;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

static inline void user_proc_block(i32 pid)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_PROC_CTRL_BLOCK;
    cfg.pid = pid;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

static inline void user_proc_unblock(i32 pid)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_PROC_CTRL_UNBLOCK;
    cfg.pid = pid;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
}

static inline i32 user_proc_get_pid(void)
{
    user_proc_ctrl cfg = {0};
    cfg.cmd = U_PROC_CTRL_GET_PID;
    user_syscall(SYSCALL_PROC_THREAD, &cfg, sizeof(cfg));
    return cfg.pid;
}

/* Coarse yield-based delay (no timer syscall in the user ABI). */
static inline void user_delay_ms(u32 ms)
{
    for (u32 i = 0; i < ms * 50; i++)
        user_yield();
}

/*
 * Portal RPC ABI — MUST mirror the kernel's portal_ctrl_config
 * (include/ipc/portal.h) and its command enum in layout and values.
 */
enum {
    U_PORTAL_CTRL_INIT = 0,
    U_PORTAL_CTRL_DESTROY,
    U_PORTAL_CTRL_CALL,
    U_PORTAL_CTRL_WAIT_REPLY,
    U_PORTAL_CTRL_GET_RESULT,
    U_PORTAL_CTRL_CLEANUP,
    U_PORTAL_CTRL_WAIT,
    U_PORTAL_CTRL_GET_REQ,
    U_PORTAL_CTRL_REPLY,
};

typedef struct user_portal_ctrl {
    int   cmd;
    u32   client_id;
    u32   server_id;
    void* va;
    u32   va_size;
    void* req;      /* opaque request handle, returned by CALL */
    void* out;      /* INIT: portal id out; CALL: shm_va out   */
    int   ret;      /* GET_RESULT out / REPLY in               */
} user_portal_ctrl;

/* Synchronous portal RPC: CALL → WAIT_REPLY → GET_RESULT → CLEANUP. */
int user_portal_call(u32 portal_id, void* va, u32 size);

/* ---- namespace client (namespace_server.elf @ PORTAL_ID_NAMESPACE) ---- */

/* Register this service's bindings (server side).  portal_id / mailbox_tid
 * / mail_magic may each be 0 if the service does not offer that channel. */
int ns_register(const char* name, u32 portal_id, u32 mailbox_tid,
                u32 mail_magic);

/* Resolve a registered name; fills the (non-NULL) out_* with the service's
 * portal id, mailbox-owner thread tid and broadcast mail magic.  Returns
 * 0 on success or a negative code (-1 = not registered yet). */
int ns_lookup(const char* name, u32* out_portal_id,
              u32* out_mailbox_tid, u32* out_mail_magic);

/* Print a NUL-terminated string to the VGA console.  The console portal id
 * is resolved from the namespace ("console", terminal_server.elf) on first
 * use and cached; retries while the server comes up. */
void console_putstr(const char* s);

/*
 * Log to serial (COM1) through the log server ("log", log_server2.elf),
 * resolved from the namespace on first use and cached (retries like
 * console_putstr).  user_log_write takes raw bytes (binary-safe),
 * user_log_str logs a NUL-terminated string.  The buffer must live in a
 * VMM-mapped region (static/stack data of the ELF is fine).
 */
void user_log_write(const char* s, u32 len);
void user_log_str(const char* s);

/* ---- RTC client (rtc_server.elf, namespace name "rtc") ----
 * user_rtc_time reads the CMOS clock (year..second, decimal).
 * user_rtc_sleep_ms is a real timed delay (PIT counter), unlike the
 * fallback user_delay_ms() yield loop.  Both return 0 on success or a
 * negative code (-1 = server not registered yet, -3 = bad args/cmd). */
int  user_rtc_time(rtc_time* out);
int  user_rtc_sleep_ms(u32 ms);

/* ---- graphics client (terminal server doubles as the graphics server) ----
 * gfx_set_mode(0x13) switches the VGA to 320x200x256 graphics; the client
 * then shm_share()s its frame buffer with the terminal process once and
 * calls gfx_blit() per frame (the server memcpy()s the shared buffer to
 * 0xA0000).  gfx_set_mode(0x03) switches back to text.  Both return the
 * server's reply int (0 = ok, negative = error). */
int  gfx_set_mode(u32 mode);
int  gfx_blit(u32 fb_size);

/* Preferred blit: copies fb into a static header+fb staging buffer that
 * is shm_share()d with the terminal process for the duration of the call;
 * the server copies the fb to 0xA0000.  No persistent mapping needed. */
int  gfx_blit_shared(const u8* fb, u32 fb_size);

/*
 * ---- IRQ syscall (SYSCALL_IRQ) ---------------------------------------
 *
 * Layout MUST mirror the kernel's irq_ctrl_config (include/kernel/irq.h).
 */
enum {
    U_IRQ_CTRL_REQUEST = 0,
    U_IRQ_CTRL_RELEASE,
    U_IRQ_CTRL_MASK,
    U_IRQ_CTRL_UNMASK,
};

typedef struct user_irq_ctrl {
    u8          cmd;
    void*       handle;       /* out (request) / in (release, mask, unmask) */
    const char* name;         /* request: may be NULL */
    u32         major;
    u32         minor;
    void*       handler;      /* unused for user IRQs (mail delivery) */
    void*       param;
    int         is_user_irq;  /* kernel fills in */
    int         tid;          /* kernel fills in */
    int         ret;
} user_irq_ctrl;

/* Request a user IRQ on @major; the kernel ISR then delivers
 * MAIL_MAGIC_IRQ mails to the calling thread's mailbox.  Returns the
 * opaque irq handle (NULL on failure). */
void* user_irq_request(u32 major, u32 minor);
int   user_irq_unmask(void* handle);

/*
 * ---- mailbox syscall (SYSCALL_MAILBOX) ---------------------------------
 *
 * Layout MUST mirror the kernel's mailbox_ctrl_config
 * (include/ipc/mailbox.h) and its command enum in layout and values.
 * Command values are the kernel's MAILBOX_CTRL_* numbers.
 */
enum {
    U_MAILBOX_CTRL_SEND             = 0,  /* send a mail (uni / broadcast)  */
    U_MAILBOX_CTRL_LISTEN           = 1,  /* dequeue a mail (non-blocking)  */
    U_MAILBOX_CTRL_ALLOC_MAIL       = 4,  /* allocate a mail for sending    */
    U_MAILBOX_CTRL_RELEASE_MAIL     = 5,  /* free a received mail           */
    U_MAILBOX_CTRL_SUBSCRIBE_MAIL   = 8,  /* subscribe own mailbox to magic */
    U_MAILBOX_CTRL_UNSUBSCRIBE_MAIL = 9,  /* unsubscribe own mailbox        */
};

/* Broadcast receiver wildcard — MUST mirror MAIL_ANY_TID
 * (include/ipc/mailbox.h).  Set user_mail.receiver_tid to this to
 * broadcast to every mailbox subscribed to m->magic. */
#define USER_MAIL_ANY_TID   (-0xcd)

typedef struct user_mailbox_ctrl {
    u8   cmd;
    void* m;          /* mail* — out (ALLOC/LISTEN) / in (SEND/RELEASE)    */
    void* mb;         /* mailbox* — NULL = calling thread's own mailbox    */
    void* handler;    /* unused by user programs (kernel-only handlers)    */
    int  pid;         /* unused by user programs                           */
    int  tid;         /* unused by user programs                           */
    u32  magic;       /* in: SUBSCRIBE / UNSUBSCRIBE magic                 */
    int  ret;         /* out: 0 / negative errno                           */
} user_mailbox_ctrl;

/*
 * Mail view — mirrors the LEADING fields of the kernel's mail
 * (include/ipc/mailbox.h).  The kernel mail object is allocated from the
 * SHARED USER heap (SYSCALL_HEAP malloc, [0xC0000000, 0xC1000000)); its
 * bookkeeping (mailmeta) stays in the kernel heap and is never exposed to
 * ring-3.  The view is therefore directly readable AND writable:
 *   - outbound: fill magic / receiver_tid / data / data_size, user_mail_send
 *   - inbound:  read magic / sender_tid / data / data_size, user_mail_release
 * magic is the opaque notification tag (e.g. MAIL_MAGIC_IRQ, or a
 * user-defined event magic such as MSG_KEY_EVENT).
 */
typedef struct user_mail {
    u32  magic;
    int  sender_tid;      /* informational (mirrors kernel's mail) */
    int  receiver_tid;    /* routing: thread id or USER_MAIL_ANY_TID */
    char data[256];
    u32  data_size;   /* mirrors kernel's size_t (u32 on i686) */
} user_mail;

/* Block (yielding) until a mail arrives on the calling thread's own
 * mailbox; returns the opaque mail* (view as user_mail*). */
void* user_mail_listen(void);
void  user_mail_release(void* m);

/* Allocate a mail for sending (view as user_mail* and fill it in). */
void* user_mail_alloc(void);

/* Send a mail: addressed (receiver_tid set) or broadcast to every mailbox
 * subscribed to m->magic (receiver_tid = USER_MAIL_ANY_TID). */
int   user_mail_send(void* m);

/* Subscribe / unsubscribe the calling thread's OWN mailbox to a magic so
 * that broadcasts carrying that magic are delivered to it. */
int   user_mail_subscribe(u32 magic);
int   user_mail_unsubscribe(u32 magic);

int user_irq_wait(void);

/* ------------------------------------------------------------------
 * User-heap syscall (SYSCALL_HEAP) — ring-3 malloc()/free().
 *
 * Mirrors heap_ctrl_config (include/mm/heap.h): layout + command values
 * must stay in sync.  malloc()/free() are the libc-style names; user
 * ELFs are compiled with -fno-builtin so GCC never rewrites calls into
 * __builtin_malloc.  The heap lives in the shared user region
 * [0xC0000000, 0xC1000000) (USER_HEAP_BASE, arch/i386/paging.h).
 * ---------------------------------------------------------------- */
#define U_HEAP_CTRL_MALLOC      0
#define U_HEAP_CTRL_FREE        1

typedef struct user_heap_ctrl {
    u32   cmd;      /* U_HEAP_CTRL_MALLOC / U_HEAP_CTRL_FREE */
    u32   size;     /* MALLOC: bytes requested              */
    void* ptr;      /* MALLOC: out — new ptr; FREE: in      */
    int   ret;      /* out: 0 / errno                       */
} user_heap_ctrl;

void* malloc(unsigned int size);
void  free(void* ptr);

/* ------------------------------------------------------------------
 * MMIO window mapping syscall (SYSCALL_MMIO, kernel/mmio.c) — map a
 * CAP_MAP_MEM-authorized physical MMIO window into THIS process at a
 * caller-chosen fixed high VA (own_phys = 0, so the window is never
 * returned to the PMM).  Mirrors mmio_syscall_data
 * (include/kernel/mmio.h); layout + command values must stay in sync.
 * ---------------------------------------------------------------- */
#define U_MMIO_CTRL_MAP      0
#define U_MMIO_CTRL_UNMAP    1

typedef struct user_mmio_ctrl {
    u8   cmd;      /* U_MMIO_CTRL_MAP / U_MMIO_CTRL_UNMAP */
    u32  pa;       /* MAP: in — physical MMIO address     */
    u32  size;     /* MAP/UNMAP: in — bytes (page mult.)  */
    u32  va;       /* MAP/UNMAP: in — caller-chosen VA    */
    int  ret;      /* out: 0 / errno                      */
} user_mmio_ctrl;

/* MAP: map [pa,pa+size) at the fixed high VA @vaddr; UNMAP: drop it. */
int user_mmio_map(u32 pa, u32 size, void* vaddr);
int user_mmio_unmap(void* vaddr, u32 size);

#endif
