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

/*
 * Kernel-owned request record.  User code only holds an opaque
 * portal_req* handle returned by portal_wait() and must NEVER
 * dereference it — pass it back to portal_reply() as-is.
 */
typedef struct portal_req {
    u32 client_id;
    i32 server_pid;          /* portal owner pid (used by shm_unshare)  */
    void* shm_va;            /* server-side VA of the shared payload    */
    u32 shm_size;
    int  dequeued;           /* 1 once GET_REQ took it off the list     */

    semaphore* done_sem;     /* per-request: client blocks, server signals */
    portal_resp resp;
    list_node this_node;
} portal_req;

/*
 * Kernel-owned portal.  User code only holds the numeric id returned by
 * portal_init() and passes it back to portal_call()/portal_destroy().
 */
typedef struct portal {
    u32         id;
    i32         pid;
    i32         tid;
    list_node   reqs;
    semaphore*  req_sem;     /* server waits here for a request to arrive */
    list_node   this_node;
} portal;

/*
 * Data structure carried through the portal syscall gate.  Every public
 * portal API below is a thin wrapper that traps through this gate, so
 * ring-3 code never touches kernel locks / heap / semaphores directly.
 */
typedef struct portal_ctrl_config {
    enum {
        PORTAL_CTRL_INIT = 0,   /* create a portal for the current thread */
        PORTAL_CTRL_DESTROY,    /* destroy a portal                      */
        PORTAL_CTRL_CALL,       /* client: shm_share + enqueue req       */
        PORTAL_CTRL_WAIT_REPLY, /* client: park until the server replies */
        PORTAL_CTRL_GET_RESULT, /* client: read resp.ret (after wake)    */
        PORTAL_CTRL_CLEANUP,    /* client: shm_unshare + free req        */
        PORTAL_CTRL_WAIT,       /* server: park until a request arrives  */
        PORTAL_CTRL_GET_REQ,    /* server: dequeue the next request      */
        PORTAL_CTRL_REPLY,      /* server: store resp.ret + wake client  */
    } cmd;
    u32         client_id;
    u32         server_id;      /* portal id (in)                          */
    void*       va;             /* client buf (in) / shm_va (out, GET_REQ) */
    size_t      va_size;        /* size (in) / shm size (out, GET_REQ)     */
    portal_req* req;            /* opaque req handle                       */
    void*       out;            /* INIT: portal id (out); CALL: shm_va     */
    int         ret;            /* GET_RESULT: resp.ret (out) / REPLY (in) */
} portal_ctrl_config;

int  portal_init(u32* out_id);
int  portal_init_fixed(u32 want_id, u32* out_id);
int  portal_destroy(u32 portal_id);
int  portal_call(u32 portal_id, void* va, size_t size);
portal_req* portal_wait(u32 portal_id, void** out_shm_va, u32* out_shm_size);
int  portal_reply(portal_req* req, int resp_ret);

#endif
