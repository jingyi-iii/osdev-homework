#ifndef KERNEL_ERRNO_H
#define KERNEL_ERRNO_H

/**
 * @file errno.h
 * @brief Unified kernel error code definitions.
 *
 * All kernel subsystems return a negative errno value on failure,
 * or 0 / a positive value on success.  This file provides named
 * constants so that error paths are self-documenting.
 *
 * Error code layout (ranges):
 *   0                Success
 *   -1  ..  -9       General / Common errors
 *   -10 .. -19       Memory management errors
 *   -20 .. -29       Resource / Busy / Overflow errors
 *   -30 .. -39       Permission / Access errors
 *   -40 .. -49       Device / Driver errors
 *   -50 .. -59       IRQ errors
 *   -60 .. -69       Process / Thread errors
 *   -70 .. -79       Timeout / State errors
 *   -80 .. -89       I/O errors
 *   -90 .. -99       Not-found / Not-exist errors
 *   -100 .. -109     Internal / Fatal errors
 *   -110 .. -127     Reserved for future use
 */

/* ===================================================================
 *  Success
 * =================================================================== */
#define E_OK                0       /**< No error */

/* ===================================================================
 *  General / Common errors  (-1 .. -9)
 * =================================================================== */
#define E_NOMEM             -1      /**< Out of memory (alias for ENOMEM) */
#define E_INVAL             -2      /**< Invalid argument */
#define E_FAULT             -3      /**< Bad address / pointer */
#define E_NOSYS             -4      /**< Function not implemented */
#define E_BADF              -5      /**< Bad descriptor / handle */
#define E_2BIG              -6      /**< Argument list too long */
#define E_RANGE             -7      /**< Result out of range */
#define E_AGAIN             -8      /**< Resource temporarily unavailable, try again */
#define E_NODEV             -9      /**< No such device */

/* ===================================================================
 *  Memory management errors  (-10 .. -19)
 * =================================================================== */
#define E_NOMEM_ALLOC       -10     /**< Memory allocation failed (kmalloc returned NULL) */
#define E_NOMEM_FREE        -11     /**< Invalid free / double-free */
#define E_NOMEM_CORRUPT     -12     /**< Heap metadata corruption detected */
#define E_NOMEM_EXHAUSTED   -13     /**< System memory pool exhausted */
#define E_NOMEM_OVERFLOW    -14     /**< Allocation size overflow */

/* ===================================================================
 *  Resource / Busy / Overflow errors  (-20 .. -29)
 * =================================================================== */
#define E_BUSY              -20     /**< Device or resource busy */
#define E_EXISTS            -21     /**< Resource already exists */
#define E_NOSPC             -22     /**< No space left on device / resource full */
#define E_OVERFLOW          -23     /**< Value too large for defined data type */
#define E_LIMIT             -24     /**< Resource limit reached (e.g. max spinlocks) */
#define E_DEADLK            -25     /**< Resource deadlock avoided */

/* ===================================================================
 *  Permission / Access errors  (-30 .. -39)
 * =================================================================== */
#define E_PERM              -30     /**< Operation not permitted */
#define E_ACCES             -31     /**< Permission denied */
#define E_ROFS              -32     /**< Read-only resource */
#define E_PRIV              -33     /**< Insufficient privilege level */

/* ===================================================================
 *  Device / Driver errors  (-40 .. -49)
 * =================================================================== */
#define E_DEV_NOTREADY      -40     /**< Device not ready */
#define E_DEV_FAULT         -41     /**< Device hardware fault */
#define E_DEV_NOMEDIUM      -42     /**< No medium present */
#define E_DRV_NOTFOUND      -43     /**< No matching driver found */
#define E_DRV_PROBE         -44     /**< Driver probe failed */
#define E_DRV_BIND          -45     /**< Driver bind failed */
#define E_DRV_UNBIND        -46     /**< Driver unbind failed */

/* ===================================================================
 *  IRQ errors  (-50 .. -59)
 * =================================================================== */
#define E_IRQ_NOTAVAIL      -50     /**< IRQ line not available */
#define E_IRQ_INUSE         -51     /**< IRQ already in use */
#define E_IRQ_BADVECTOR     -52     /**< Invalid IRQ vector number */
#define E_IRQ_MASKED        -53     /**< IRQ is masked */
#define E_IRQ_HANDLER       -54     /**< IRQ handler error */

/* ===================================================================
 *  Process / Thread errors  (-60 .. -69)
 * =================================================================== */
#define E_PROC_NOTFOUND     -60     /**< Process not found */
#define E_THREAD_NOTFOUND   -61     /**< Thread not found */
#define E_THREAD_STATE      -62     /**< Thread in invalid state for operation */
#define E_THREAD_CREATE     -63     /**< Thread creation failed */
#define E_PROC_CREATE       -64     /**< Process creation failed */
#define E_PROC_ZOMBIE       -65     /**< Process is a zombie */
#define E_SCHED             -66     /**< Scheduler error */
#define E_CONTEXT           -67     /**< Context restore/switch error */

/* ===================================================================
 *  Timeout / State errors  (-70 .. -79)
 * =================================================================== */
#define E_TIMEDOUT          -70     /**< Operation timed out */
#define E_INPROGRESS        -71     /**< Operation already in progress */
#define E_ALREADY           -72     /**< Operation already completed */
#define E_CANCELED          -73     /**< Operation canceled */
#define E_IDLE              -74     /**< Subsystem is idle / not initialized */
#define E_NOTREADY          -75     /**< Subsystem not ready */

/* ===================================================================
 *  I/O errors  (-80 .. -89)
 * =================================================================== */
#define E_IO                -80     /**< Generic I/O error */
#define E_IO_PORT           -81     /**< Port I/O error */
#define E_IO_READ           -82     /**< Read error */
#define E_IO_WRITE          -83     /**< Write error */
#define E_IO_EOF            -84     /**< End of file / stream */
#define E_PROTO             -85     /**< Protocol error */
#define E_PROTONOSUPPORT    -86     /**< Protocol not supported */

/* ===================================================================
 *  Not-found / Not-exist errors  (-90 .. -99)
 * =================================================================== */
#define E_NOTFOUND          -90     /**< Generic not found */
#define E_NOENT             -91     /**< No such file or directory */
#define E_NODATA            -92     /**< No data available */
#define E_EMPTY             -93     /**< Collection / buffer is empty */

/* ===================================================================
 *  Internal / Fatal errors  (-100 .. -109)
 * =================================================================== */
#define E_INTERNAL          -100    /**< Internal kernel error */
#define E_PANIC             -101    /**< Unrecoverable error — kernel panic */
#define E_NOTSUP            -102    /**< Operation not supported */
#define E_BADARCH           -103    /**< Architecture mismatch */
#define E_ASSERT            -104    /**< Assertion failed */

/* ===================================================================
 *  Compatibility aliases (Linux-style names)
 *  Kept for gradual migration from raw -1, -22, etc.
 * =================================================================== */
#define ENOMEM              E_NOMEM
#define EINVAL              E_INVAL
#define EFAULT              E_FAULT
#define ENOSYS              E_NOSYS
#define EBADF               E_BADF
#define E2BIG               E_2BIG
#define ERANGE              E_RANGE
#define EAGAIN              E_AGAIN
#define ENODEV              E_NODEV
#define EBUSY               E_BUSY
#define EEXIST              E_EXISTS
#define ENOSPC              E_NOSPC
#define EOVERFLOW           E_OVERFLOW
#define EPERM               E_PERM
#define EACCES              E_ACCES
#define EROFS               E_ROFS
#define EDEADLK             E_DEADLK
#define ETIMEDOUT           E_TIMEDOUT
#define EINPROGRESS         E_INPROGRESS
#define EALREADY            E_ALREADY
#define ECANCELED           E_CANCELED
#define EIO                 E_IO
#define EPROTO              E_PROTO
#define EPROTONOSUPPORT     E_PROTONOSUPPORT
#define ENOENT              E_NOENT
#define ENODATA             E_NODATA
#define ENOTSUP             E_NOTSUP

/* ===================================================================
 *  Helper macros
 * =================================================================== */

/** Evaluates to 1 if `err` indicates an error (negative), 0 otherwise. */
#define IS_ERR(err)         ((err) < 0)

/** True if the error code can be retried (e.g. E_AGAIN, E_BUSY). */
#define IS_ERR_RETRYABLE(err) \
    ((err) == E_AGAIN || (err) == E_BUSY || (err) == E_INPROGRESS)

/** True if the error indicates a fatal / unrecoverable condition. */
#define IS_ERR_FATAL(err) \
    ((err) == E_PANIC || (err) == E_INTERNAL)

/**
 * @brief Convert an errno value to a human-readable string.
 *
 * @param err  Negative error code (e.g. E_INVAL).
 * @return     Static string literal describing the error.
 */
static inline const char *strerror(int err)
{
    switch (err) {
    case E_OK:              return "Success";
    /* General */
    case E_NOMEM:           return "Out of memory";
    case E_INVAL:           return "Invalid argument";
    case E_FAULT:           return "Bad address";
    case E_NOSYS:           return "Function not implemented";
    case E_BADF:            return "Bad descriptor";
    case E_2BIG:            return "Argument list too long";
    case E_RANGE:           return "Result out of range";
    case E_AGAIN:           return "Try again";
    case E_NODEV:           return "No such device";
    /* Memory */
    case E_NOMEM_ALLOC:     return "Memory allocation failed";
    case E_NOMEM_FREE:      return "Invalid free";
    case E_NOMEM_CORRUPT:   return "Heap corruption";
    case E_NOMEM_EXHAUSTED: return "Memory pool exhausted";
    case E_NOMEM_OVERFLOW:  return "Allocation size overflow";
    /* Resource */
    case E_BUSY:            return "Resource busy";
    case E_EXISTS:          return "Resource already exists";
    case E_NOSPC:           return "No space left";
    case E_OVERFLOW:        return "Value overflow";
    case E_LIMIT:           return "Resource limit reached";
    case E_DEADLK:          return "Deadlock avoided";
    /* Permission */
    case E_PERM:            return "Operation not permitted";
    case E_ACCES:           return "Permission denied";
    case E_ROFS:            return "Read-only resource";
    case E_PRIV:            return "Insufficient privilege";
    /* Device / Driver */
    case E_DEV_NOTREADY:    return "Device not ready";
    case E_DEV_FAULT:       return "Device hardware fault";
    case E_DEV_NOMEDIUM:    return "No medium";
    case E_DRV_NOTFOUND:    return "No matching driver";
    case E_DRV_PROBE:       return "Driver probe failed";
    case E_DRV_BIND:        return "Driver bind failed";
    case E_DRV_UNBIND:      return "Driver unbind failed";
    /* IRQ */
    case E_IRQ_NOTAVAIL:    return "IRQ not available";
    case E_IRQ_INUSE:       return "IRQ in use";
    case E_IRQ_BADVECTOR:   return "Invalid IRQ vector";
    case E_IRQ_MASKED:      return "IRQ is masked";
    case E_IRQ_HANDLER:     return "IRQ handler error";
    /* Process / Thread */
    case E_PROC_NOTFOUND:   return "Process not found";
    case E_THREAD_NOTFOUND: return "Thread not found";
    case E_THREAD_STATE:    return "Invalid thread state";
    case E_THREAD_CREATE:   return "Thread creation failed";
    case E_PROC_CREATE:     return "Process creation failed";
    case E_PROC_ZOMBIE:     return "Process is zombie";
    case E_SCHED:           return "Scheduler error";
    case E_CONTEXT:         return "Context switch error";
    /* Timeout / State */
    case E_TIMEDOUT:        return "Operation timed out";
    case E_INPROGRESS:      return "Operation in progress";
    case E_ALREADY:         return "Already completed";
    case E_CANCELED:        return "Operation canceled";
    case E_IDLE:            return "Subsystem idle";
    case E_NOTREADY:        return "Subsystem not ready";
    /* I/O */
    case E_IO:              return "I/O error";
    case E_IO_PORT:         return "Port I/O error";
    case E_IO_READ:         return "Read error";
    case E_IO_WRITE:        return "Write error";
    case E_IO_EOF:          return "End of file";
    case E_PROTO:           return "Protocol error";
    case E_PROTONOSUPPORT:  return "Protocol not supported";
    /* Not found */
    case E_NOTFOUND:        return "Not found";
    case E_NOENT:           return "No such entry";
    case E_NODATA:          return "No data available";
    case E_EMPTY:           return "Empty";
    /* Internal */
    case E_INTERNAL:        return "Internal error";
    case E_PANIC:           return "Kernel panic";
    case E_NOTSUP:          return "Not supported";
    case E_BADARCH:         return "Architecture mismatch";
    case E_ASSERT:          return "Assertion failed";
    default:                return "Unknown error";
    }
}

#endif /* KERNEL_ERRNO_H */
