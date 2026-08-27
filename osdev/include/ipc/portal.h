#ifndef PORTAL_H
#define PORTAL_H

#include "lib/types.h"
#include "sync/semaphore.h"
#include "lib/list.h"
#include <stddef.h>

#define PORTAL_ID_ANY  (0)

typedef struct portal_resp {
    u8* payload;
    u32 resp_size;
    int ret;
} portal_resp;

typedef struct portal_req {
    u32 client_id;
    void* shm_va;
    u32 shm_size;
    u8* payload;
    u32 req_size;

    semaphore* done_sem;          /* per-request: client blocks, server signals */
    portal_resp resp;
    list_node this_node;
} portal_req;

typedef struct portal {
    u32         id;
    i32         pid;
    i32         tid;

    list_node   reqs;

    semaphore*  req_sem;      /* server waits here for a request to arrive */

    enum {
        PORTAL_IDLE = 0,
        PORTAL_REQ_SENT,
        PORTAL_REP_SENT,
    } state;

    list_node   this_node;
} portal;

typedef struct portal_ctrl_config {
    enum {
        PORTAL_CTRL_WAIT = 0,
        PORTAL_CTRL_CALL,
        PORTAL_CTRL_REPLY,
        PORTAL_CTRL_MMAP,
        PORTAL_CTRL_UNMMAP,
    } cmd;
    u32 client_id;
    u32 server_id;
    u32 target_pid;
    void* va;
    size_t va_size;
    portal_req* req;
    void* out;
} portal_ctrl_config;

int portal_init(portal* p);
void portal_destroy(portal* p);
int portal_call(u32 portal_id, void* va, size_t size);
portal_req* portal_wait(u32 portal_id);
int portal_reply(portal_req* req);

#endif
