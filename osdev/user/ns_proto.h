/*
 * user/ns_proto.h — user-space namespace protocol (user <-> user).
 *
 * Shared verbatim between namespace_server.elf (the registry holder) and
 * every client that registers / resolves service bindings.
 *
 * This is deliberately NOT part of the kernel ABI (kernel/uapi.h): the
 * namespace registry lives entirely in a USER process, so its names and
 * wire format belong to user space.  Adding a service name here only
 * rebuilds the user ELFs that use it — the kernel never sees it.  The one
 * kernel-side constant is the bootstrap portal id PORTAL_ID_NAMESPACE
 * (kernel/uapi.h).
 *
 * Registry entries: name -> {portal_id, mailbox_tid, mail_magic}.
 *   - portal_id   : portal to portal_call() for sync RPC (0 = none)
 *   - mailbox_tid : the service's mailbox-owning thread tid, i.e. the value
 *                   to set as mail.receiver_tid for a point-to-point mail
 *   - mail_magic  : the magic to user_mail_subscribe() for broadcast events
 */
#ifndef NS_PROTO_H
#define NS_PROTO_H

#include "lib/types.h"

/* Well-known service names (only cross-ELF services need a shared name). */
#define NS_NAME_CONSOLE    "console"
#define NS_NAME_LOG        "log"
#define NS_NAME_KEYBOARD   "kb"
#define NS_NAME_RTC        "rtc"

enum {
    NS_REGISTER   = 1,   /* bind (or rebind) a name -> ids        */
    NS_LOOKUP     = 2,   /* resolve a name (fills out_* fields)   */
    NS_UNREGISTER = 3,   /* drop a name                           */
};

/* Request AND response share one portal payload buffer.  The portal reply
 * int carries the status: 0 = ok, -1 = not found, -2 = table full,
 * -3 = malformed request. */
typedef struct ns_request {
    u32  cmd;              /* NS_*                                */
    char name[32];
    u32  portal_id;        /* REGISTER: RPC portal id (0 = none)  */
    u32  mailbox_tid;      /* REGISTER: mailbox-owner thread tid
                            * (0 = none)                          */
    u32  mail_magic;       /* REGISTER: broadcast subscribe magic
                            * (0 = none)                          */
    u32  out_portal_id;    /* LOOKUP result (server writes back)  */
    u32  out_mailbox_tid;  /* LOOKUP result                       */
    u32  out_mail_magic;   /* LOOKUP result                       */
} ns_request;

#endif
