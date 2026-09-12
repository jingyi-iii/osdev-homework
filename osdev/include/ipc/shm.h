#ifndef SHM_H
#define SHM_H

#include "kernel/process.h"
#include "mm/vmm.h"

int shm_share(i32 pid, void* va, size_t size, void** out_va);
int shm_unshare(i32 pid, void* va);

#endif
