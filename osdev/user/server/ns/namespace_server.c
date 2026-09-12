/*
 * user/server/ns/namespace_server.c — ring-3 user namespace service
 * (standalone user ELF).
 *
 * Publishes the single fixed well-known portal PORTAL_ID_NAMESPACE and
 * serves a name -> {portal_id, mailbox_tid, mail_magic} registry:
 *
 *   - REGISTER / UNREGISTER: called by the other services (terminal ->
 *     "console", log_server2 -> "log", kb_server -> "kb") with the ids
 *     they were handed at runtime (portal ids are now DYNAMIC, only this
 *     portal is fixed);
 *   - LOOKUP: called by clients that used to hardcode well-known ids
 *     (console_putstr, user_log_write, keyboard consumers...).
 *
 * The request AND the response share the portal payload buffer (portal RPC
 * shm-maps the client's buffer into this process), see ns_request in
 * include/kernel/uapi.h.  The portal reply int carries the status.
 *
 * Single service thread, so the table needs no lock.
 */
#include "userlib.h"          /* user_syscall() / portal ABI / ns_proto */
#include "kernel/uapi.h"      /* PORTAL_ID_NAMESPACE                   */
#include <stddef.h>           /* size_t */

#define NS_MAX_ENTRIES   16
#define NS_NAME_LEN      (sizeof(((ns_request*)0)->name))   /* 32 */

typedef struct ns_entry {
    char name[NS_NAME_LEN];
    u32  portal_id;      /* RPC portal id (0 = none)              */
    u32  mailbox_tid;    /* mailbox-owner thread tid (0 = none)   */
    u32  mail_magic;     /* broadcast subscribe magic (0 = none)  */
} ns_entry;

static ns_entry ns_table[NS_MAX_ENTRIES];
static unsigned ns_count = 0;

static u32 ns_strlen(const char* s)
{
    u32 n = 0;

    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static int ns_find(const char* name)
{
    for (unsigned i = 0; i < ns_count; i++) {
        unsigned j = 0;

        while (ns_table[i].name[j] && name[j] &&
               ns_table[i].name[j] == name[j])
            j++;
        if (ns_table[i].name[j] == 0 && name[j] == 0)
            return (int)i;
    }
    return -1;
}

/*
 * Run one request against the table.  LOOKUP writes the out_* fields back
 * into @req (which points into the shm-mapped client buffer).  Returns the
 * status to send back through the portal reply.
 */
static int ns_handle(ns_request* req, u32 size)
{
    u32 len;
    int idx;

    if (!req)
        return -3;

    /* The out_* response fields must fit in the mapped payload. */
    if (size < sizeof(ns_request))
        return -3;

    len = ns_strlen(req->name);
    if (len == 0 || len >= NS_NAME_LEN)
        return -3;

    switch (req->cmd) {
    case NS_REGISTER:
        idx = ns_find(req->name);
        if (idx < 0) {
            if (ns_count >= NS_MAX_ENTRIES)
                return -2;              /* table full */
            idx = (int)ns_count++;
        }
        for (u32 i = 0; i < len; i++)
            ns_table[idx].name[i] = req->name[i];
        ns_table[idx].name[len] = 0;
        ns_table[idx].portal_id   = req->portal_id;
        ns_table[idx].mailbox_tid = req->mailbox_tid;
        ns_table[idx].mail_magic  = req->mail_magic;
        return 0;

    case NS_LOOKUP:
        idx = ns_find(req->name);
        if (idx < 0)
            return -1;                  /* not registered yet */
        req->out_portal_id   = ns_table[idx].portal_id;
        req->out_mailbox_tid = ns_table[idx].mailbox_tid;
        req->out_mail_magic  = ns_table[idx].mail_magic;
        return 0;

    case NS_UNREGISTER:
        idx = ns_find(req->name);
        if (idx < 0)
            return -1;
        for (unsigned i = (unsigned)idx; i + 1 < ns_count; i++)
            ns_table[i] = ns_table[i + 1];
        ns_count--;
        return 0;

    default:
        return -3;
    }
}

/*
 * User namespace service entry.
 *
 * 1. Publish the fixed PORTAL_ID_NAMESPACE portal (the only well-known
 *    portal id in the system — it is the bootstrap for everything else).
 * 2. Serve REGISTER / LOOKUP / UNREGISTER forever.
 */
void _start(void)
{
    user_portal_ctrl cfg = {0};

    cfg.cmd       = U_PORTAL_CTRL_INIT;
    cfg.server_id = PORTAL_ID_NAMESPACE;
    if (user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg)) != 0) {
        /* Portal id already taken (should not happen at boot): idle. */
        for (;;)
            user_yield();
    }

    for (;;) {
        cfg.cmd = U_PORTAL_CTRL_WAIT;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));

        cfg.cmd = U_PORTAL_CTRL_GET_REQ;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
        if (!cfg.req)
            continue;

        cfg.ret = ns_handle((ns_request*)cfg.va, (u32)cfg.va_size);

        cfg.cmd = U_PORTAL_CTRL_REPLY;
        user_syscall(SYSCALL_PORTAL, &cfg, sizeof(cfg));
    }
}
