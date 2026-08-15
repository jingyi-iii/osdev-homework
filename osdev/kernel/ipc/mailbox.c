#include "ipc/mailbox.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "kernel/errno.h"
#include "arch_irq.h"
#include "kernel/irq.h"

mail* alloc_mail(void)
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

static void release_mail(mail* m)
{
    if (m) {
        u32 eflags = spinlock_lock_irqsave(m->sp_lock);
        m->ref_count--;
        if (m->ref_count > 0) {
            spinlock_unlock_irqrestore(m->sp_lock, eflags);
            return;
        }
        spinlock_unlock_irqrestore(m->sp_lock, eflags);
        spinlock_release(m->sp_lock);
        memset(m, 0, sizeof(mail));
        kfree(m);
    }
}

mailbox* alloc_mailbox(int owner_pid, int owner_tid)
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

void release_mailbox(mailbox* mb)
{
    if (mb) {
        u32 eflags = spinlock_lock_irqsave(mb->sp_lock);

        /* Release all pending mails, safely removing each from the list first */
        while (!list_empty(&mb->mails)) {
            mail* m = list_entry(mb->mails.prev, mail, this_node);
            list_del(&m->this_node);
            release_mail(m);
        }

        /* Release all registered handlers */
        while (!list_empty(&mb->handlers)) {
            mailhandler* mh = list_entry(mb->handlers.prev, mailhandler, this_node);
            list_del(&mh->this_node);
            kfree(mh);
        }

        spinlock_unlock_irqrestore(mb->sp_lock, eflags);
        spinlock_release(mb->sp_lock);
        memset(mb, 0, sizeof(mailbox));
        kfree(mb);
    }
}

int send_mail(mailbox* mb, mail* m)
{
    if (!mb || !m)
        return -EINVAL;

    /* IRQ-safe lock: every holder disables interrupts, so even from an ISR
     * this blocking acquisition can never deadlock on a preempted holder
     * (replaces the previous unbounded busy-wait). */
    u32 eflags = spinlock_lock_irqsave(mb->sp_lock);
    if (!list_empty(&mb->handlers)) {
        /*
         * Deliver to all registered handlers synchronously.
         * Handlers run in ISR context and must NOT call release_mail().
         * send_mail() releases one reference after all handlers return,
         * consuming exactly one ref_count per mailbox delivery
         * regardless of how many handlers are registered.
         */
        list_for_each(pos, &mb->handlers) {
            mailhandler* mh = list_entry(pos, mailhandler, this_node);
            if (mh->handler)
                mh->handler(m);
        }
        spinlock_unlock_irqrestore(mb->sp_lock, eflags);

        /* All handlers have run; consume one reference for this delivery. */
        release_mail(m);
        return 0;
    } else {
        /* No handler: queue the mail for later retrieval via mailbox_listen */
        list_add(&m->this_node, &mb->mails);
        spinlock_unlock_irqrestore(mb->sp_lock, eflags);
    }

    return 0;
}

int send(mail* m)
{
    if (!m)
        return -EINVAL;

    if (m->receiver_pid == MAIL_ANY_PID || m->receiver_tid == MAIL_ANY_TID) {
        /*
         * Broadcast: deliver to all threads that have a mailbox.
         * ref_count tracks how many deliveries are outstanding:
         *   - one per handler-having mailbox (consumed by send_mail)
         *   - one for the sender (consumed by release_mail at end)
         * Handlers run synchronously and must NOT call release_mail().
         * For threads without handlers, a clone is queued instead.
         */
        int handler_recipients = 0;
        int has_any_mailbox = 0;

        u32 eflags = spinlock_lock_irqsave(schedule_lock);

        /*
         * First pass: count only threads whose mailboxes have handlers.
         * Threads without handlers receive clones, which are independent
         * and do not affect the original mail's ref_count.
         */
        list_for_each(node, &thread_head) {
            tcb* t = list_entry(node, tcb, this_node);
            if (!t || !t->mailbox)
                continue;

            has_any_mailbox = 1;

            u32 eflags = spinlock_lock_irqsave(t->mailbox->sp_lock);
            if (!list_empty(&t->mailbox->handlers))
                handler_recipients++;
            spinlock_unlock_irqrestore(t->mailbox->sp_lock, eflags);
        }

        if (!has_any_mailbox) {
            spinlock_unlock_irqrestore(schedule_lock, eflags);
            release_mail(m);
            return 0;
        }

        /*
         * ref_count accounts for:
         *   - one reference per handler-having mailbox (consumed by
         *     send_mail's internal release_mail after handlers return)
         *   - one extra reference for the sender (consumed by release_mail below)
         */
        m->ref_count = handler_recipients + 1;

        /* Second pass: deliver to each recipient */
        list_for_each(node, &thread_head) {
            tcb* t = list_entry(node, tcb, this_node);
            if (!t || !t->mailbox)
                continue;

            int has_handler;
            u32 eflags = spinlock_lock_irqsave(t->mailbox->sp_lock);
            has_handler = !list_empty(&t->mailbox->handlers);
            spinlock_unlock_irqrestore(t->mailbox->sp_lock, eflags);

            if (has_handler) {
                /* send_mail() calls handlers then release_mail(m) */
                send_mail(t->mailbox, m);
            } else {
                /*
                 * No handler: clone the mail so each queue gets its own copy.
                 * The clone has ref_count = 1; the listener's eventual
                 * release_mail will free it.
                 * The original mail's ref_count is unaffected — clones are
                 * independent mail objects.
                 */
                mail* clone = alloc_mail();
                if (clone) {
                    memcpy(clone->data, m->data, sizeof(m->data));
                    clone->data_size = m->data_size;
                    clone->sender_pid = m->sender_pid;
                    clone->sender_tid = m->sender_tid;
                    clone->receiver_pid = m->receiver_pid;
                    clone->receiver_tid = m->receiver_tid;
                    clone->ref_count = 1;
                    u32 eflags = spinlock_lock_irqsave(t->mailbox->sp_lock);
                    list_add(&clone->this_node, &t->mailbox->mails);
                    spinlock_unlock_irqrestore(t->mailbox->sp_lock, eflags);
                }
            }
        }

        spinlock_unlock_irqrestore(schedule_lock, eflags);

        /* Release the sender's reference */
        release_mail(m);
    } else {
        tcb* receiver = thread_get_by_tid(m->receiver_tid);
        if (!receiver || !receiver->mailbox) {
            release_mail(m);
            return E_NOTFOUND;
        }
        return send_mail(receiver->mailbox, m);
    }

    return 0;
}

static mail* try_get_mail(mailbox* mb)
{
    if (!mb)
        return 0;

    u32 eflags = spinlock_lock_irqsave(mb->sp_lock);
    if (list_empty(&mb->mails)) {
        spinlock_unlock_irqrestore(mb->sp_lock, eflags);
        return 0;
    }

    mail* m = list_entry(mb->mails.prev, mail, this_node);
    list_del(&m->this_node);
    spinlock_unlock_irqrestore(mb->sp_lock, eflags);

    return m;
}

static int register_handler(mailbox* mb, mail_handler handler)
{
    if (!mb || !handler)
        return -EINVAL;

    mailhandler* mh = (mailhandler*)kmalloc(sizeof(mailhandler));
    if (!mh)
        return -ENOMEM;

    mh->handler = handler;
    list_init(&mh->this_node);

    u32 eflags = spinlock_lock_irqsave(mb->sp_lock);
    list_add(&mh->this_node, &mb->handlers);
    spinlock_unlock_irqrestore(mb->sp_lock, eflags);

    return 0;
}

static int unregister_handler(mailbox* mb, mail_handler handler)
{
    if (!mb || !handler)
        return -EINVAL;

    u32 eflags = spinlock_lock_irqsave(mb->sp_lock);
    list_for_each(pos, &mb->handlers) {
        mailhandler* mh = list_entry(pos, mailhandler, this_node);
        if (mh->handler == handler) {
            list_del(pos);
            spinlock_unlock_irqrestore(mb->sp_lock, eflags);
            kfree(mh);
            return 0;
        }
    }
    spinlock_unlock_irqrestore(mb->sp_lock, eflags);

    return 0;
}

/*
 * Mailbox syscall layer
 */
#define MAILBOX_SYSCALL_MINOR   (3)

static irq* mailbox_syscall_irq = 0;

static void mailbox_syscall_isr(void* data)
{
    mailbox_ctrl_config* config = (mailbox_ctrl_config*)data;
    if (!config)
        return;

    switch (config->cmd) {
    case MAILBOX_CTRL_SEND:
        config->ret = send(config->m);
        break;
    case MAILBOX_CTRL_LISTEN:
        /*
         * Non-blocking on purpose: this runs inside the syscall ISR with
         * interrupts disabled, so a blocking loop that calls thread_yield()
         * would spin forever (a nested int $100 is swallowed by the irq
         * reentrancy guard in arch_syscall_entry).  The mailbox_listen()
         * wrapper loops and yields in user mode instead.
         */
        config->m = try_get_mail(config->mb);
        config->ret = (config->m != 0) ? 0 : -1;
        break;
    case MAILBOX_CTRL_REGISTER_HANDLER:
        config->ret = register_handler(config->mb, config->handler);
        break;
    case MAILBOX_CTRL_UNREGISTER_HANDLER:
        config->ret = unregister_handler(config->mb, config->handler);
        break;
    case MAILBOX_CTRL_ALLOC_MAIL:
        config->m = alloc_mail();
        config->ret = (config->m != 0) ? 0 : -ENOMEM;
        break;
    case MAILBOX_CTRL_RELEASE_MAIL:
        release_mail(config->m);
        config->ret = 0;
        break;
    case MAILBOX_CTRL_ALLOC:
        config->mb = alloc_mailbox(config->pid, config->tid);
        config->ret = (config->mb != 0) ? 0 : -ENOMEM;
        break;
    case MAILBOX_CTRL_RELEASE:
        release_mailbox(config->mb);
        config->ret = 0;
        break;
    default:
        config->ret = -EINVAL;
        break;
    }
}

void mailbox_syscall_init(void)
{
    irq_request(&mailbox_syscall_irq, "mailbox_syscall", 100,
                MAILBOX_SYSCALL_MINOR, mailbox_syscall_isr, 0);
    if (mailbox_syscall_irq)
        irq_unmask(mailbox_syscall_irq);
}

void mailbox_syscall_exit(void)
{
    if (mailbox_syscall_irq) {
        irq_mask(mailbox_syscall_irq);
        irq_release(mailbox_syscall_irq);
        mailbox_syscall_irq = 0;
    }
}

mail* mailbox_alloc_mail(void)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_ALLOC_MAIL;

    arch_syscall(MAILBOX_SYSCALL_MINOR, &config);

    return config.m;
}

void mailbox_release_mail(mail* m)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_RELEASE_MAIL;
    config.m = m;

    arch_syscall(MAILBOX_SYSCALL_MINOR, &config);
}

mailbox* mailbox_alloc(int owner_pid, int owner_tid)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_ALLOC;
    config.pid = owner_pid;
    config.tid = owner_tid;

    arch_syscall(MAILBOX_SYSCALL_MINOR, &config);

    return config.mb;
}

void mailbox_release(mailbox* mb)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_RELEASE;
    config.mb = mb;

    arch_syscall(MAILBOX_SYSCALL_MINOR, &config);
}


int mailbox_send(mail* m)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_SEND;
    config.m = m;

    arch_syscall(MAILBOX_SYSCALL_MINOR, &config);

    return config.ret;
}

mail* mailbox_listen(mailbox* mb)
{
    for ( ;; ) {
        mailbox_ctrl_config config = {0};
        config.cmd = MAILBOX_CTRL_LISTEN;
        config.mb = mb;

        arch_syscall(MAILBOX_SYSCALL_MINOR, &config);

        if (config.m)
            return config.m;

        /* No mail yet: yield HERE in user mode (a fresh, non-nested
         * syscall), so the scheduler and the interrupt handlers that will
         * queue the mail (e.g. the keyboard ISR) actually get to run. */
        thread_yield();
    }
}

int mailbox_register_handler(mailbox* mb, mail_handler handler)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_REGISTER_HANDLER;
    config.mb = mb;
    config.handler = handler;

    arch_syscall(MAILBOX_SYSCALL_MINOR, &config);

    return config.ret;
}

int mailbox_unregister_handler(mailbox* mb, mail_handler handler)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_UNREGISTER_HANDLER;
    config.mb = mb;
    config.handler = handler;

    arch_syscall(MAILBOX_SYSCALL_MINOR, &config);

    return config.ret;
}
