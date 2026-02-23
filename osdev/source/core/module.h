#ifndef __MODULE_H__
#define __MODULE_H__

typedef void (*init_call_t)(void);
#define module_init(fn) \
    static init_call_t __initcall_##fn##_used \
    __attribute__((__used__, section(".initcall"))) = (fn)

#endif