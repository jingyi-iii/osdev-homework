#ifndef __MODULE_H__
#define __MODULE_H__

typedef void (*init_call_t)(void);
typedef void (*exit_call_t)(void);

#define module_init(fn) \
    static init_call_t __initcall_##fn##_used \
    __attribute__((__used__, section(".initcall"))) = (fn)

#define module_exit(fn) \
    static exit_call_t __exitcall_##fn##_used \
    __attribute__((__used__, section(".exitcall"))) = (fn)

#endif