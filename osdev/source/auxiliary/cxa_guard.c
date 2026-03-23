#include "spinlock.h"
#include "module.h"
#include <stdint.h>

static spinlock* guard_spinlock;

void guard_init(void)
{
    guard_spinlock = spinlock_alloc();
}

module_init(guard_init);

int __cxa_guard_acquire(char* guard)
{
    uint8_t* guard_byte = (uint8_t*)guard;

    if (*guard_byte & 1)
        return 0;

    spinlock_lock(guard_spinlock);
    if (*guard_byte & 1) {
        spinlock_unlock(guard_spinlock);
        return 0;
    }

    if (*guard_byte & 2) {
        spinlock_unlock(guard_spinlock);
        return 0;
    }

    *guard_byte = 2;
    spinlock_unlock(guard_spinlock);

    return 1;
}

void __cxa_guard_release(char* guard)
{
    uint8_t* guard_byte = (uint8_t*)guard;

    spinlock_lock(guard_spinlock);
    *guard_byte = 1;
    spinlock_unlock(guard_spinlock);
}

void __cxa_guard_abort(char* guard)
{
    uint8_t* guard_byte = (uint8_t*)guard;

    spinlock_lock(guard_spinlock);
    *guard_byte = 0;
    spinlock_unlock(guard_spinlock);
}

#ifndef UNIT_TEST_BUILD
void* __dso_handle = (void*)&__dso_handle;

int __cxa_atexit(void (*destructor)(void*), void* arg, void* dso_handle)
{
    (void)destructor;
    (void)arg;
    (void)dso_handle;
    return 0;
}

void __cxa_finalize(void* dso_handle)
{
    (void)dso_handle;
}
#endif