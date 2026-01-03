#ifndef __PROCESS_H_
#define __PROCESS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PROCESS     (32)
#define MAX_NAME_LEN    (16)

enum proc_run_state {
    PS_NULL = 0,
    PS_READY_TO_RUN,
    PS_PENDING,
};

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
typedef struct process {
    regs_t*         regs;
    uint64_t        ldts[2];
    void*           stack;
    proc_entry_t    entry;
    int32_t         pid;
    struct process  *next;
} process_t;

void process_evn_setup(void);
int32_t create_proc(uint8_t ring, proc_entry_t entry);
int ldt_init(void);

#ifdef __cplusplus
}
#endif
#endif
