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

/* ---- LOG (user-mode log server, SYSCALL_LOG) ---- */

void user_log_write(const char* s, u32 len)
{
    user_log_config cfg = {0};

    if (!s || len == 0)
        return;
    if (len > sizeof(cfg.data))
        len = sizeof(cfg.data);
    cfg.size = len;
    for (u32 i = 0; i < len; i++)
        cfg.data[i] = s[i];

    /* SYSCALL_LOG exists only once log_server2.elf has claimed it. */
    for (int tries = 0; tries < 10000; tries++) {
        if (user_syscall(SYSCALL_LOG, &cfg, sizeof(cfg)) == 0)
            return;
        user_yield();
    }
}

void user_log_str(const char* s)
{
    u32 len = 0;

    if (!s)
        return;
    while (s[len])
        len++;
    user_log_write(s, len);   /* truncates to the inline data[] size */
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

void* user_mail_alloc(void)
{
    user_mailbox_ctrl cfg = {0};

    cfg.cmd = U_MAILBOX_CTRL_ALLOC_MAIL;
    user_syscall(SYSCALL_MAILBOX, &cfg, sizeof(cfg));

    return cfg.m;
}

int user_mail_send(void* m)
{
    user_mailbox_ctrl cfg = {0};

    cfg.cmd = U_MAILBOX_CTRL_SEND;
    cfg.m   = m;
    user_syscall(SYSCALL_MAILBOX, &cfg, sizeof(cfg));

    return cfg.ret;
}

int user_mail_subscribe(u32 magic)
{
    user_mailbox_ctrl cfg = {0};

    cfg.cmd   = U_MAILBOX_CTRL_SUBSCRIBE_MAIL;
    cfg.magic = magic;      /* mb == NULL: own mailbox */
    user_syscall(SYSCALL_MAILBOX, &cfg, sizeof(cfg));

    return cfg.ret;
}

int user_mail_unsubscribe(u32 magic)
{
    user_mailbox_ctrl cfg = {0};

    cfg.cmd   = U_MAILBOX_CTRL_UNSUBSCRIBE_MAIL;
    cfg.magic = magic;      /* mb == NULL: own mailbox */
    user_syscall(SYSCALL_MAILBOX, &cfg, sizeof(cfg));

    return cfg.ret;
}


int user_irq_wait(void)
{
    for (;;) {
        user_mailbox_ctrl cfg = {0};

        cfg.cmd = U_MAILBOX_CTRL_LISTEN;   /* mb == NULL: own mailbox */
        user_syscall(SYSCALL_MAILBOX, &cfg, sizeof(cfg));

        if (cfg.m) {
            /* Only the kernel's IRQ notification (MAIL_MAGIC_IRQ) counts;
             * any other mail is released and we keep waiting. */
            if (((user_mail*)cfg.m)->magic == MAIL_MAGIC_IRQ) {
                user_mail_release(cfg.m);
                return 0;
            }
            user_mail_release(cfg.m);
        }

        /* No mail yet: yield so IRQ handlers get a chance to deliver. */
        user_yield();
    }
}