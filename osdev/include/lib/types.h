#ifndef __TYPES_H__
#define __TYPES_H__

/*
 * Project-wide fixed-width integer type convention.
 *
 * Prefer u8/u16/u32/u64 (and i8/i16/i32/i64) over the C99
 * uint8_t/uint16_t/uint32_t/uint64_t spellings; pointer-sized types are
 * uptr/iptr.  These are plain typedefs with no _t suffix (the suffix is
 * reserved for types coming from libc).
 */

typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

typedef signed char         i8;
typedef signed short        i16;
typedef signed int          i32;
typedef signed long long    i64;

typedef unsigned long       uptr;
typedef signed long         iptr;

#endif
