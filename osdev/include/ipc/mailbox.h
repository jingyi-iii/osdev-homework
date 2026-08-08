#ifndef MAILBOX_H
#define MAILBOX_H

#include <stddef.h>
#include <stdint.h>
#include "lib/list.h"
#include "sync/spinlock.h"
#include "kernel/process.h"

#define MAIL_ANY_PID    (-0xab)
#define MAIL_ANY_TID    (-0xcd)

typedef struct mail {
    int sender_pid;
    int sender_tid;
    int receiver_pid;
    int receiver_tid;
    char data[256];
    size_t data_size;

    // do not use these fields directly, they are for internal use only
    size_t unique_id;
    int ref_count;
    spinlock* sp_lock;
    list_node this_node;
} mail;

/*
 * Mail handler callback. Handlers run synchronously in ISR context during
 * mailbox delivery. Handlers MUST NOT call mailbox_release_mail() —
 * send_mail() releases the reference after all handlers return.
 */
typedef void (*mail_handler)(mail* m);

typedef struct mailhandler {
    mail_handler handler;
    list_node this_node;
} mailhandler;

typedef struct mailbox {
    int owner_pid;
    int owner_tid;
    spinlock* sp_lock;
    list_node mails;
    list_node handlers;
} mailbox;

enum mailbox_ctrl_cmd {
    MAILBOX_CTRL_SEND = 0,
    MAILBOX_CTRL_LISTEN,
    MAILBOX_CTRL_REGISTER_HANDLER,
    MAILBOX_CTRL_UNREGISTER_HANDLER,
    MAILBOX_CTRL_ALLOC_MAIL,
    MAILBOX_CTRL_RELEASE_MAIL,
    MAILBOX_CTRL_ALLOC,
    MAILBOX_CTRL_RELEASE,
};

typedef struct mailbox_ctrl_config {
    uint8_t     cmd;
    mail*       m;          /* in: mail to send / out: received mail from listen / alloc_mail */
    mailbox*    mb;         /* in: target mailbox / out: allocated mailbox */
    mail_handler handler;   /* in: handler function */
    int         pid;        /* in: owner pid for mailbox_alloc */
    int         tid;        /* in: owner tid for mailbox_alloc */
    int         ret;        /* out: return value */
} mailbox_ctrl_config;

void        mailbox_syscall_init        (void);
void        mailbox_syscall_exit        (void);
mail*       mailbox_alloc_mail          (void);
void        mailbox_release_mail        (mail* m);
mailbox*    mailbox_alloc               (int owner_pid, int owner_tid);
void        mailbox_release             (mailbox* mb);
int         mailbox_send                (mail* m);
mail*       mailbox_listen              (mailbox* mb);
int         mailbox_register_handler    (mailbox* mb, mail_handler handler);
int         mailbox_unregister_handler  (mailbox* mb, mail_handler handler);

#endif
