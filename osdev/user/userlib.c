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

/* ---- IRQ ---- */

void* user_irq_request(u32 major, u32 minor)
{
    user_irq_ctrl cfg = {0};

    cfg.cmd   = U_IRQ_CTRL_REQUEST;
    cfg.major = major;
    cfg.minor = minor;
    user_syscall(SYSCALL_IRQ, &cfg, sizeof(cfg));

    return cfg.handle;
}

int user_irq_unmask(void* handle)
{
    user_irq_ctrl cfg = {0};

    cfg.cmd    = U_IRQ_CTRL_UNMASK;
    cfg.handle = handle;
    user_syscall(SYSCALL_IRQ, &cfg, sizeof(cfg));

    return cfg.ret;
}

/* ---- mailbox ---- */

void* user_mail_listen(void)
{
    for (;;) {
        user_mailbox_ctrl cfg = {0};

        cfg.cmd = U_MAILBOX_CTRL_LISTEN;   /* mb == NULL: own mailbox */
        user_syscall(SYSCALL_MAILBOX, &cfg, sizeof(cfg));

        if (cfg.m)
            return cfg.m;

        /* No mail yet: yield so IRQ handlers get a chance to deliver. */
        user_yield();
    }
}

void user_mail_release(void* m)
{
    user_mailbox_ctrl cfg = {0};

    cfg.cmd = U_MAILBOX_CTRL_RELEASE_MAIL;
    cfg.m   = m;
    user_syscall(SYSCALL_MAILBOX, &cfg, sizeof(cfg));
}
