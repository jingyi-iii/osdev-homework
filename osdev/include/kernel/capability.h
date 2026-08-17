#ifndef CAPABILITY_H
#define CAPABILITY_H

#include "lib/types.h"
#include "lib/list.h"

typedef enum {
    CAP_OWN_IRQ,
    CAP_MAP_MEM,
    CAP_ACCESS_IO,
    CAP_IPC,
    CAP_CREATE_KRNL_PROC,
    CAP_CREATE_KRNL_THREAD,
} cap_type;

typedef struct cap_mem {
    u32 base;
    u32 size;
    u32 flags;
} cap_mem;

typedef struct cam_io_port {
    u32 base;
    u32 count;
} cap_io_port;

typedef struct capability {
    list_node this_node;
    cap_type type;
    union {
        u32 irq;
        cap_mem mem;
        cap_io_port io_port;
        int issue_ipc;
        int issue_proc_create;
        int issue_thread_create;
    };
} capability;

struct pcb;
int cap_check(struct pcb* proc, cap_type type, const void* args);
int cap_grant(struct pcb* proc, cap_type type, const void* args);
int cap_revoke(struct pcb* proc, cap_type type, const void* args);
void cap_revoke_all(struct pcb* proc);

/* Copy a parent process's capabilities into a freshly created child
 * (capability flow-down).  Used by p_create() so user processes spawned by
 * a granted process keep the grants they need. */
void cap_inherit_all(struct pcb* child, struct pcb* parent);

#endif
