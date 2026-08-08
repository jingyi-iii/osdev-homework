#ifndef SHM_H
#define SHM_H

#include "kernel/process.h"
#include "mm/vmm.h"

int shm_share(int32_t pid, void* va, size_t size, void** out_va);
int shm_unshare(int32_t pid, void* va);

#endif
