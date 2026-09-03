#include "ipc/portal.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "kernel/process.h"
#include "ipc/shm.h"
#include "sync/spinlock.h"
#include "lib/module.h"
#include "arch_irq.h"
#include "kernel/syscall.h"
#include "kernel/uapi.h"
#include "mm/heap.h"

static DECLARE_HEAD_NODE(portal_header);
static spinlock portal_lock = { .state = LOCK_UNLOCKED };

static u32 portal_next_id = PORTAL_ID_ANY + 1;

static inline int portal_run_direct(void)
{
    return !arch_running_ring3() && arch_in_gate();
}

static portal* portal_get_by_id(u32 id)
{
    spinlock_lock(&portal_lock);
    list_for_each(node, &portal_header) {
        portal* p = list_entry(node, portal, this_node);
        if (p->id == id) {
            spinlock_unlock(&portal_lock);
            return p;
        }
    }
    spinlock_unlock(&portal_lock);

    return 0;
}

static portal* portal_get_by_tid(u32 tid)
{
    spinlock_lock(&portal_lock);
    list_for_each(node, &portal_header) {
        portal* p = list_entry(node, portal, this_node);
        if ((u32)p->tid == tid) {
            spinlock_unlock(&portal_lock);
            return p;
        }
    }
    spinlock_unlock(&portal_lock);

    return 0;
}

/*
 * Shared portal logic — the single place that maps a PORTAL_CTRL_*
 * command to the kernel implementation.  It runs in two ways:
 *   - directly, from the public wrappers when portal_run_direct() (ring 0
 *     inside a gate), and
 *   - inside the syscall gate via portal_syscall_isr() (ring-3 callers).
 *
 * There is deliberately NO capability check here: kernel/ISR callers run
 * against whatever process happens to be scheduled and must not be
 * subjected to the ring-3 CAP_IPC gate.  portal_syscall_isr() applies
 * that check on the trap path only.
 *
 * Blocking rule (WAIT / WAIT_REPLY): the whole body runs inside the
 * int $100 gate (ring-0, IF=0).  WAIT / WAIT_REPLY park the caller with
 * a plain semaphore_wait() as the LAST meaningful action of the gate
 * body.  This kernel defers context switches to the gate exit, so a
 * blocked caller is suspended at its own gate exit and resumes in user
 * mode right after the syscall — never in the middle of the gate.  Any
 * state-dependent post-wake logic therefore lives on the user side as
 * separate, non-blocking syscalls (GET_RESULT / GET_REQ) that run after
 * the park.  Keep it that way: do not turn WAIT / WAIT_REPLY into a
 * blocking retry loop (mailbox's LISTEN is non-blocking for exactly
 * this reason).
 */
static int portal_exec(portal_ctrl_config* cfg)
{
    portal* p = 0;
    portal_req* req = 0;
    int ret = 0;

    if (!cfg)
        return E_INVAL;

    switch (cfg->cmd)
    {
    case PORTAL_CTRL_INIT:
        p = (portal*)kmalloc(sizeof(portal));
        if (!p)
            return E_NOMEM;
        memset(p, 0, sizeof(*p));
        /* A non-zero cfg->server_id requests a specific well-known id (the
         * terminal server publishes the console portal at the fixed
         * PORTAL_ID_CONSOLE so separately-linked user ELFs can find it via
         * the ABI constant).  0 = auto-assign. */
        if (cfg->server_id != 0) {
            if (portal_get_by_id(cfg->server_id)) {
                kfree(p);
                return E_EXISTS;
            }
            p->id = cfg->server_id;
        } else {
            p->id = portal_next_id;
        }
        if (p->id >= portal_next_id)
            portal_next_id = p->id + 1;
        p->pid = proc_get_pid();
        p->tid = thread_get_tid();
        p->req_sem = semaphore_create(0);
        if (!p->req_sem) {
            kfree(p);
            return E_NOMEM;
        }
        list_init(&p->reqs);
        list_init(&p->this_node);
        spinlock_lock(&portal_lock);
        list_add(&p->this_node, &portal_header);
        spinlock_unlock(&portal_lock);
        cfg->out = (void*)(uptr)p->id;
        return 0;

    case PORTAL_CTRL_DESTROY:
        p = portal_get_by_id(cfg->server_id);
        if (!p)
            return E_NOTFOUND;
        /* All-or-nothing teardown.  If any queued request still has a
         * blocked client (done_sem with waiters) or another server thread
         * is parked in WAIT (req_sem with waiters), refuse: freeing the
         * req / sem would strand the waiter on a freed handle (UAF in its
         * later CLEANUP) and leak the semaphore.  The caller must drain
         * in-flight calls before destroying.  This runs inside one gate
         * with IF=0, so the waiters check and the teardown below cannot
         * be raced by a concurrent WAIT_REPLY. */
        spinlock_lock(&portal_lock);
        if (p->req_sem && semaphore_has_waiters(p->req_sem)) {
            spinlock_unlock(&portal_lock);
            return E_BUSY;
        }
        list_for_each(pos, &p->reqs) {
            portal_req* r = list_entry(pos, portal_req, this_node);
            if (r->done_sem && semaphore_has_waiters(r->done_sem)) {
                spinlock_unlock(&portal_lock);
                return E_BUSY;
            }
        }
        list_for_each_safe(pos, n, &p->reqs) {
            portal_req* r = list_entry(pos, portal_req, this_node);
            list_del(pos);
            if (r->done_sem)
                semaphore_destroy(r->done_sem);
            kfree(r);
        }
        list_del(&p->this_node);
        spinlock_unlock(&portal_lock);
        if (p->req_sem)
            semaphore_destroy(p->req_sem);
        kfree(p);
        return 0;

    case PORTAL_CTRL_CALL:
        p = portal_get_by_id(cfg->server_id);
        if (!p)
            return E_NOTFOUND;

        /* Map the caller's buffer into the server's address space. */
        ret = shm_share(p->pid, cfg->va, cfg->va_size, &cfg->out);
        if (ret)
            return ret;

        req = (portal_req*)kmalloc(sizeof(portal_req));
        if (!req) {
            /* shm_share() above already mapped the buffer into the
             * server's address space (and granted it CAP_MAP_MEM).  Roll
             * that back so the server does not keep a stale mapping +
             * grant — a later share of the same buffer would fail on the
             * duplicate cap grant. */
            shm_unshare(p->pid, cfg->out);
            return E_NOMEM;
        }
        memset(req, 0, sizeof(*req));
        req->client_id = thread_get_tid();
        req->server_pid = p->pid;
        req->shm_va = cfg->out;
        req->shm_size = (u32)cfg->va_size;
        req->done_sem = semaphore_create(0);
        if (!req->done_sem) {
            shm_unshare(p->pid, cfg->out);
            kfree(req);
            return E_NOMEM;
        }

        spinlock_lock(&portal_lock);
        list_add(&req->this_node, &p->reqs);
        spinlock_unlock(&portal_lock);

        /* Wake the server: one signal per posted request. */
        semaphore_signal(p->req_sem->id);

        cfg->req = req;                 /* opaque handle back to the client */
        return 0;

    case PORTAL_CTRL_WAIT_REPLY:
        req = cfg->req;
        if (!req || !req->done_sem)
            return E_INVAL;
        /* Tail-block: suspended at this gate's exit, resumes in user mode
         * after the syscall.  No post-block logic here. */
        semaphore_wait(req->done_sem->id);
        return 0;

    case PORTAL_CTRL_GET_RESULT:
        req = cfg->req;
        if (!req)
            return E_INVAL;
        /* Runs only after the client has been woken (the reply was stored
         * by PORTAL_CTRL_REPLY before the signal), so resp.ret is fresh. */
        cfg->ret = req->resp.ret;
        return 0;

    case PORTAL_CTRL_CLEANUP:
        req = cfg->req;
        if (!req)
            return E_INVAL;
        if (req->shm_va)
            ret = shm_unshare(req->server_pid, req->shm_va);
        spinlock_lock(&portal_lock);
        if (!req->dequeued)
            list_del(&req->this_node);
        spinlock_unlock(&portal_lock);
        if (req->done_sem)
            semaphore_destroy(req->done_sem);
        kfree(req);
        return ret;

    case PORTAL_CTRL_WAIT:
        p = portal_get_by_tid(thread_get_tid());
        if (!p)
            return E_NOTFOUND;
        /* Tail-block; the request is dequeued by the following GET_REQ. */
        semaphore_wait(p->req_sem->id);
        return 0;

    case PORTAL_CTRL_GET_REQ:
        p = portal_get_by_tid(thread_get_tid());
        if (!p)
            return E_NOTFOUND;
        spinlock_lock(&portal_lock);
        req = 0;
        if (!list_empty(&p->reqs)) {
            req = list_entry(p->reqs.next, portal_req, this_node);
            list_del(&req->this_node);   /* hand off: dequeue now */
            req->dequeued = 1;
        }
        spinlock_unlock(&portal_lock);
        cfg->req = req;
        if (req) {
            cfg->va = req->shm_va;
            cfg->va_size = req->shm_size;
        }
        return 0;

    case PORTAL_CTRL_REPLY:
        req = cfg->req;
        if (!req || !req->done_sem)
            return E_INVAL;
        /* Store the response before signaling; the client's GET_RESULT
         * runs only after it has been woken, so it sees this value. */
        req->resp.ret = cfg->ret;
        semaphore_signal(req->done_sem->id);
        return 0;

    default:
        return E_INVAL;
    }
}

/*
 * Portal syscall gate (SYSCALL_PORTAL).  Ring-3 entry only: applies the
 * CAP_IPC check, then defers to the shared portal_exec().
 */
static int portal_syscall_isr(void* data)
{
    portal_ctrl_config* cfg = (portal_ctrl_config*)data;
    if (!cfg)
        return E_INVAL;

    /*
     * CAP_IPC gate: a user (CPL3) process may only use the portal IPC
     * service if it holds a CAP_IPC grant.  Kernel processes / drivers
     * are trusted and skip the check.  The handler runs in the caller's
     * context, so get_current_process() is the process behind the
     * syscall.
     */
    pcb* proc = get_current_process();
    if (proc && proc->priv != PROC_PRIV_KERNEL) {
        int ipc_ok = 1;
        if (cap_check(proc, CAP_IPC, &ipc_ok) != 0) {
            cfg->ret = E_PERM;
            return cfg->ret;
        }
    }

    return portal_exec(cfg);
}

static i32 portal_scall_handle = -1;

void portal_syscall_init(void)
{
    portal_scall_handle = syscall_register(SYSCALL_PORTAL,
        portal_syscall_isr, sizeof(portal_ctrl_config));
}

/*
 * Public API — thin wrappers.  Every one of them only builds a config and
 * traps through the portal syscall gate, so ring-3 code never touches
 * kernel locks / heap / semaphores directly.
 */

int portal_init(u32* out_id)
{
    return portal_init_fixed(0, out_id);
}

int portal_init_fixed(u32 want_id, u32* out_id)
{
    portal_ctrl_config cfg = {0};
    int ret = 0;

    if (!out_id)
        return E_INVAL;

    cfg.cmd = PORTAL_CTRL_INIT;
    cfg.server_id = want_id;

    if (portal_run_direct())
        ret = portal_exec(&cfg);
    else
        ret = arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));

    if (ret)
        return ret;

    *out_id = (u32)(uptr)cfg.out;
    return 0;
}

int portal_destroy(u32 portal_id)
{
    portal_ctrl_config cfg = {0};

    cfg.cmd = PORTAL_CTRL_DESTROY;
    cfg.server_id = portal_id;

    if (portal_run_direct())
        return portal_exec(&cfg);
    else
        return arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));
}

int portal_call(u32 portal_id, void* va, size_t size)
{
    portal_ctrl_config cfg = {0};
    portal_req* req;
    int ret;

    /* ① enqueue (kernel: shm_share + req alloc + register + wake server) */
    cfg.cmd = PORTAL_CTRL_CALL;
    cfg.server_id = portal_id;
    cfg.va = va;
    cfg.va_size = size;
    if (portal_run_direct())
        ret = portal_exec(&cfg);
    else
        ret = arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));

    if (ret)
        return ret;
    if (!cfg.req)
        return E_INVAL;
    req = cfg.req;

    /* ② park until the server replies (the block takes effect at the
     *    gate exit; on resume the reply is already stored) */
    cfg.cmd = PORTAL_CTRL_WAIT_REPLY;
    cfg.req = req;
    if (portal_run_direct())
        portal_exec(&cfg);
    else
        arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));

    /* ③ fetch the response code — only valid once we have been woken */
    cfg.cmd = PORTAL_CTRL_GET_RESULT;
    cfg.req = req;
    if (portal_run_direct())
        portal_exec(&cfg);
    else
        arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));
    ret = cfg.ret;

    /* ④ tear down (kernel: shm_unshare + free req) */
    cfg.cmd = PORTAL_CTRL_CLEANUP;
    cfg.req = req;
    if (portal_run_direct())
        portal_exec(&cfg);
    else
        arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));

    return ret;
}

portal_req* portal_wait(u32 portal_id, void** out_shm_va, u32* out_shm_size)
{
    portal_ctrl_config cfg = {0};

    /* ① park until a request arrives (tail-block) */
    cfg.cmd = PORTAL_CTRL_WAIT;
    cfg.client_id = portal_id;
    if (portal_run_direct())
        portal_exec(&cfg);
    else
        arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));

    /* ② dequeue the request now that one is known to be pending */
    cfg.cmd = PORTAL_CTRL_GET_REQ;
    if (portal_run_direct())
        portal_exec(&cfg);
    else
        arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));

    if (!cfg.req)
        return 0;
    if (out_shm_va)
        *out_shm_va = cfg.va;
    if (out_shm_size)
        *out_shm_size = (u32)cfg.va_size;
    return cfg.req;
}

int portal_reply(portal_req* req, int resp_ret)
{
    portal_ctrl_config cfg = {0};

    cfg.cmd = PORTAL_CTRL_REPLY;
    cfg.req = req;
    cfg.ret = resp_ret;
    if (portal_run_direct())
        return portal_exec(&cfg);
    else
        return arch_syscall(portal_scall_handle, &cfg, sizeof(cfg));
}

module_init(portal_syscall_init);
