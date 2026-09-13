#include "kernel/syscall.h"
#include "kernel/errno.h"
#include "kernel/log.h"
#include "kernel/uapi.h"
#include "kernel/process.h"  /* thread_get_tid */
#include "lib/string.h"
#include "lib/module.h"
#include "sync/spinlock.h"
#include "arch_irq.h"
#include "arch_mem.h"
#include "paging.h"          /* arch_get_cr3 */
#include "arch_task.h"       /* arch_task_context / regs */

/* Current thread's saved context (arch/i386/task.c). */
extern volatile arch_task_context* curr_task_ctx;

/*
 * True when the current syscall gate was entered FROM RING 3.  The live
 * arch_running_ring3() reads the current CS — inside the gate that is
 * always CPL0, so it cannot be used here.  The SAVED frame's CS is
 * authoritative: for a ring-3 caller the gate pushed the user CS onto
 * the per-thread frame; for a ring-0 caller (kernel thread, nested gate
 * call) it is the kernel CS.
 */
static int gate_caller_ring3(void)
{
    arch_task_context* ctx = (arch_task_context*)curr_task_ctx;

    if (!ctx || !ctx->regs)
        return 0;
    return (ctx->regs->cs & 3) == 3;
}

static LIST_HEAD(syscall_header);
static spinlock syscall_lock = { .state = LOCK_UNLOCKED };

/*
 * Serializes arch_validate_user_range() + the following memcpy() in the
 * copy_*_user helpers.  Held IRQ-safe, so no interrupt handler can preempt
 * the holder and change (or free) the page tables being validated on this
 * single-CPU kernel — closes the check-then-copy TOCTOU window.
 */
static spinlock copy_lock = { .state = LOCK_UNLOCKED };

i32 syscall_register(i32 num, syscall_handler_fn fn, size_t max_param_size)
{
    if (!fn || num < 0)
        return E_INVAL;

    syscall* sc = kmalloc(sizeof(syscall));
    if (!sc)
        return E_NOMEM;

    memset(sc, 0, sizeof(syscall));
    sc->handle = num;
    sc->fn = fn;
    sc->max_param_size = max_param_size;

    list_init(&sc->this_node);

    spinlock_lock(&syscall_lock);
    list_for_each(node, &syscall_header) {
        syscall* ex = list_entry(node, syscall, this_node);
        if (ex->handle == num) {
            spinlock_unlock(&syscall_lock);
            kfree(sc);
            return E_EXISTS;
        }
    }
    list_add(&sc->this_node, &syscall_header);
    spinlock_unlock(&syscall_lock);

    return num;
}

int syscall_unregister(i32 handle)
{
    if (handle < 0)
        return E_INVAL;

    spinlock_lock(&syscall_lock);
    list_for_each_safe(node, next, &syscall_header) {
        syscall* sc = list_entry(node, syscall, this_node);
        if (sc->handle != handle)
            continue;
        
        list_del(&sc->this_node);
        kfree(sc);
        spinlock_unlock(&syscall_lock);
        return 0;
    }
    spinlock_unlock(&syscall_lock);

    return E_NOTFOUND;
}

/*
 * Kernel copy of a ring-3 syscall config.  It lives in the low identity
 * map (kernel .bss, below 16MB), which is mapped under EVERY page
 * directory, so it is reachable from whichever CR3 is active when a
 * tail-blocking handler's C tail runs (MAILBOX_CTRL_LISTEN_BLOCK) —
 * with the lazy CR3 design the switch commits only at the gate exit,
 * but the .bss buffer is valid under any design/timing and never
 * involves heap memory.  Syscall dispatch is serialized (the gate
 * refuses re-entry), so one shared buffer is enough.
 */
#define SYSCALL_KBUF_MAX  512
static u8 syscall_kbuf[SYSCALL_KBUF_MAX];

int syscall_dispatch(u32 handle, void* arg, size_t size)
{
    int ret = E_NOTFOUND;
    syscall_handler_fn fn = 0;
    size_t max_param_size = 0;

    spinlock_lock(&syscall_lock);
    list_for_each(node, &syscall_header) {
        syscall* sc = list_entry(node, syscall, this_node);
        if ((u32)sc->handle != handle)
            continue;

        ret = 0;
        fn = sc->fn;
        max_param_size = sc->max_param_size;
        break;
    }
    spinlock_unlock(&syscall_lock);

    if (ret)
        return ret;

    /*
     * Ring-3 callers: copy the config into a validated, zeroed kernel
     * buffer, run the handler on it, then copy the part the handler knows
     * about back so the OUT parameters (ret / pid / tid / value / m / mb
     * / va ...) reach the caller.  Ring-0 callers (kernel servers,
     * drivers, the boot path) pass their pointer straight through — they
     * are trusted kernel code and their configs live on the kernel stack,
     * which the user-space copy helpers would reject.
     *
     * Only the first min(size, max_param_size) bytes are copied in/out:
     * a caller passing an older (smaller) struct still works, and a
     * caller passing a newer (larger) struct has its extra tail left
     * untouched (the handler only knows max_param_size bytes).  The
     * kernel buffer is sized max_param_size and zeroed, so the handler
     * never sees garbage beyond what the caller provided and no stale
     * heap bytes can leak back to user space.
     */
    if (gate_caller_ring3()) {
        size_t n = (size < max_param_size) ? size : max_param_size;
        size_t buf_size = max_param_size ? max_param_size : 1;
        int kmalloc_buf = (buf_size > SYSCALL_KBUF_MAX);
        void* kbuf = kmalloc_buf ? kmalloc(buf_size) : (void*)syscall_kbuf;

        if (!kbuf)
            return E_NOMEM;
        memset(kbuf, 0, buf_size);

        if (copy_from_user(kbuf, arg, n) != 0) {
            if (kmalloc_buf)
                kfree(kbuf);
            return E_FAULT;
        }

        /* The handler runs on the kernel copy of the caller's config, so
         * it never dereferences caller memory directly.
         *
         * If fn() switched the caller away — a tail-block (LISTEN_BLOCK,
         * portal WAIT / WAIT_REPLY) or a self-delete / self-exit that
         * frees the caller's stack or its whole address space — the
         * write-back is off limits.  Deliver nothing back and leave the
         * caller's config as it initialized it: the two-phase user
         * wrappers re-read OUT fields with follow-up non-blocking calls.
         *
         * thread_get_tid() flips as soon as the switch is *requested*,
         * which is what matters here: with the lazy CR3 switch the
         * address space (and curr_task_ctx) only change at the gate exit,
         * long after this tail ran.  The CR3 compare stays as
         * belt-and-braces for eager-style switches. */
        i32 tid_before = thread_get_tid();
        u32 cr3_before = arch_get_cr3();

        ret = fn(kbuf);

        if (thread_get_tid() == tid_before && arch_get_cr3() == cr3_before) {
            if (copy_to_user(arg, kbuf, n) != 0 && ret == 0)
                ret = E_FAULT;
            if (kmalloc_buf)
                kfree(kbuf);
        }
        /* else: switched away — nothing is delivered back and the kmalloc
         * fallback buffer is leaked (never happens for the registered
         * configs, all < SYSCALL_KBUF_MAX). */
    } else {
        ret = fn(arg);
    }

    return ret;
}

int copy_from_user(void* dst, const void* user_src, size_t n)
{
    u32 eflags;
    int ok;

    if (!dst || !user_src)
        return E_FAULT;

    /* Validate and copy atomically against interrupt handlers. */
    eflags = spinlock_lock_irqsave(&copy_lock);
    ok = arch_validate_user_range(user_src, n, 0);
    if (ok)
        memcpy(dst, user_src, n);
    spinlock_unlock_irqrestore(&copy_lock, eflags);

    return ok ? 0 : E_FAULT;
}

int copy_to_user(void* user_dst, const void* src, size_t n)
{
    u32 eflags;
    int ok;

    if (!user_dst || !src)
        return E_FAULT;

    /* Validate and copy atomically against interrupt handlers. */
    eflags = spinlock_lock_irqsave(&copy_lock);
    ok = arch_validate_user_range(user_dst, n, 1);
    if (ok)
        memcpy(user_dst, src, n);
    spinlock_unlock_irqrestore(&copy_lock, eflags);

    return ok ? 0 : E_FAULT;
}

void syscall_exit(void)
{
    spinlock_lock(&syscall_lock);
    list_for_each_safe(node, next, &syscall_header) {
        syscall* sc = list_entry(node, syscall, this_node);
        list_del(&sc->this_node);
        kfree(sc);
    }
    spinlock_unlock(&syscall_lock);
}

module_exit(syscall_exit);
