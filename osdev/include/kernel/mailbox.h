#ifndef MAILBOX_H
#define MAILBOX_H

#include <stddef.h>
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

typedef void (*mail_handler)(mail* m);

typedef struct mailhander {
    mail_handler handler;
    list_node this_node;
} mailhander;

typedef struct mailbox {
    int owner_pid;
    int owner_tid;
    spinlock* sp_lock;
    list_node mails;
    list_node handlers;
} mailbox;

mail* mailbox_alloc_mail(void);
mailbox* mailbox_alloc(int owner_pid, int owner_tid);
void mailbox_release_mail(mail* m);
void mailbox_release(mailbox* mb);
int mailbox_send(mail* m);
mail* mailbox_listen(mailbox* mb);
int mailbox_register_handler(mailbox* mb, mail_handler handler);
int mailbox_unregister_handler(mailbox* mb, mail_handler handler);

#endif
