#include "userlib.h"

int user_portal_call(u32 portal_id, void* va, u32 size)
{
    user_portal_ctrl cfg = {0};
    void* req;
    int ret;

    /* ① enqueue (kernel: shm_share + req alloc + register + wake server) */
    cfg.cmd = U_PORTAL_CTRL_CALL;
    cfg.server_id = portal_id;
    cfg.va = va;
    cfg.va_size = size;
    ret = user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    if (ret)
        return ret;
    req = cfg.req;
    if (!req)
        return -1;

    /* ② park until the server replies (resumes in user mode after the
     *    syscall; the reply is already stored) */
    cfg.cmd = U_PORTAL_CTRL_WAIT_REPLY;
    cfg.req = req;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

    /* ③ fetch the response code — only valid once woken */
    cfg.cmd = U_PORTAL_CTRL_GET_RESULT;
    cfg.req = req;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    ret = cfg.ret;

    /* ④ tear down (kernel: shm_unshare + free req) */
    cfg.cmd = U_PORTAL_CTRL_CLEANUP;
    cfg.req = req;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

    return ret;
}

void console_putstr(const char* s)
{
    u32 len = 0;
    int tries = 0;

    if (!s)
        return;
    while (s[len])
        len++;
    if (len == 0)
        return;

    /* Console output is a portal RPC to the terminal server's well-known
     * console portal.  The server is spawned at boot before us, but
     * scheduling order is not guaranteed, so retry a bounded number of
     * times while it comes up. */
    while (tries++ < 10000) {
        if (user_portal_call(PORTAL_ID_CONSOLE, (void*)s, len) == 0)
            return;
        user_yield();
    }
}
