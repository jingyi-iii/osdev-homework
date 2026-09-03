/*
 * user/userlib.h — minimal user-mode library.
 *
 * Self-contained: only depends on lib/types.h and kernel/uapi.h (both are
 * plain headers with no kernel-only dependencies), so user programs link
 * cleanly against the fixed syscall ABI without kernel symbols.
 */
#ifndef USERLIB_H
#define USERLIB_H

#include "lib/types.h"
#include "kernel/uapi.h"

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

/* Print a NUL-terminated string via the terminal server's console portal
 * (PORTAL_ID_CONSOLE). */
void console_putstr(const char* s);

/*
 * Log through the user-mode log server (SYSCALL_LOG, claimed by
 * log_server2.elf via SYSCALL_SYSCTL).  Until the server registers, the
 * kernel returns E_NOTFOUND, so both retry briefly (same pattern as
 * console_putstr).  user_log_write takes raw bytes (binary-safe),
 * user_log_str logs a NUL-terminated string.
 */
void user_log_write(const char* s, u32 len);
void user_log_str(const char* s);

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

/* Broadcast receiver wildcards — MUST mirror MAIL_ANY_PID / MAIL_ANY_TID
 * (include/ipc/mailbox.h).  Set user_mail.receiver_* to these to broadcast. */
#define USER_MAIL_ANY_PID   (-0xab)
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
 * (include/ipc/mailbox.h); the remaining fields are kernel-internal.
 * The kernel mail object lives in the low identity-mapped kernel heap, so
 * the view is directly readable AND writable from user mode:
 *   - outbound: fill magic / receiver_* / data / data_size, user_mail_send
 *   - inbound:  read magic / sender_* / data / data_size, user_mail_release
 * magic is the opaque notification tag (e.g. MAIL_MAGIC_IRQ, or a
 * user-defined event magic such as MSG_KEY_EVENT).
 */
typedef struct user_mail {
    u32  magic;
    int  sender_pid;
    int  sender_tid;
    int  receiver_pid;
    int  receiver_tid;
    char data[256];
    u32  data_size;   /* mirrors kernel's size_t (u32 on i686) */
} user_mail;

/* Block (yielding) until a mail arrives on the calling thread's own
 * mailbox; returns the opaque mail* (view as user_mail*). */
void* user_mail_listen(void);
void  user_mail_release(void* m);

/* Allocate a mail for sending (view as user_mail* and fill it in). */
void* user_mail_alloc(void);

/* Send a mail: addressed (receiver_* set) or broadcast to every mailbox
 * subscribed to m->magic (receiver_* = USER_MAIL_ANY_PID/TID). */
int   user_mail_send(void* m);

/* Subscribe / unsubscribe the calling thread's OWN mailbox to a magic so
 * that broadcasts carrying that magic are delivered to it. */
int   user_mail_subscribe(u32 magic);
int   user_mail_unsubscribe(u32 magic);

int user_irq_wait(void);

#endif
