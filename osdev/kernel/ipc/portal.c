#include "ipc/portal.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "kernel/process.h"
#include "ipc/shm.h"
#include "sync/spinlock.h"

static DECLARE_HEAD_NODE(portal_header);
static spinlock portal_lock = { .state = LOCK_UNLOCKED };

int portal_init(portal* p)
{
    static u32 id = 0;

    if (!p)
        return E_INVAL;

    memset(p, 0, sizeof(portal));
    p->id = id++;
    p->pid = proc_get_pid();
    p->tid = thread_get_tid();
    wait_queue_init(&p->client_wq);
    wait_queue_init(&p->server_wq);
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
    return 0;
}

int portal_wait(u32 portal_id)
{
    return 0;
}

int portal_reply(u32 portal_id, int ret, size_t out_len)
{
    return 0;
}
