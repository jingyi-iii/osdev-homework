#ifndef __ARCH_TASK_H
#define __ARCH_TASK_H

#include "arch_protm.h"
#include "mm/heap.h"
#include "mm/vmm.h"

typedef struct regs {
    u32 gs;
    u32 fs;
    u32 es;
    u32 ds;
    u32 edi;
    u32 esi;
    u32 ebp;
    u32 kesp;
    u32 ebx;
    u32 edx;
    u32 ecx;
    u32 eax;
    u32 eip;
    u32 cs;
    u32 eflags;
    u32 esp;
    u32 ss;
} __attribute__((packed)) regs;

typedef struct tss {
    u32 backlink;
    u32 esp0;
    u32 ss0;
    u32 esp1;
    u32 ss1;
    u32 esp2;
    u32 ss2;
    u32 cr3;
    u32 eip;
    u32 flags;
    u32 eax;
    u32 ecx;
    u32 edx;
    u32 ebx;
    u32 esp;
    u32 ebp;
    u32 esi;
    u32 edi;
    u32 es;
    u32 cs;
    u32 ss;
    u32 ds;
    u32 fs;
    u32 gs;
    u32 ldt;
    u16 trap;
    u16 iobase;
} __attribute__((packed)) tss;

typedef void (*task_entry_t)(void);

typedef enum task_priv {
    TASK_PRIV_KERNEL = 0,
    TASK_PRIV_USER,
} task_priv;

typedef struct arch_task_context {
    regs*         regs;
    u32        ldts[4];    // 64 bits for each ldt entry
    void*           stack;
    u8         ring;
} arch_task_context;

int tss_init(void);
int arch_task_context_init(vmm_control_block* vcb, arch_task_context* context, task_entry_t entry, task_priv priv);
void arch_task_context_release(vmm_control_block* vcb, arch_task_context* context);
int arch_task_restore_context(arch_task_context* context);
#endif
