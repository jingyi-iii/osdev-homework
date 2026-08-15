#include "kernel/capability.h"
#include "kernel/process.h"
#include "kernel/errno.h"
#include "mm/heap.h"

/* The macros below run with proc->cap_lock held; failures set `ret` and
 * jump to the `unlock:` label so the lock is always released exactly once. */
#define CASE_CAP_CHECK(id, member, check_func) \
    case id: \
        if (args && check_func(&cap->member, (const typeof(cap->member)*)args) == 0) \
            ret = 0; \
        else \
            ret = E_PERM; \
        break;

#define CASE_CAP_GRANT(id, member) \
    case id: \
        cap->type = id; \
        cap->member = *(const typeof(cap->member)*)args; \
        list_add(&cap->this_node, &proc->capabilities); \
        break;

#define CASE_CAP_REVOKE(id, member, cmp_func) \
    case id: \
        if (cmp_func(&cap->member, args)) { \
            list_del(node); \
            kfree(cap); \
        } \
        break;

#define CASE_CAP_GRANT_CHECK(id, check_func) \
    case id: \
        if (check_func((const void*)args) != 0) { \
            ret = E_INVAL; \
            goto unlock; \
        } \
        break;

#define CASE_CAP_IS_DUP(id, member, cmp_func) \
    case id: \
        list_for_each(node, &proc->capabilities) { \
            capability* existing_cap = list_entry(node, capability, this_node); \
            if (existing_cap->type == id && cmp_func(&existing_cap->member, args)) { \
                ret = E_EXISTS; \
                goto unlock; \
            } \
        } \
        break;

static inline int cap_irq_check(const u32* defender, const u32* challenger)
{
    if (!defender || !challenger)
        return E_INVAL;
    return (*defender == *challenger) ? 0 : E_PERM;
}

static inline int cap_mem_check(const cap_mem* defender, const cap_mem* challenger)
{
    if (!defender || !challenger)
        return E_INVAL;

    /* Reject empty requests */
    if (challenger->size == 0)
        return E_INVAL;

    /* The challenger's requested flags must be a subset of the defender's */
    if ((defender->flags & challenger->flags) != challenger->flags)
        return E_PERM;

    /* The challenger region must be fully contained in the defender region.
     * Use subtraction instead of base + size to avoid uint32 overflow. */
    if (challenger->base < defender->base)
        return E_PERM;
    if (challenger->size > defender->size)
        return E_PERM;
    if (challenger->base - defender->base > defender->size - challenger->size)
        return E_PERM;

    return 0;
}

static inline int cap_io_port_check(const cap_io_port* defender, const cap_io_port* challenger)
{
    if (!defender || !challenger)
        return E_INVAL;

    /* Reject empty requests */
    if (challenger->count == 0)
        return E_INVAL;

    /* The challenger region must be fully contained in the defender region.
     * Use subtraction instead of base + count to avoid uint32 overflow. */
    if (challenger->base < defender->base)
        return E_PERM;
    if (challenger->count > defender->count)
        return E_PERM;
    if (challenger->base - defender->base > defender->count - challenger->count)
        return E_PERM;

    return 0;
}

static inline int cap_permission_check(int* defender, const int* unused)
{
    (void)unused;
    return *defender ? 0 : E_PERM;
}

static inline int cap_irq_cmp(const void* a, const void* b)
{
    return *(const u32*)a == *(const u32*)b;
}

static inline int cap_mem_cmp(const void* a, const void* b)
{
    return (*(const cap_mem*)a).base == (*(const cap_mem*)b).base &&
           (*(const cap_mem*)a).size == (*(const cap_mem*)b).size &&
           (*(const cap_mem*)a).flags == (*(const cap_mem*)b).flags;
}

static inline int cap_io_port_cmp(const void* a, const void* b)
{
    return (*(const cap_io_port*)a).base == (*(const cap_io_port*)b).base &&
           (*(const cap_io_port*)a).count == (*(const cap_io_port*)b).count;
}

static inline int cap_permission_cmp(const void* a, const void* b)
{
    return *(const int*)a == *(const int*)b;
}

static inline int cap_irq_grant_check(const u32* perm)
{
    if (!perm)
        return E_INVAL;

    if (*perm > 255)
        return E_INVAL;

    return 0;
}

static inline int cap_mem_grant_check(const cap_mem* perm)
{
    if (!perm)
        return E_INVAL;

    /* Reject empty requests */
    if (perm->size == 0)
        return E_INVAL;

    if (perm->base + perm->size < perm->base)
        return E_INVAL;

    return 0;
}

static inline int cap_io_port_grant_check(const cap_io_port* perm)
{
    if (!perm)
        return E_INVAL;

    /* Reject empty requests */
    if (perm->count == 0)
        return E_INVAL;

    if (perm->base + perm->count < perm->base)
        return E_INVAL;

    return 0;
}

static inline int cap_permission_grant_check(const int* perm)
{
    if (!perm)
        return E_INVAL;

    return (*perm == 1) ? 0 : E_INVAL;
}

int cap_check(struct pcb* proc, cap_type type, const void* args)
{
    int ret = E_PERM;

    if (!proc || !proc->cap_lock)
        return E_INVAL;

    spinlock_lock(proc->cap_lock);

    list_for_each(node, &proc->capabilities) {
        capability* cap = list_entry(node, capability, this_node);
        if (cap->type != type)
            continue;

        switch (type) {
        CASE_CAP_CHECK(CAP_IRQ_OWN,       irq,                 cap_irq_check)
        CASE_CAP_CHECK(CAP_MEM_MAP,       mem,                 cap_mem_check)
        CASE_CAP_CHECK(CAP_IO_ACCESS,     io_port,             cap_io_port_check)
        CASE_CAP_CHECK(CAP_IPC,           issue_ipc,           cap_permission_check)
        CASE_CAP_CHECK(CAP_PROC_CREATE,   issue_proc_create,   cap_permission_check)
        CASE_CAP_CHECK(CAP_THREAD_CREATE, issue_thread_create, cap_permission_check)
        }

        if (ret == 0)
            break;
    }

    spinlock_unlock(proc->cap_lock);
    return ret;
}

int cap_grant(struct pcb* proc, cap_type type, const void* args)
{
    capability* cap = 0;
    int ret = 0;

    if (!proc || !args || !proc->cap_lock)
        return E_INVAL;

    spinlock_lock(proc->cap_lock);

    /* 1. Validate the requested capability before doing anything. */
    switch (type) {
    CASE_CAP_GRANT_CHECK(CAP_IRQ_OWN,       cap_irq_grant_check)
    CASE_CAP_GRANT_CHECK(CAP_MEM_MAP,       cap_mem_grant_check)
    CASE_CAP_GRANT_CHECK(CAP_IO_ACCESS,     cap_io_port_grant_check)
    CASE_CAP_GRANT_CHECK(CAP_IPC,           cap_permission_grant_check)
    CASE_CAP_GRANT_CHECK(CAP_PROC_CREATE,   cap_permission_grant_check)
    CASE_CAP_GRANT_CHECK(CAP_THREAD_CREATE, cap_permission_grant_check)
    default:
        ret = E_INVAL;
        goto unlock;
    }

    /* 2. Refuse duplicates. */
    switch (type) {
    CASE_CAP_IS_DUP(CAP_IRQ_OWN,       irq,                 cap_irq_cmp)
    CASE_CAP_IS_DUP(CAP_MEM_MAP,       mem,                 cap_mem_cmp)
    CASE_CAP_IS_DUP(CAP_IO_ACCESS,     io_port,             cap_io_port_cmp)
    CASE_CAP_IS_DUP(CAP_IPC,           issue_ipc,           cap_permission_cmp)
    CASE_CAP_IS_DUP(CAP_PROC_CREATE,   issue_proc_create,   cap_permission_cmp)
    CASE_CAP_IS_DUP(CAP_THREAD_CREATE, issue_thread_create, cap_permission_cmp)
    default:
        ret = E_INVAL;
        goto unlock;
    }

    cap = kmalloc(sizeof(capability));
    if (!cap) {
        ret = E_NOMEM;
        goto unlock;
    }

    /* 3. Insert the new capability. */
    switch (type) {
    CASE_CAP_GRANT(CAP_IRQ_OWN,       irq)
    CASE_CAP_GRANT(CAP_MEM_MAP,       mem)
    CASE_CAP_GRANT(CAP_IO_ACCESS,     io_port)
    CASE_CAP_GRANT(CAP_IPC,           issue_ipc)
    CASE_CAP_GRANT(CAP_PROC_CREATE,   issue_proc_create)
    CASE_CAP_GRANT(CAP_THREAD_CREATE, issue_thread_create)
    default:
        kfree(cap);
        ret = E_INVAL;
        goto unlock;
    }

unlock:
    spinlock_unlock(proc->cap_lock);
    return ret;
}

int cap_revoke(struct pcb* proc, cap_type type, const void* args)
{
    if (!proc || !args || !proc->cap_lock)
        return E_INVAL;

    spinlock_lock(proc->cap_lock);

    list_for_each_safe(node, n, &proc->capabilities) {
        capability* cap = list_entry(node, capability, this_node);
        if (cap->type != type)
            continue;

        switch (type) {
        CASE_CAP_REVOKE(CAP_IRQ_OWN,       irq,                 cap_irq_cmp)
        CASE_CAP_REVOKE(CAP_MEM_MAP,       mem,                 cap_mem_cmp)
        CASE_CAP_REVOKE(CAP_IO_ACCESS,     io_port,             cap_io_port_cmp)
        CASE_CAP_REVOKE(CAP_IPC,           issue_ipc,           cap_permission_cmp)
        CASE_CAP_REVOKE(CAP_PROC_CREATE,   issue_proc_create,   cap_permission_cmp)
        CASE_CAP_REVOKE(CAP_THREAD_CREATE, issue_thread_create, cap_permission_cmp)
        }
    }

    spinlock_unlock(proc->cap_lock);
    return 0;
}

void cap_revoke_all(struct pcb* proc)
{
    if (!proc || !proc->cap_lock)
        return;

    spinlock_lock(proc->cap_lock);

    list_for_each_safe(node, n, &proc->capabilities) {
        capability* cap = list_entry(node, capability, this_node);
        list_del(node);
        kfree(cap);
    }

    spinlock_unlock(proc->cap_lock);
}
