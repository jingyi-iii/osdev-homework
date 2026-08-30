#include "kernel/syscall.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "lib/module.h"
#include "mm/paging.h"
#include "sync/spinlock.h"
#include "arch_irq.h"

static LIST_HEAD(syscall_header);
static spinlock syscall_lock = { .state = LOCK_UNLOCKED };

/*
 * Serializes user_range_ok() + the following memcpy() in the copy_*_user
 * helpers.  Held IRQ-safe, so no interrupt handler can preempt the holder
 * and change (or free) the page tables being validated on this single-CPU
 * kernel — closes the check-then-copy TOCTOU window.
 */
static spinlock copy_lock = { .state = LOCK_UNLOCKED };

i32 syscall_register(i32 num, syscall_handler_fn fn, size_t max_param_size)
{
    if (!fn || num < 0)
        return E_INVAL;

    syscall* sc = kmalloc(sizeof(syscall));
    if (!sc)
        return E_NOMEM;

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

        ret = fn(kbuf);
        if (copy_to_user(arg, kbuf, n) != 0 && ret == 0)
            ret = E_FAULT;
        kfree(kbuf);
    } else {
        ret = fn(arg);
    }

    return ret;
}

/*
 * Validate that [addr, addr+n) lies in user space and is mapped with the
 * required permissions in the CURRENT page table (the syscall runs in the
 * caller's address space, so CR3 is the caller's directory).  For writes
 * the pages must be writable as well.
 *
 * Note: the kernel is identity-mapped with the first 16 MB user-accessible
 * (see arch_clone_kernel_pde), so this cannot distinguish a user pointer
 * from one into that low kernel region — true isolation needs a proper
 * kernel/user remap, out of scope here.
 */
static int user_range_ok(const void* user_addr, size_t n, int for_write)
{
    u32 addr = (u32)user_addr;
    u32 cr3;

    if (n == 0)
        return 1;
    if (addr + n < addr)              /* 32-bit overflow */
        return 0;
    if (addr >= USER_SPACE_TOP || addr + n > USER_SPACE_TOP)
        return 0;                     /* outside user space */

    cr3 = arch_get_cr3() & PAGE_MASK;

    while (n > 0) {
        u32 pde = *(volatile u32*)(cr3 + PD_INDEX(addr) * 4);
        if (!(pde & PTE_PRESENT) || !(pde & PTE_USER) ||
            (for_write && !(pde & PTE_RW)))
            return 0;

        if (pde & 0x80) {
            /* 4MB page (PS bit): the whole region shares the PDE's
             * permissions, there is no page table to walk. */
            if (!(pde & PTE_USER) || (for_write && !(pde & PTE_RW)))
                return 0;
        } else {
            u32 pt_base = pde & PAGE_MASK;
            u32 pte = *(volatile u32*)(pt_base + PT_INDEX(addr) * 4);
            if (!(pte & PTE_PRESENT) || !(pte & PTE_USER) ||
                (for_write && !(pte & PTE_RW)))
                return 0;
        }

        size_t remain = PAGE_SIZE - PAGE_OFFSET(addr);
        if (remain > n)
            remain = n;
        addr += remain;
        n -= remain;
    }
    return 1;
}

int copy_from_user(void* dst, const void* user_src, size_t n)
{
    u32 eflags;
    int ok;

    if (!dst || !user_src)
        return E_FAULT;

    /* Validate and copy atomically against interrupt handlers. */
    eflags = spinlock_lock_irqsave(&copy_lock);
    ok = user_range_ok(user_src, n, 0);
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
    ok = user_range_ok(user_dst, n, 1);
    if (ok)
        memcpy(user_dst, src, n);
    spinlock_unlock_irqrestore(&copy_lock, eflags);

    return ok ? 0 : E_FAULT;
}

void syscall_init(void)
{
    /* syscall_lock is statically initialized so that syscall_register()
     * works even when this initcall runs after other subsystems'
     * initcalls (the linker orders .initcall by object file name). */
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
