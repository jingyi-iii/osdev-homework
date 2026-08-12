#ifndef __IRQDEV_H__
#define __IRQDEV_H__

#include <stdint.h>
#include <stddef.h>
#include "lib/list.h"
#include "sync/spinlock.h"
#include "kernel/errno.h"

#define IRQ_ANY_MINOR  UINT32_MAX

typedef void (*irq_handler_fn)(void* context);

typedef struct irq {
    const char *name;
    void *context;
    uint32_t major;
    uint32_t minor;
    int enabled;
    int is_user_irq;
    int tid;
    void* owner;        /* registering thread's tcb (user IRQ only) */
    spinlock* sp_lock;
    list_node node;
    list_node thread_node;  /* bind with tcb->irqs */
    irq_handler_fn handler;
} irq;

typedef struct irqline {
    uint32_t major;
    int enabled;
    spinlock* sp_lock;
    list_node irqs;
} irqline;

int irq_request(irq **out, const char* name, uint32_t major, uint32_t minor,
                    irq_handler_fn cb, void* cb_param);
void irq_release(irq *p);
int irq_mask(struct irq* p);
int irq_unmask(struct irq* p);

/*
 * IRQ syscall gate (major 100) for RING3 access.
 * The four functions above transparently route through this gate when the
 * caller runs in user mode (CPL3).
 */
#define IRQ_SYSCALL_MINOR       (5)

/* IRQ syscall commands */
typedef enum {
    IRQ_SYSCALL_REQUEST = 0,
    IRQ_SYSCALL_RELEASE = 1,
    IRQ_SYSCALL_MASK    = 2,
    IRQ_SYSCALL_UNMASK  = 3,
} irq_syscall_cmd;

/* Data structure carried through the IRQ syscall gate */
typedef struct irq_syscall_data {
    uint8_t        cmd;       /* irq_syscall_cmd                            */
    irq*           handle;    /* out (request) / in (release, mask, unmask) */
    const char*    name;      /* request: irq name                          */
    uint32_t       major;     /* request: IRQ major                         */
    uint32_t       minor;     /* request: IRQ minor                         */
    irq_handler_fn handler;   /* request: callback                          */
    void*          param;     /* request: callback param                    */
    int            is_user_irq;  /* request: is user mode irq               */
    int            tid;
    int            ret;       /* out: return code                           */
} irq_syscall_data;

void irq_syscall_init(void);
void irq_syscall_exit(void);

#endif
