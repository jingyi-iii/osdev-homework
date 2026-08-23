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
        if (p->tid == tid) {
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

    case PORTAL_CTRL_CALL:
        return portal_call(config->server_id, config->va, config->va_size);

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
    wait_queue_init(&p->client_wq);
    wait_queue_init(&p->server_wq);
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

    wait_queue_destroy(&p->client_wq);
    wait_queue_destroy(&p->server_wq);
    list_del(&p->this_node);
}


int portal_call(u32 portal_id, void* va, size_t size)
{
    portal* ptl = 0;
    portal_req* req = 0;
    void* target_va = 0;
    int found = 0;
    int ret = 0;

    if (arch_running_ring3()) {
        portal_ctrl_config config;
        config.server_id = portal_id;
        config.cmd = PORTAL_CTRL_CALL;
        config.va = va;
        config.va_size = size;
        return arch_syscall(portal_scall_handle, &config, sizeof(config));
    }

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

    ret = shm_share(ptl->pid, va, size, &target_va);
    if (ret)
        return ret;

    req = kmalloc(sizeof(portal_req));
    if (!req)
        return E_NOMEM;
    req->client_id = thread_get_tid();
    req->shm_va = target_va;
    req->shm_size = size;
    spinlock_lock(&portal_lock);
    list_add(&req->this_node, &ptl->reqs);
    spinlock_unlock(&portal_lock);

    spinlock_lock(ptl->client_wq.sp_lock);
    wait_queue_sleep_locked(&ptl->client_wq);
    spinlock_unlock(ptl->client_wq.sp_lock);

    /* RESP here */
    ret = req->resp.ret;
    list_del(&req->this_node);
    shm_unshare(ptl->pid, target_va);

    return ret;
}

portal_req* portal_wait(u32 portal_id)
{
    for ( ;; ) {
        if (arch_running_ring3()) {
            portal_ctrl_config config;
            config.client_id = portal_id;
            config.cmd = PORTAL_CTRL_WAIT;
            config.out = 0;
            arch_syscall(portal_scall_handle, &config, sizeof(config));
            
            if (config.out) {
                return (portal_req*)config.out;
            } else {
                thread_yield();
                continue;
            }
        }

        portal* ptl = portal_get_by_tid(thread_get_tid());
        if (!ptl)
            return 0;

        return portal_get_req_by_id(ptl, portal_id);;
    }
}

int portal_reply(portal_req* req)
{
    if (!req)
        return E_INVAL;

    if (arch_running_ring3()) {
        portal_ctrl_config config;
        config.cmd = PORTAL_CTRL_REPLY;
        config.req = req;
        return arch_syscall(portal_scall_handle, &config, sizeof(config));
    }

    portal* ptl = portal_get_by_tid(thread_get_tid());
    wait_queue_wake_by_tid(&ptl->client_wq, req->client_id);

    return 0;
}

module_init(portal_syscall_init);
