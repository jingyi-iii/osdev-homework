#ifndef __ARCH_TASK_H
#define __ARCH_TASK_H

#include "arch_protm.h"
#include "mm/heap.h"

typedef struct regs {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t kesp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t esp;
    uint32_t ss;
} __attribute__((packed)) regs_t;

typedef struct tss {
    uint32_t backlink;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1;
    uint32_t ss1;
    uint32_t esp2;
    uint32_t ss2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t flags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iobase;
} __attribute__((packed)) tss_t;

typedef void (*task_entry_t)(void);

typedef enum task_priv {
    TASK_PRIV_KERNEL = 0,
    TASK_PRIV_USER,
} task_priv;

typedef struct arch_task_context {
    regs_t*         regs;
    uint32_t        ldts[4];    // 64 bits for each ldt entry
    void*           stack;
    uint8_t         ring;
} arch_task_context;

int tss_init(void);
int arch_task_context_init(arch_task_context* context, task_entry_t entry, task_priv priv);
void arch_task_context_release(arch_task_context* context);
int arch_task_restore_context(arch_task_context* context);
#endif
