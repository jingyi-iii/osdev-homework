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

#endif
