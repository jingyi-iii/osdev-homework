#include "kernel/syscall.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "lib/module.h"
#include "sync/spinlock.h"
#include "arch_irq.h"
#include "arch_mem.h"

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
    if (arch_running_ring3()) {
        size_t n = (size < max_param_size) ? size : max_param_size;
        size_t buf_size = max_param_size ? max_param_size : 1;
        void* kbuf = kmalloc(buf_size);
        if (!kbuf)
            return E_NOMEM;
        memset(kbuf, 0, buf_size);

        if (copy_from_user(kbuf, arg, n) != 0) {
            kfree(kbuf);
            return E_FAULT;
        }

        /* The handler runs on the kernel copy of the caller's config, so
         * it never dereferences caller memory directly. */
        ret = fn(kbuf);
        if (copy_to_user(arg, kbuf, n) != 0 && ret == 0)
            ret = E_FAULT;
        kfree(kbuf);
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
