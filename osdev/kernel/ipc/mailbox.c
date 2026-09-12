#include "ipc/mailbox.h"
#include "mm/heap.h"
#include "lib/string.h"
#include "kernel/errno.h"
#include "arch_irq.h"
#include "kernel/irq.h"
#include "kernel/syscall.h"
#include "kernel/uapi.h"
#include "kernel/process.h"
#include "kernel/capability.h"

/*
 * Registry of every live mail's bookkeeping (mailmeta), keyed by payload.
 * Lets the kernel map a user-visible mail* back to its kernel-side meta
 * WITHOUT storing a meta pointer inside the ring-3-writable mail object.
 *
 * INVARIANT: every access (alloc_mail's list_add, release_mail's list_del,
 * get_mailmeta's walk) must run with interrupts DISABLED.  All current
 * callers do so (syscall gate / schedule_lock / ISR context).  Keep it that
 * way — do not call these from an interrupt-enabled ring-0 thread.
 */
static LIST_HEAD(inflight_mail_header);

static mailmeta* get_mailmeta(mail* m)
{
    if (!m)
        return 0;

    list_for_each(pos, &inflight_mail_header) {
        mailmeta* meta = list_entry(pos, mailmeta, query_node);
        if (meta->payload == m)
            return meta;
    }

    return 0;
}

mail* alloc_mail(void)
{
    static size_t unique_id = 0;

    /* The user-visible part lives in the shared USER heap (SYSCALL_HEAP /
     * kernel/mm/heap.c malloc) so ring-3 can dereference it directly; the
     * bookkeeping meta stays in the KERNEL heap (kmalloc) so ring-3 never
     * touches refcounts / locks / list nodes. */
    mail* m = (mail*)malloc(sizeof(mail));
    if (!m)
        return 0;

    mailmeta* meta = (mailmeta*)kmalloc(sizeof(mailmeta));
    if (!meta) {
        free(m);
        return 0;
    }

    memset(m, 0, sizeof(mail));
    meta->payload = m;         /* queue<->mail link: try_get_mail() returns this */
    meta->ref_count = 1;       /* caller initially holds one reference */
    meta->sp_lock = spinlock_alloc();
    if (!meta->sp_lock) {
        kfree(meta);
        free(m);
        return 0;
    }

    meta->unique_id = __sync_fetch_and_add(&unique_id, 1);
    list_init(&meta->this_node);
    list_init(&meta->query_node);

    list_add(&meta->query_node, &inflight_mail_header);

    return m;
}

static void release_mail(mail* m)
{
    if (!m)
        return;

    mailmeta* meta = get_mailmeta(m);
    if (!meta)
        return;

    u32 eflags = spinlock_lock_irqsave(meta->sp_lock);
    meta->ref_count--;
    if (meta->ref_count > 0) {
        spinlock_unlock_irqrestore(meta->sp_lock, eflags);
        return;
    }
    spinlock_unlock_irqrestore(meta->sp_lock, eflags);
    spinlock_release(meta->sp_lock);
    list_del(&meta->query_node);
    memset(meta, 0, sizeof(mailmeta));
    kfree(meta);    /* bookkeeping:      kernel heap */

    memset(m, 0, sizeof(mail));
    free(m);        /* user-visible part: user pool  */
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
    memset(mb->subscriptions, 0, sizeof(mb->subscriptions));
    list_init(&mb->mails);
    list_init(&mb->handlers);

    /* waiters SHARES mb->sp_lock (NOT wait_queue_init — that would allocate
     * a second lock) so LISTEN_BLOCK's empty-check + enqueue and a sender's
     * queue + wake are atomic under one lock: no lost wakeup. */
    list_init(&mb->waiters.waiters);
    mb->waiters.sp_lock = mb->sp_lock;

    return mb;
}

void release_mailbox(mailbox* mb)
{
    if (mb) {
        u32 eflags = spinlock_lock_irqsave(mb->sp_lock);

        /* Release all pending mails, safely removing each from the list first */
        while (!list_empty(&mb->mails)) {
            mailmeta* meta = list_entry(mb->mails.prev, mailmeta, this_node);
            list_del(&meta->this_node);
            release_mail(meta->payload);
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
        return E_INVAL;

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
        mailmeta* meta = get_mailmeta(m);
        if (!meta) {
            /* Untracked / forged mail: it cannot be queued.  Drop the
             * reference (a no-op for an untracked mail) and report failure
             * instead of silently losing the mail. */
            spinlock_unlock_irqrestore(mb->sp_lock, eflags);
            release_mail(m);
            return E_INVAL;
        }
        list_add(&meta->this_node, &mb->mails);
        spinlock_unlock_irqrestore(mb->sp_lock, eflags);

        /* A thread may be parked in MAILBOX_CTRL_LISTEN_BLOCK on this
         * mailbox — wake it so it dequeues promptly instead of on the next
         * scheduler tick.  wake_one takes mb->sp_lock again (waiters shares
         * it), so it MUST run after the unlock above.  Safe from ISR
         * context: thread_unblock only marks the thread runnable; the
         * actual switch happens at a later gate exit. */
        wait_queue_wake_one(&mb->waiters);
    }

    return 0;
}

int send(mail* m)
{
    if (!m)
        return E_INVAL;

    if (m->receiver_tid == MAIL_ANY_TID) {
        /*
         * Broadcast: deliver only to mailboxes subscribed to m->magic.
         * ref_count tracks how many deliveries are outstanding:
         *   - one per subscribed handler-having mailbox (consumed by send_mail)
         *   - one for the sender (consumed by release_mail at end)
         * Handlers run synchronously and must NOT call release_mail().
         * For subscribed threads without handlers, a clone is queued instead.
         */
        int handler_recipients = 0;
        int has_any_mailbox = 0;

        u32 eflags = spinlock_lock_irqsave(schedule_lock);

        /*
         * First pass: count only mailboxes that are subscribed to this
         * mail's magic AND have a handler.  Unsubscribed mailboxes are
         * skipped in the second pass; handler-less mailboxes receive
         * clones, which are independent and do not affect the original
         * mail's ref_count.
         */
        list_for_each(node, &thread_head) {
            tcb* t = list_entry(node, tcb, this_node);
            if (!t || !t->mailbox)
                continue;

            has_any_mailbox = 1;

            u32 eflags = spinlock_lock_irqsave(t->mailbox->sp_lock);
            /* magic 0 is never a valid subscription (SUBSCRIBE rejects it),
             * so it must not match an empty subscription slot (0 == 0). */
            if (m->magic != 0 && !list_empty(&t->mailbox->handlers)) {
                for (int i = 0; i < MAX_SUBSCRIPTION_COUNT; i++) {
                    if (t->mailbox->subscriptions[i] == m->magic) {
                        handler_recipients++;
                        break;
                    }
                }
            }
            spinlock_unlock_irqrestore(t->mailbox->sp_lock, eflags);
        }

        if (!has_any_mailbox) {
            spinlock_unlock_irqrestore(schedule_lock, eflags);
            release_mail(m);
            return 0;
        }

        mailmeta* meta = get_mailmeta(m);
        if (!meta) {
            spinlock_unlock_irqrestore(schedule_lock, eflags);
            return E_INVAL;
        }
        /*
         * ref_count accounts for:
         *   - one reference per subscribed handler-having mailbox
         *     (consumed by send_mail's internal release_mail after
         *     handlers return)
         *   - one extra reference for the sender (consumed by release_mail below)
         * Accumulate rather than overwrite so any pre-existing outstanding
         * reference (e.g. this mail already queued elsewhere) is preserved;
         * the sender's own reference was set to 1 by alloc_mail().
         */
        meta->ref_count += handler_recipients;

        /* Second pass: deliver to each recipient */
        list_for_each(node, &thread_head) {
            tcb* t = list_entry(node, tcb, this_node);
            if (!t || !t->mailbox)
                continue;

            int has_handler = 0;
            int has_subscribed = 0;
            u32 eflags = spinlock_lock_irqsave(t->mailbox->sp_lock);
            has_handler = !list_empty(&t->mailbox->handlers);
            /* magic 0 is never a valid subscription, so it must not match
             * an empty subscription slot (0 == 0). */
            if (m->magic != 0) {
                for (int i = 0; i < MAX_SUBSCRIPTION_COUNT; i++) {
                    if (t->mailbox->subscriptions[i] == m->magic) {
                        has_subscribed = 1;
                        break;
                    }
                }
            }
            spinlock_unlock_irqrestore(t->mailbox->sp_lock, eflags);

            if (!has_subscribed)
                continue;

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
                    /* Copy only the user-visible payload.  alloc_mail()
                     * already gave the clone its OWN meta (ref_count=1,
                     * payload=clone, fresh spinlock + list node) — do NOT
                     * copy m's bookkeeping over it (that would share m's
                     * spinlock and clobber the clone's ref_count/list state). */
                    clone->magic        = m->magic;
                    clone->sender_tid   = m->sender_tid;
                    clone->receiver_tid = m->receiver_tid;
                    clone->data_size    = m->data_size;
                    memcpy(clone->data, m->data, sizeof(m->data));

                    /* alloc_mail() registered the clone's meta in the inflight
                     * list (ref_count already 1), so the lookup below always
                     * succeeds — no extra refcount bookkeeping is needed. */
                    u32 eflags = spinlock_lock_irqsave(t->mailbox->sp_lock);
                    mailmeta* clone_meta = get_mailmeta(clone);
                    if (clone_meta)
                        list_add(&clone_meta->this_node, &t->mailbox->mails);
                    spinlock_unlock_irqrestore(t->mailbox->sp_lock, eflags);

                    /* Wake a parked LISTEN_BLOCK waiter (e.g. a game thread
                     * waiting for a key broadcast).  We hold schedule_lock
                     * while walking the thread list, so use the LOCKED wake
                     * variant — wait_queue_wake_one -> thread_unblock would
                     * re-take schedule_lock and deadlock.  No-op if nobody
                     * is parked on this mailbox. */
                    wait_queue_wake_one_locked(&t->mailbox->waiters);
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

    mailmeta* meta = list_entry(mb->mails.prev, mailmeta, this_node);
    list_del(&meta->this_node);
    spinlock_unlock_irqrestore(mb->sp_lock, eflags);

    return meta->payload;
}

static int register_handler(mailbox* mb, mail_handler handler)
{
    if (!mb || !handler)
        return E_INVAL;

    mailhandler* mh = (mailhandler*)kmalloc(sizeof(mailhandler));
    if (!mh)
        return E_NOMEM;

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
        return E_INVAL;

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
 * Shared mailbox logic — the single place that maps a MAILBOX_CTRL_*
 * command to the kernel implementation.  It runs in two ways:
 *   - directly, from the public wrappers when mb_run_direct() (ring 0
 *     inside a gate), and
 *   - inside the syscall gate via mailbox_syscall_isr() (ring-3 callers).
 *
 * There is deliberately NO capability check here: kernel/ISR callers run
 * against whatever process happens to be scheduled and must not be
 * subjected to the ring-3 CAP_IPC gate.  mailbox_syscall_isr() applies
 * that check on the trap path only.
 */
static int mailbox_exec(mailbox_ctrl_config* config)
{
    if (!config)
        return E_INVAL;

    switch (config->cmd) {
    case MAILBOX_CTRL_SEND:
        config->ret = send(config->m);
        break;
    case MAILBOX_CTRL_LISTEN:
        /*
         * Non-blocking on purpose: this runs inside the syscall ISR with
         * interrupts disabled, so a blocking loop that calls thread_yield()
         * would spin forever (a nested int $100 is swallowed by the irq
         * reentrancy guard in arch_syscall_entry).  The user-side listen
         * loop wraps LISTEN + yield in user mode instead.
         *
         * mb == NULL means "my own thread's mailbox": standalone user ELFs
         * cannot know their mailbox's kernel pointer, so the handler
         * (running in the caller's context) resolves it here.
         */
        if (!config->mb) {
            tcb* t = thread_get_by_tid(thread_get_tid());
            config->mb = t ? t->mailbox : 0;
        }
        config->m = try_get_mail(config->mb);
        config->ret = (config->m != 0) ? 0 : -1;
        break;
    case MAILBOX_CTRL_LISTEN_BLOCK:
        /*
         * Tail-blocking variant of LISTEN — the kernel's ONE supported
         * "block inside a syscall" shape.  This kernel defers context
         * switches to the gate exit (arch_task_restore_context only
         * re-points curr_task_ctx), so a thread parked here NEVER resumes
         * in the middle of this gate: it resumes in user mode right after
         * the syscall.  Therefore this call does NOT hand back the mail; it
         * only guarantees one is queued — the caller must follow up with a
         * non-blocking LISTEN to dequeue (portal's WAIT_REPLY -> GET_RESULT
         * two-phase pattern).  Spurious wakes (woken before the mail was
         * dequeuable) are absorbed by the user-side retry loop.
         *
         * The park is the LAST blocking action of this case — deliberately
         * NO re-check loop: after wait_queue_sleep_locked() returns only
         * gate teardown runs before the deferred switch fires.
         */
        if (!config->mb) {
            tcb* t = thread_get_by_tid(thread_get_tid());
            config->mb = t ? t->mailbox : 0;
        }
        if (!config->mb) {
            config->ret = E_INVAL;
            break;
        }

        /* Set the result BEFORE the block and cache everything we still
         * need into locals.  The tail-block defers the real context switch
         * to the gate exit, but thread_block() already switched address
         * space (CR3) to the next thread — which may be in ANOTHER process.
         * From that point on the syscall config buffer (a kmalloc'd kbuf,
         * not readable under another process's CR3) is OFF LIMITS: do NOT
         * re-read config->mb / config->ret after wait_queue_sleep_locked().
         * Only the cached mailbox (< 16MB identity, shared) and the static
         * spinlock array are safe to touch afterwards. */
        config->ret = 0;
        {
            mailbox* mb = config->mb;
            spinlock* sp = mb->sp_lock;
            u32 eflags = spinlock_lock_irqsave(sp);
            if (list_empty(&mb->mails)) {
                /* No mail: park.  sleep_locked expects the caller to already
                 * hold wq->sp_lock (== mb->sp_lock) with IF=0; it enqueues
                 * us, unlocks, thread_block()s (deferred switch), re-locks.
                 * After it returns we may be running under another process's
                 * CR3 — release the lock with the CACHED sp only, then exit
                 * the gate (the thread resumes in user mode after the
                 * syscall once woken; it never comes back into this case). */
                wait_queue_sleep_locked(&mb->waiters);
            }
            spinlock_unlock_irqrestore(sp, eflags);
        }
        break;
    case MAILBOX_CTRL_REGISTER_HANDLER:
        config->ret = register_handler(config->mb, config->handler);
        break;
    case MAILBOX_CTRL_UNREGISTER_HANDLER:
        config->ret = unregister_handler(config->mb, config->handler);
        break;
    case MAILBOX_CTRL_ALLOC_MAIL:
        config->m = alloc_mail();
        config->ret = (config->m != 0) ? 0 : E_NOMEM;
        break;
    case MAILBOX_CTRL_RELEASE_MAIL:
        release_mail(config->m);
        config->ret = 0;
        break;
    case MAILBOX_CTRL_ALLOC:
        config->mb = alloc_mailbox(config->pid, config->tid);
        config->ret = (config->mb != 0) ? 0 : E_NOMEM;
        break;
    case MAILBOX_CTRL_RELEASE:
        release_mailbox(config->mb);
        config->ret = 0;
        break;
    case MAILBOX_CTRL_SUBSCRIBE_MAIL:
        /* mb == NULL means the calling thread's own mailbox (same rule as
         * LISTEN): ring-3 programs cannot know their mailbox's kernel
         * pointer, so the handler resolves it here. */
        if (!config->mb) {
            tcb* t = thread_get_by_tid(thread_get_tid());
            config->mb = t ? t->mailbox : 0;
        }
        if (!config->mb || config->magic == 0) {
            config->ret = E_INVAL;
            break;
        }
        u32 eflags = spinlock_lock_irqsave(config->mb->sp_lock);
        for (int i = 0; i < MAX_SUBSCRIPTION_COUNT; i++) {
            if (config->mb->subscriptions[i] == config->magic) {
                spinlock_unlock_irqrestore(config->mb->sp_lock, eflags);
                config->ret = 0;   /* already subscribed */
                return config->ret;
            }
        }

        for (int i = 0; i < MAX_SUBSCRIPTION_COUNT; i++) {
            if (config->mb->subscriptions[i] == 0) {
                config->mb->subscriptions[i] = config->magic;
                config->ret = 0;
                spinlock_unlock_irqrestore(config->mb->sp_lock, eflags);
                return config->ret;
            }
        }
        spinlock_unlock_irqrestore(config->mb->sp_lock, eflags);
        config->ret = E_NOMEM;
        break;
    case MAILBOX_CTRL_UNSUBSCRIBE_MAIL:
        /* mb == NULL: own mailbox, same as SUBSCRIBE / LISTEN. */
        if (!config->mb) {
            tcb* t = thread_get_by_tid(thread_get_tid());
            config->mb = t ? t->mailbox : 0;
        }
        if (!config->mb || config->magic == 0) {
            config->ret = E_INVAL;
            break;
        }
        u32 eflags2 = spinlock_lock_irqsave(config->mb->sp_lock);
        for (int i = 0; i < MAX_SUBSCRIPTION_COUNT; i++) {
            if (config->mb->subscriptions[i] == config->magic) {
                config->mb->subscriptions[i] = 0;
                config->ret = 0;
                spinlock_unlock_irqrestore(config->mb->sp_lock, eflags2);
                return config->ret;
            }
        }
        spinlock_unlock_irqrestore(config->mb->sp_lock, eflags2);
        config->ret = E_INVAL;
        break;
    default:
        config->ret = E_INVAL;
        break;
    }

    return config->ret;
}

/*
 * Mailbox syscall gate (SYSCALL_MAILBOX).  Ring-3 entry only: applies the
 * CAP_IPC check, then defers to the shared mailbox_exec().
 */
static int mailbox_syscall_isr(void* data)
{
    mailbox_ctrl_config* config = (mailbox_ctrl_config*)data;
    if (!config)
        return E_INVAL;

    /*
     * CAP_IPC gate: a user (CPL3) process may only use the mailbox IPC
     * service if it holds a CAP_IPC grant.  Kernel processes / drivers are
     * trusted and skip the check.  The handler runs in the caller's
     * context, so get_current_process() is the process behind the syscall.
     */
    pcb* proc = get_current_process();
    if (proc && proc->priv != PROC_PRIV_KERNEL) {
        /*
         * Ring-3 has NO legitimate mailbox handle: the only mailbox a user
         * thread may touch is its own, addressed by mb == NULL (mailbox_exec
         * resolves it to the current thread).  A non-NULL mb from CPL3 is a
         * forged raw kernel pointer (mailbox* lives in the kernel heap) —
         * reject it so the kernel never dereferences caller-chosen
         * addresses through LISTEN/SUBSCRIBE/...  Kernel/driver callers are
         * trusted and keep passing real mailbox* handles.
         */
        if (config->mb) {
            config->ret = E_INVAL;
            return config->ret;
        }
        int ipc_ok = 1;
        if (cap_check(proc, CAP_IPC, &ipc_ok) != 0) {
            config->ret = E_PERM;
            return config->ret;
        }
    }

    return mailbox_exec(config);
}

static i32 mailbox_scall_handle = -1;

void mailbox_syscall_init(void)
{
    mailbox_scall_handle = syscall_register(SYSCALL_MAILBOX,
        mailbox_syscall_isr, sizeof(mailbox_ctrl_config));
}

void mailbox_syscall_exit(void)
{
    syscall_unregister(mailbox_scall_handle);
}

/*
 * Inside a syscall/IRQ gate (ring-0, irq_reenter_cnt == 0) issuing
 * int $100 again would hit the reentrancy guard in arch_syscall() and
 * silently return E_AGAIN with a stale (zeroed) config — the caller
 * would see a bogus "success".  Call the kernel implementation
 * directly instead; at CPL3 or idle ring-0 the gate works normally.
 */
static inline int mb_run_direct(void)
{
    return !arch_running_ring3() && arch_in_gate();
}

mail* mailbox_alloc_mail(void)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_ALLOC_MAIL;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));

    return config.m;
}

void mailbox_release_mail(mail* m)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_RELEASE_MAIL;
    config.m = m;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));
}

mailbox* mailbox_alloc(int owner_pid, int owner_tid)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_ALLOC;
    config.pid = owner_pid;
    config.tid = owner_tid;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));

    return config.mb;
}

void mailbox_release(mailbox* mb)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_RELEASE;
    config.mb = mb;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));
}


int mailbox_send(mail* m)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_SEND;
    config.m = m;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));

    return config.ret;
}

mail* mailbox_listen(mailbox* mb)
{
    for ( ;; ) {
        mailbox_ctrl_config config = {0};
        config.cmd = MAILBOX_CTRL_LISTEN;
        config.mb = mb;

        /* Non-blocking LISTEN (try_get_mail): run the shared exec directly
         * inside a gate, otherwise through the syscall. */
        if (mb_run_direct())
            mailbox_exec(&config);
        else
            arch_syscall(mailbox_scall_handle, &config, sizeof(config));

        if (config.m)
            return config.m;

        /* No mail yet: yield so the scheduler and the interrupt handlers
         * that will queue the mail (e.g. the keyboard ISR) actually get to
         * run — inside the gate this is a direct context switch, in user
         * mode a fresh, non-nested syscall. */
        thread_yield();
    }
}

int mailbox_register_handler(mailbox* mb, mail_handler handler)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_REGISTER_HANDLER;
    config.mb = mb;
    config.handler = handler;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));

    return config.ret;
}

int mailbox_unregister_handler(mailbox* mb, mail_handler handler)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_UNREGISTER_HANDLER;
    config.mb = mb;
    config.handler = handler;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));

    return config.ret;
}

int mailbox_subscribe_mail(mailbox* mb, u32 magic)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_SUBSCRIBE_MAIL;
    config.mb = mb;
    config.magic = magic;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));

    return config.ret;
}

int mailbox_unsubscribe_mail(mailbox* mb, u32 magic)
{
    mailbox_ctrl_config config = {0};
    config.cmd = MAILBOX_CTRL_UNSUBSCRIBE_MAIL;
    config.mb = mb;
    config.magic = magic;

    if (mb_run_direct())
        mailbox_exec(&config);
    else
        arch_syscall(mailbox_scall_handle, &config, sizeof(config));

    return config.ret;
}

