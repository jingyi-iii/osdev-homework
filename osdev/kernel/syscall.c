#include "kernel/syscall.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "lib/module.h"
#include "mm/paging.h"
#include "sync/spinlock.h"
#include "arch_irq.h"

/*
 * Dedicated syscall dispatch.  int $100 (arch_syscall_entry in irq.S) no
 * longer goes through the hardware-IRQ machinery (irqlines/irq_request);
 * it calls syscall_dispatch() directly, which looks the handle up in this
 * static table.  Only kernel subsystems register handlers (at init time),
 * so a user process can never attach a handler to the gate.  Handles are
 * allocated by syscall_register() — subsystems never choose a number.
 */
// static syscall_handler_fn syscall_table[SYSCALL_MAX_HANDLES] = {0};

static LIST_HEAD(syscall_header);

i32 syscall_register(syscall_handler_fn fn, size_t max_param_size)
{
    static i32 handle = 0;

    if (!fn)
        return E_INVAL;

    syscall* sc = kmalloc(sizeof(syscall));
    if (!sc)
        return E_NOMEM;
        
    sc->fn = fn;
    sc->max_param_size = max_param_size;
    sc->handle = handle++;
    list_init(&sc->this_node);
    list_add(&sc->this_node, &syscall_header);

    return sc->handle;
}

int syscall_unregister(i32 handle)
{
    if (handle < 0)
        return E_INVAL;

    list_for_each_safe(node, next, &syscall_header) {
        syscall* sc = list_entry(node, syscall, this_node);
        if (!sc || sc->handle != handle)
            continue;
        
        list_del(&sc->this_node);
        kfree(sc);
    }

    return 0;
}

int syscall_dispatch(u32 handle, void* arg, size_t size)
{
    int ret = 0;

    list_for_each(node, &syscall_header) {
        syscall* sc = list_entry(node, syscall, this_node);
        if (!sc || sc->handle != handle || sc->max_param_size < size)
            continue;

        /*
         * Ring-3 callers: copy the config into a validated kernel buffer,
         * run the handler on it, then copy it BACK so the OUT parameters
         * (ret / pid / tid / value / m / mb / va ...) reach the caller.
         * Ring-0 callers (kernel servers, drivers, the boot path) pass
         * their pointer straight through — they are trusted kernel code
         * and their configs live on the kernel stack, which the user-space
         * copy helpers would reject.
         */
        if (arch_running_ring3()) {
            void* kbuf = kmalloc(size);
            if (!kbuf)
                continue;

            if (copy_from_user(kbuf, arg, size) != 0) {
                kfree(kbuf);
                continue;
            }

            ret = sc->fn(kbuf);
            copy_to_user(arg, kbuf, size);   /* write back OUT params */
            kfree(kbuf);
            break;
        } else {
            ret = sc->fn(arg);
            break;
        }
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
        if (!(pde & PTE_PRESENT) || (for_write && !(pde & PTE_RW)))
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
    if (!dst || !user_src)
        return E_FAULT;
    if (!user_range_ok(user_src, n, 0))
        return E_FAULT;
    memcpy(dst, user_src, n);
    return 0;
}

int copy_to_user(void* user_dst, const void* src, size_t n)
{
    if (!user_dst || !src)
        return E_FAULT;
    if (!user_range_ok(user_dst, n, 1))
        return E_FAULT;
    memcpy(user_dst, src, n);
    return 0;
}

void syscall_init(void)
{

}

void syscall_exit(void)
{

}

module_init(syscall_init);
module_exit(syscall_exit);
