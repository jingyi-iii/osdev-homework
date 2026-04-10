#ifndef __ARCH_PROCESS_H
#define __ARCH_PROCESS_H

#include "arch_protm.h"
#include "heap.h"

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

typedef void (*proc_entry_t)(void);

typedef enum proc_priv {
    PROC_PRIV_KERNEL = 0,
    PROC_PRIV_USER,
} proc_priv;

typedef struct proc_context {
    regs_t*         regs;
    uint32_t        ldts[4];    // 64 bits for each ldt entry
    void*           stack;
    uint8_t         ring;
} proc_context;

int tss_init(void);
int proc_context_init(proc_context* context, proc_entry_t entry, proc_priv priv);
int proc_restore_context(proc_context* context);
#endif
