#include "userlib.h"

/* Console portal id cache — shared by console_putstr() and the gfx_*
 * control-frame helpers (one namespace lookup per process). */
static u32 console_portal = 0;

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

/* ====================================================================
 * Namespace client — console/log no longer hardcode portal ids; they are
 * resolved from namespace_server.elf (PORTAL_ID_NAMESPACE) and cached.
 * ==================================================================== */

static u32 ns_strlen(const char* s)
{
    u32 n = 0;

    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static int ns_call(ns_request* req)
{
    return user_portal_call(PORTAL_ID_NAMESPACE, (void*)req, sizeof(*req));
}

int ns_register(const char* name, u32 portal_id, u32 mailbox_tid,
                u32 mail_magic)
{
    ns_request req = {0};
    u32 len;

    if (!name || (len = ns_strlen(name)) == 0 || len >= sizeof(req.name))
        return -3;
    req.cmd = NS_REGISTER;
    for (u32 i = 0; i < len; i++)
        req.name[i] = name[i];
    req.portal_id   = portal_id;
    req.mailbox_tid = mailbox_tid;
    req.mail_magic  = mail_magic;
    return ns_call(&req);
}

int ns_lookup(const char* name, u32* out_portal_id,
              u32* out_mailbox_tid, u32* out_mail_magic)
{
    ns_request req = {0};
    u32 len;
    int ret;

    if (!name || (len = ns_strlen(name)) == 0 || len >= sizeof(req.name))
        return -3;
    req.cmd = NS_LOOKUP;
    for (u32 i = 0; i < len; i++)
        req.name[i] = name[i];

    ret = ns_call(&req);
    if (ret != 0)
        return ret;

    if (out_portal_id)
        *out_portal_id = req.out_portal_id;
    if (out_mailbox_tid)
        *out_mailbox_tid = req.out_mailbox_tid;
    if (out_mail_magic)
        *out_mail_magic = req.out_mail_magic;
    return 0;
}

/* Resolve @name's portal id into the per-process @cached slot. */
static u32 ns_portal_id(const char* name, u32* cached)
{
    u32 pid = 0, mt = 0, mm = 0;

    if (*cached)
        return *cached;
    if (ns_lookup(name, &pid, &mt, &mm) == 0 && pid)
        *cached = pid;
    return *cached;
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

    /* Console output is a portal RPC to the terminal server ("console").
     * The servers are spawned at boot before us, but scheduling order is
     * not guaranteed, so retry a bounded number of times while the
     * namespace / console server come up. */
    while (tries++ < 10000) {
        u32 id = ns_portal_id(NS_NAME_CONSOLE, &console_portal);
        if (id && user_portal_call(id, (void*)s, len) == 0)
            return;
        console_portal = 0;   /* server restarting: re-resolve next round */
        user_yield();
    }
}

/* ---- LOG (user-mode log server, resolved via namespace "log") ---- */

void user_log_write(const char* s, u32 len)
{
    static u32 log_portal = 0;
    int tries = 0;

    if (!s || len == 0)
        return;

    /* The log server registers itself in the namespace only after it is
     * loaded at boot, so retry briefly (same pattern as console_putstr). */
    while (tries++ < 10000) {
        u32 id = ns_portal_id(NS_NAME_LOG, &log_portal);
        if (id && user_portal_call(id, (void*)s, len) == 0)
            return;
        log_portal = 0;
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
    user_log_write(s, len);
}

/* ---- graphics (console portal control frames, terminal server) ---- */

/* Send one graphics control frame as a plain portal payload (SET_MODE
 * and other header-only commands).  Returns the server's reply int. */
static int gfx_ctrl_send(u8 cmd, u32 a, u32 b)
{
    gfx_ctrl g;
    u32 id = ns_portal_id(NS_NAME_CONSOLE, &console_portal);

    if (!id)
        return -1;

    g.esc      = 0x1b;
    g.tag      = 'G';
    g.cmd      = cmd;
    g.reserved = 0;
    g.a        = a;
    g.b        = b;
    return user_portal_call(id, &g, sizeof(g));
}

int gfx_set_mode(u32 mode)
{
    return gfx_ctrl_send(GFX_CTRL_SET_MODE, mode, 0);
}

int gfx_blit(u32 fb_size)
{
    return gfx_ctrl_send(GFX_CTRL_BLIT, fb_size, 0);
}

/* Blit a frame buffer: the bytes are copied into a static header+fb
 * staging buffer which is shm_share()d with the terminal process for the
 * duration of the call; the server copies the fb to 0xA0000. */
int gfx_blit_shared(const u8* fb, u32 fb_size)
{
    /* Static staging: must live in one contiguous STATIC region so the
     * whole header+fb page range is shared (a stack header would leave
     * the fb outside the shared pages).  Single-threaded gfx clients
     * only. */
    static struct { gfx_ctrl hdr; u8 fb[GFX_FB_SIZE]; } stage;
    u32 id = ns_portal_id(NS_NAME_CONSOLE, &console_portal);

    if (!id || !fb || fb_size > GFX_FB_SIZE)
        return -1;

    stage.hdr.esc      = 0x1b;
    stage.hdr.tag      = 'G';
    stage.hdr.cmd      = GFX_CTRL_BLIT;
    stage.hdr.reserved = 0;
    stage.hdr.a        = fb_size;
    stage.hdr.b        = 0;
    for (u32 i = 0; i < fb_size; i++)
        stage.fb[i] = fb[i];

    /* ① enqueue — shm_share()s the static header+fb into the terminal */
    user_portal_ctrl cfg = {0};
    cfg.cmd       = U_PORTAL_CTRL_CALL;
    cfg.server_id = id;
    cfg.va        = &stage;
    cfg.va_size   = sizeof(stage.hdr) + fb_size;
    int ret = user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    if (ret)
        return ret;
    void* req = cfg.req;
    if (!req)
        return -1;

    /* ② park until the server replies (the blit is done by then) */
    cfg.cmd = U_PORTAL_CTRL_WAIT_REPLY;
    cfg.req = req;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

    /* ③ fetch the reply int */
    cfg.cmd = U_PORTAL_CTRL_GET_RESULT;
    cfg.req = req;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    ret = cfg.ret;

    /* ④ tear down the share */
    cfg.cmd = U_PORTAL_CTRL_CLEANUP;
    cfg.req = req;
    user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

    return ret;
}

/* ---- RTC (rtc server, resolved via namespace "rtc") ---- */
/* Resolve the "rtc" portal once and cache it (per-process).  Returns the
 * portal id, or 0 while the server has not registered yet. */
static u32 rtc_portal_id(void)
{
    static u32 cached = 0;
    u32 pid = 0;

    if (cached)
        return cached;
    if (ns_lookup(NS_NAME_RTC, &pid, 0, 0) == 0 && pid)
        cached = pid;
    return cached;
}

int user_rtc_time(rtc_time* out)
{
    rtc_request req;

    if (!out)
        return -3;

    u32 id = rtc_portal_id();
    if (!id)
        return -1;

    req.cmd = RTC_CMD_GET_TIME;
    int ret = user_portal_call(id, &req, sizeof(req));
    if (ret != 0)
        return ret;

    /* No memcpy in the freestanding user link — copy field by field. */
    out->year   = req.time.year;
    out->month  = req.time.month;
    out->day    = req.time.day;
    out->hour   = req.time.hour;
    out->minute = req.time.minute;
    out->second = req.time.second;
    return 0;
}

int user_rtc_sleep_ms(u32 ms)
{
    rtc_request req;

    req.cmd      = RTC_CMD_SLEEP_MS;
    req.sleep_ms = ms;
    u32 id = rtc_portal_id();
    if (!id)
        return -1;
    return user_portal_call(id, &req, sizeof(req));
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