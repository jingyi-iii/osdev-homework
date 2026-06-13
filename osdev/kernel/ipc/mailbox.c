#include "kernel/mailbox.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "kernel/errno.h"

mail* mailbox_alloc_mail(void)
{
    static size_t unique_id = 0;

    mail* m = (mail*)kmalloc(sizeof(mail));
    if (!m)
        return 0;

    memset(m, 0, sizeof(mail));
    m->ref_count = 1;   /* caller initially holds one reference */
    m->sp_lock = spinlock_alloc();
    if (!m->sp_lock) {
        kfree(m);
        return 0;
    }

    m->unique_id = __sync_fetch_and_add(&unique_id, 1);
    list_init(&m->this_node);

    return m;
}

void mailbox_release_mail(mail* m)
{
    if (m) {
        spinlock_lock(m->sp_lock);
        m->ref_count--;
        if (m->ref_count > 0) {
            spinlock_unlock(m->sp_lock);
            return;
        }
        spinlock_unlock(m->sp_lock);
        spinlock_release(m->sp_lock);
        memset(m, 0, sizeof(mail));
        kfree(m);
    }
}

mailbox* mailbox_alloc(int owner_pid, int owner_tid)
{
    mailbox* mb = (mailbox*)kmalloc(sizeof(mailbox));
    if (!mb)
        return 0;

    mb->sp_lock = spinlock_alloc();
    if (!mb->sp_lock) {
        kfree(mb);
        return 0;
    }

    mb->owner_pid = owner_pid;
    mb->owner_tid = owner_tid;
    list_init(&mb->mails);
    list_init(&mb->handlers);

    return mb;
}

void mailbox_release(mailbox* mb)
{
    if (mb) {
        spinlock_lock(mb->sp_lock);

        /* Release all pending mails, safely removing each from the list first */
        while (!list_empty(&mb->mails)) {
            mail* m = list_entry(mb->mails.prev, mail, this_node);
            list_del(&m->this_node);
            mailbox_release_mail(m);
        }

        /* Release all registered handlers */
        while (!list_empty(&mb->handlers)) {
            mailhander* mh = list_entry(mb->handlers.prev, mailhander, this_node);
            list_del(&mh->this_node);
            kfree(mh);
        }

        spinlock_unlock(mb->sp_lock);
        spinlock_release(mb->sp_lock);
        memset(mb, 0, sizeof(mailbox));
        kfree(mb);
    }
}

static int send_mail(mailbox* mb, mail* m)
{
    if (!mb || !m)
        return -EINVAL;

    spinlock_lock(mb->sp_lock);
    if (!list_empty(&mb->handlers)) {
        /*
         * Deliver to all registered handlers.
         * Each handler is responsible for calling mailbox_release_mail(m)
         * when it is done with the mail.
         */
        list_for_each(pos, &mb->handlers) {
            mailhander* mh = list_entry(pos, mailhander, this_node);
            if (mh->handler)
                mh->handler(m);
        }
    } else {
        /* No handler: queue the mail for later retrieval via mailbox_listen */
        list_add(&m->this_node, &mb->mails);
    }
    spinlock_unlock(mb->sp_lock);

    return 0;
}

int mailbox_send(mail* m)
{
    if (!m)
        return -EINVAL;

    if (m->receiver_pid == MAIL_ANY_PID || m->receiver_tid == MAIL_ANY_TID) {
        /*
         * Broadcast: deliver to all threads that have a mailbox.
         * ref_count tracks how many deliveries are outstanding.
         * Each handler must call mailbox_release_mail() to decrement.
         * For threads without handlers, a clone is queued instead.
         */
        int recipients = 0;

        spinlock_lock(schedule_lock);

        /* First pass: count threads with mailboxes */
        list_for_each(node, &thread_head) {
            tcb* t = list_entry(node, tcb, this_node);
            if (t && t->mailbox)
                recipients++;
        }

        if (recipients == 0) {
            spinlock_unlock(schedule_lock);
            mailbox_release_mail(m);
            return 0;
        }

        m->ref_count = recipients;

        /* Second pass: deliver to each recipient */
        list_for_each(node, &thread_head) {
            tcb* t = list_entry(node, tcb, this_node);
            if (!t || !t->mailbox)
                continue;

            int has_handler;
            spinlock_lock(t->mailbox->sp_lock);
            has_handler = !list_empty(&t->mailbox->handlers);
            spinlock_unlock(t->mailbox->sp_lock);

            if (has_handler) {
                /* Handler will call mailbox_release_mail(m) → ref_count-- */
                send_mail(t->mailbox, m);
            } else {
                /*
                 * No handler: clone the mail so each queue gets its own copy.
                 * The clone has ref_count = 1; the listener's eventual
                 * mailbox_release_mail will free it.
                 */
                mail* clone = mailbox_alloc_mail();
                if (clone) {
                    memcpy(clone->data, m->data, sizeof(m->data));
                    clone->data_size = m->data_size;
                    clone->sender_pid = m->sender_pid;
                    clone->sender_tid = m->sender_tid;
                    clone->receiver_pid = m->receiver_pid;
                    clone->receiver_tid = m->receiver_tid;
                    clone->ref_count = 1;
                    spinlock_lock(t->mailbox->sp_lock);
                    list_add(&clone->this_node, &t->mailbox->mails);
                    spinlock_unlock(t->mailbox->sp_lock);
                }
                /* One fewer outstanding reference for the original mail */
                m->ref_count--;
            }
        }

        spinlock_unlock(schedule_lock);

        /* Release the sender's reference */
        mailbox_release_mail(m);
    } else {
        tcb* receiver = thread_get_by_tid(m->receiver_tid);
        if (!receiver || !receiver->mailbox) {
            mailbox_release_mail(m);
            return E_NOTFOUND;
        }
        return send_mail(receiver->mailbox, m);
    }

    return 0;
}

mail* mailbox_listen(mailbox* mb)
{
    if (!mb)
        return 0;
    
    spinlock_lock(mb->sp_lock);
    if (list_empty(&mb->mails)) {
        spinlock_unlock(mb->sp_lock);
        return 0;
    }

    mail* m = list_entry(mb->mails.prev, mail, this_node);
    list_del(&m->this_node);
    spinlock_unlock(mb->sp_lock);
    
    return m;
}

int mailbox_register_handler(mailbox* mb, mail_handler handler)
{
    if (!mb || !handler)
        return -EINVAL;

    mailhander* mh = (mailhander*)kmalloc(sizeof(mailhander));
    if (!mh)
        return -ENOMEM;

    mh->handler = handler;
    list_init(&mh->this_node);

    spinlock_lock(mb->sp_lock);
    list_add(&mh->this_node, &mb->handlers);
    spinlock_unlock(mb->sp_lock);

    return 0;
}

int mailbox_unregister_handler(mailbox* mb, mail_handler handler)
{
    if (!mb || !handler)
        return -EINVAL;

    spinlock_lock(mb->sp_lock);
    list_for_each(pos, &mb->handlers) {
        mailhander* mh = list_entry(pos, mailhander, this_node);
        if (mh->handler == handler) {
            list_del(pos);
            spinlock_unlock(mb->sp_lock);
            kfree(mh);
            return 0;
        }
    }
    spinlock_unlock(mb->sp_lock);

    return 0;
}

