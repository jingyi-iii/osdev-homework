#include "kernel/syscall.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "lib/module.h"
#include "sync/spinlock.h"
#include "arch_irq.h"
#include "arch_mem.h"
#include "kernel/process.h"   /* pcb / get_current_process (user syscall registry) */
#include "kernel/uapi.h"      /* SYSCALL_SYSCTL + user_sysctl_config */

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

    /*
     * Decide whose page tables the handler runs under (owner_cr3):
     *   - kernel callers (module init, gate handlers for kernel services)
     *     register handlers that run in the CALLER's context: owner_cr3 = 0;
     *   - a ring-3 process registering a user syscall does so through the
     *     SYSCALL_SYSCTL gate: sysctl_syscall_isr() runs at ring 0 but on
     *     behalf of the calling USER process, so the handler must be
     *     invoked under THAT process's page directory (its code and
     *     globals live in its own address space).
     * arch_running_ring3() cannot be used here — this function is always
     * reached from ring 0 (module init or a gate handler).  Detect the
     * user origin from the current process's privilege instead, exactly
     * like irq.c / portal.c do inside their exec handlers.
     */
    pcb* proc = get_current_process();
    sc->owner_cr3 = (proc && proc->priv != PROC_PRIV_KERNEL) ? proc->vcb.cr3 : 0;

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
 * Invoke a syscall handler, switching to the handler's page directory if it
 * is a user-registered handler (owner_cr3 != 0).  The handler lives in the
 * registering process's address space (e.g. the log server ELF at
 * 0xA0000000), which is not mapped in the caller's directory; switching CR3
 * makes its code and globals reachable while it runs at ring 0.  The kernel
 * itself (and the kernel heap) sit in the low identity map that every
 * directory shares, so the switch is safe from inside the gate.
 */
static int call_user_handler(syscall_handler_fn fn, void* arg, u32 owner_cr3)
{
    u32 cur_cr3 = arch_get_cr3();
    int switched = (owner_cr3 && owner_cr3 != cur_cr3);

    if (switched)
        arch_load_cr3(owner_cr3);
    int ret = fn(arg);
    if (switched)
        arch_load_cr3(cur_cr3);

    return ret;
}

int syscall_dispatch(u32 handle, void* arg, size_t size)
{
    int ret = E_NOTFOUND;
    syscall_handler_fn fn = 0;
    size_t max_param_size = 0;
    u32 owner_cr3 = 0;

    spinlock_lock(&syscall_lock);
    list_for_each(node, &syscall_header) {
        syscall* sc = list_entry(node, syscall, this_node);
        if ((u32)sc->handle != handle)
            continue;

        ret = 0;
        fn = sc->fn;
        max_param_size = sc->max_param_size;
        owner_cr3 = sc->owner_cr3;
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

        /* copy_from_user ran with the CALLER's page tables; the handler
         * runs with the owner's (if user-registered), so the config the
         * handler sees is the kernel copy, never caller memory. */
        ret = call_user_handler(fn, kbuf, owner_cr3);
        if (copy_to_user(arg, kbuf, n) != 0 && ret == 0)
            ret = E_FAULT;
        kfree(kbuf);
    } else {
        ret = call_user_handler(fn, arg, owner_cr3);
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

/*
 * SYSCALL_SYSCTL — user-mode syscall registry gate.
 *
 * Only ring-3 callers may claim a number, and the handler must live in
 * user space (below USER_SPACE_TOP).  The kernel stores the handler with
 * the caller's page directory (proc->vcb.cr3); syscall_dispatch() then
 * switches to that directory before invoking it.
 */
static int sysctl_syscall_isr(void* data)
{
    user_sysctl_config* cfg = (user_sysctl_config*)data;
    if (!cfg)
        return E_INVAL;

    switch (cfg->cmd) {
    case U_SYSCTL_REGISTER: {
        pcb* proc = get_current_process();
        if (!proc || proc->priv != PROC_PRIV_USER) {
            cfg->ret = E_PERM;
            return E_PERM;
        }
        if (!cfg->fn || (uptr)cfg->fn >= USER_SPACE_TOP) {
            cfg->ret = E_INVAL;
            return E_INVAL;
        }
        /* syscall_register() derives owner_cr3 from the current (user)
         * process itself — no need to pass it here. */
        cfg->ret = syscall_register(cfg->num, cfg->fn, cfg->max_param_size);
        break;
    }
    case U_SYSCTL_UNREGISTER:
        cfg->ret = syscall_unregister(cfg->num);
        break;
    default:
        cfg->ret = E_INVAL;
        break;
    }

    return cfg->ret;
}

void syscall_init(void)
{
    /* syscall_lock is statically initialized so that syscall_register()
     * works even when this initcall runs after other subsystems'
     * initcalls (the linker orders .initcall by object file name). */

    /* User syscall registry: lets ring-3 servers (log server ELF, ...)
     * claim fixed syscall numbers with handlers in their own address
     * space (see syscall_register / call_user_handler). */
    syscall_register(SYSCALL_SYSCTL, sysctl_syscall_isr,
                     sizeof(user_sysctl_config));
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

module_init(syscall_init);
module_exit(syscall_exit);
