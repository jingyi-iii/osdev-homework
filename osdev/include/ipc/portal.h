#ifndef PORTAL_H
#define PORTAL_H

#include "lib/types.h"
#include "sync/wait_queue.h"
#include "lib/list.h"
#include <stddef.h>

typedef struct portal {
    u32         id;
    i32         pid;
    i32         tid;
    void*       shm_va;
    u32         shm_size;
    int         client_ret;

    wait_queue  client_wq;
    wait_queue  server_wq;

    enum {
        PORTAL_IDLE = 0,
        PORTAL_REQ_SENT,
        PORTAL_REP_SENT,
    } state;

    list_node   this_node;
} portal;

typedef struct portal_req {
    u32 client_in_len;
    u32 server_out_len;
    u32 server_error;
    u8* payload;
} portal_req;

int portal_init(portal* p);
void portal_destroy(portal* p);
int portal_call(u32 portal_id, void* va, size_t size);
int portal_wait(u32 portal_id);
int portal_reply(u32 portal_id, int ret, size_t out_len);

#endif
