/*
 * arch/i386/multiboot.c — minimal multiboot v1 info/module parser.
 *
 * GRUB passes the physical address of a multiboot_info structure in EBX on
 * entry (saved by arch/i386/boot.S into mboot_info).  User programs are
 * shipped as separate files and loaded by GRUB as "modules"; each module
 * entry in the info carries start/end physical addresses plus a NUL-
 * terminated cmdline string (the file path).  The kernel loads those ELFs
 * as user processes (kernel/init.c) instead of embedding them in its own
 * image.
 *
 * The info lives in low physical memory (<1 MB), which the kernel keeps
 * identity-mapped after arch_paging_init(), so the physical addresses can
 * be dereferenced directly.  All accesses go through volatile u32
 * pointers so the compiler cannot reorder or cache them.
 *
 * Only the module fields are parsed here; everything else in the info is
 * left untouched (kernel_start hard-codes 64 MB instead of using
 * mem_upper).
 */

#include "multiboot.h"
#include "kernel/errno.h"

/* Multiboot v1 info field offsets (u32 words), per the spec. */
#define MBI_FLAGS       0   /* bit 3 = modules present */
#define MBI_MODS_COUNT  5
#define MBI_MODS_ADDR   6

/* One GRUB module entry (multiboot v1) — u32 words. */
#define MOD_START       0
#define MOD_END         1
#define MOD_STRING      2

/*
 * multiboot_info.flags — NOTE: these bit numbers are the INFO struct's
 * own numbering (bit 0 = memory info), NOT the kernel multiboot header's
 * flags.  Module list present = bit 3 (0x08); bit 4 (0x10) is the a.out
 * symbol table, a classic off-by-one trap.
 */
#define MULTIBOOT_FLAG_MODS (1u << 3)

int mboot_module_count(void)
{
    volatile u32* m;

    if (!mboot_info)
        return 0;
    m = (volatile u32*)(uptr)mboot_info;
    if (!(m[MBI_FLAGS] & MULTIBOOT_FLAG_MODS))
        return 0;
    return (int)m[MBI_MODS_COUNT];
}

int mboot_module_get(int i, u8** start, u8** end)
{
    volatile u32* m;
    volatile u32* mods;

    if (!start || !end)
        return E_INVAL;
    if (!mboot_info)
        return E_NOTFOUND;
    m = (volatile u32*)(uptr)mboot_info;
    if (!(m[MBI_FLAGS] & MULTIBOOT_FLAG_MODS))
        return E_NOTFOUND;
    if (i < 0 || (u32)i >= m[MBI_MODS_COUNT])
        return E_NOTFOUND;

    mods = (volatile u32*)(uptr)m[MBI_MODS_ADDR];
    *start = (u8*)(uptr)mods[i * 4 + MOD_START];
    *end   = (u8*)(uptr)mods[i * 4 + MOD_END];
    return 0;
}

const char* mboot_module_name(int i)
{
    volatile u32* m;
    volatile u32* mods;

    if (!mboot_info)
        return 0;
    m = (volatile u32*)(uptr)mboot_info;
    if (!(m[MBI_FLAGS] & MULTIBOOT_FLAG_MODS))
        return 0;
    if (i < 0 || (u32)i >= m[MBI_MODS_COUNT])
        return 0;

    mods = (volatile u32*)(uptr)m[MBI_MODS_ADDR];
    if (!mods[i * 4 + MOD_STRING])
        return 0;
    return (const char*)(uptr)mods[i * 4 + MOD_STRING];
}

