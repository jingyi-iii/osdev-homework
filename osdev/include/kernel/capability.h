#ifndef CAPABILITY_H
#define CAPABILITY_H

#include <stdint.h>
#include "lib/list.h"

typedef enum {
    CAP_IRQ_OWN,
    CAP_MEM_MAP,
    CAP_IO_ACCESS,
    CAP_IPC,
    CAP_PROC_CREATE,
    CAP_THREAD_CREATE,
} cap_type;

typedef struct cap_mem {
    uint32_t base;
    uint32_t size;
    uint32_t flags;
} cap_mem;

typedef struct cam_io_port {
    uint32_t base;
    uint32_t count;
} cap_io_port;

typedef struct capability {
    list_node this_node;
    cap_type type;
    union {
        uint32_t irq;
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

#endif
