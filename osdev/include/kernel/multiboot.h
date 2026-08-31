#ifndef KERNEL_MULTIBOOT_H
#define KERNEL_MULTIBOOT_H

#include "lib/types.h"

/*
 * Physical address of the multiboot_info that GRUB passes in EBX on entry,
 * saved by arch/i386/boot.S before anything clobbers the registers.  After
 * the kernel identity-maps low memory (arch_paging_init), physical ==
 * virtual, so the info and the module list it points to can be read
 * directly from C.
 */
extern u32 mboot_info;

/* Number of GRUB-loaded modules (multiboot_info.flags bit 4). */
int mboot_module_count(void);

/* Start/end of the i-th module.  Returns 0 on success, negative errno. */
int mboot_module_get(int i, u8** start, u8** end);

/* Filename/cmdline string GRUB attached to the i-th module (may be NULL or
 * empty).  Points into identity-mapped low memory. */
const char* mboot_module_name(int i);

#endif
