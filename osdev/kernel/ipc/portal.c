#include "ipc/portal.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "kernel/process.h"
#include "ipc/shm.h"
#include "sync/spinlock.h"
#include "lib/module.h"
#include "arch_irq.h"
#include "kernel/syscall.h"

static DECLARE_HEAD_NODE(portal_header);
static spinlock portal_lock = { .state = LOCK_UNLOCKED };

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

static portal_req* portal_get_req_by_id(portal* ptl, u32 id)
{
    if (!ptl)
        return 0;

    spinlock_lock(&portal_lock);
    list_for_each(node, &ptl->reqs) {
        portal_req* p = list_entry(node, portal_req, this_node);
        if (p->client_id == id || id == PORTAL_ID_ANY) {
            spinlock_unlock(&portal_lock);
            return p;
        }
    }
    spinlock_unlock(&portal_lock);

    return 0;
}

static int portal_syscall_isr(void* data)
{
    if (!data)
        return E_INVAL;

    portal_ctrl_config* config = data;
    switch (config->cmd)
    {
    case PORTAL_CTRL_WAIT:
        config->out = portal_wait(config->client_id);
        return 0;

    case PORTAL_CTRL_MMAP:
        return shm_share(config->target_pid,
                        config->va, config->va_size, &config->out);

    case PORTAL_CTRL_UNMMAP:
        return shm_unshare(config->target_pid, config->va);

    case PORTAL_CTRL_REPLY:
        return portal_reply(config->req);
    
    default:
        return E_INVAL;
    }
}

static i32 portal_scall_handle = -1;

void portal_syscall_init(void)
{
    portal_scall_handle = syscall_register(portal_syscall_isr, sizeof(portal_ctrl_config));
}

int portal_init(portal* p)
{
    static u32 id = PORTAL_ID_ANY + 1;

    if (!p)
        return E_INVAL;

    memset(p, 0, sizeof(portal));
    p->id = id++;
    p->pid = proc_get_pid();
    p->tid = thread_get_tid();
    p->req_sem =  semaphore_create(0);
    if (!p->req_sem) {
        memset(p, 0, sizeof(portal));
        return E_NOMEM;
    }

    list_init(&p->reqs);
    list_init(&p->this_node);

    spinlock_lock(&portal_lock);
    list_add(&p->this_node, &portal_header);
    spinlock_unlock(&portal_lock);

    return 0;
}

void portal_destroy(portal* p)
{
    if (!p)
        return;

    /* Drop any requests still parked on the portal.  Callers must ensure
     * no client is blocked mid-call (its req / done_sem would be freed out
     * from under it). */
    spinlock_lock(&portal_lock);
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
}


int portal_call(u32 portal_id, void* va, size_t size)
{
    portal* ptl = 0;
    portal_req* req = 0;
    void* target_va = 0;
    int found = 0;
    int ret = 0;

    spinlock_lock(&portal_lock);
    list_for_each(node, &portal_header) {
        ptl = list_entry(node, portal, this_node);
        if (ptl->id == portal_id) {
            found = 1;
            break;
        }
    }
    spinlock_unlock(&portal_lock);

    if (!found)
        return E_NOTFOUND;

    /* Share the caller's buffer with the server process so the payload is
     * reachable on both sides.  Ring-3 callers go through the MMAP syscall
     * gate (the handler runs with the caller's CR3); ring-0 callers call
     * shm_share directly. */
    if (arch_running_ring3()) {
        portal_ctrl_config config;
        int r;
        config.server_id = portal_id;
        config.cmd = PORTAL_CTRL_MMAP;
        config.va = va;
        config.va_size = size;
        config.target_pid = ptl->pid;
        r = arch_syscall(portal_scall_handle, &config, sizeof(config));
        if (r != 0)
            return r;
        target_va = config.out;
    } else {
        ret = shm_share(ptl->pid, va, size, &target_va);
        if (ret)
            return ret;
    }

    req = kmalloc(sizeof(portal_req));
    if (!req)
        return E_NOMEM;
    memset(req, 0, sizeof(*req));
    req->client_id = thread_get_tid();
    req->shm_va = target_va;
    req->shm_size = size;
    req->done_sem = semaphore_create(0);
    if (!req->done_sem) {
        kfree(req);
        return E_NOMEM;
    }

    spinlock_lock(&portal_lock);
    list_add(&req->this_node, &ptl->reqs);
    spinlock_unlock(&portal_lock);

    /* Wake the server: one req_sem signal per posted request. */
    semaphore_signal(ptl->req_sem->id);

    /* Block until the server replies to THIS request.  `done_sem` is a
     * per-request semaphore, so a reply can only ever wake the client
     * that posted this exact req (no cross-client wakeups). */
    semaphore_wait(req->done_sem->id);

    /* RESP here */
    ret = req->resp.ret;
    spinlock_lock(&portal_lock);
    list_del(&req->this_node);
    spinlock_unlock(&portal_lock);
    semaphore_destroy(req->done_sem);
    kfree(req);

    if (arch_running_ring3()) {
        portal_ctrl_config config;
        config.cmd = PORTAL_CTRL_UNMMAP;
        config.va = target_va;
        config.target_pid = ptl->pid;
        arch_syscall(portal_scall_handle, &config, sizeof(config));
    } else {
        shm_unshare(ptl->pid, target_va);
    }

    return ret;
}

portal_req* portal_wait(u32 portal_id)
{
    portal* ptl = portal_get_by_tid(thread_get_tid());
    if (!ptl)
        return 0;

    /* Block until a client posts a request. */
    semaphore_wait(ptl->req_sem->id);
    return portal_get_req_by_id(ptl, portal_id);
}

int portal_reply(portal_req* req)
{
    portal* ptl;

    if (!req || !req->done_sem)
        return E_INVAL;

    ptl = portal_get_by_tid(thread_get_tid());
    if (!ptl)
        return E_NOTFOUND;

    /* Wake exactly the client that posted @req. */
    semaphore_signal(req->done_sem->id);

    return 0;
}

module_init(portal_syscall_init);
