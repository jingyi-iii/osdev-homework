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
    U_PROC_CTRL_EXIT,
    U_PROC_CTRL_BLOCK,
    U_PROC_CTRL_UNBLOCK,
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
 * ---- IRQ syscall (SYSCALL_IRQ) ---------------------------------------
 *
 * Layout MUST mirror the kernel's irq_syscall_data (include/kernel/irq.h).
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
 * (include/ipc/mailbox.h).  Only the commands user programs need.
 */
enum {
    U_MAILBOX_CTRL_LISTEN       = 1,   /* dequeue a mail (non-blocking) */
    U_MAILBOX_CTRL_RELEASE_MAIL = 5,   /* free a received mail          */
};

typedef struct user_mailbox_ctrl {
    u8   cmd;
    void* m;          /* mail* — opaque (out on LISTEN / in on RELEASE) */
    void* mb;         /* mailbox* — NULL = calling thread's own mailbox */
    void* handler;
    int  pid;
    int  tid;
    int  ret;
} user_mailbox_ctrl;

/* Opaque mail view: only the leading magic field is meaningful to users —
 * it mirrors mail.magic (include/ipc/mailbox.h), the kernel's opaque
 * notification tag (e.g. MAIL_MAGIC_IRQ for kernel IRQ notifications). */
typedef struct user_mail {
    int magic;
} user_mail;

/* Block (yielding) until a mail arrives on the calling thread's own
 * mailbox; returns the opaque mail*. */
void* user_mail_listen(void);
void  user_mail_release(void* m);
int user_irq_wait(void);

#endif
