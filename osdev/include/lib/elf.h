#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include "lib/types.h"
#include "lib/compiler.h"
#include "kernel/process.h"

#define ELF_ET_EXEC   2    /* e_type: executable */
#define ELF_EM_386    3    /* e_machine: Intel 80386 */
#define ELF_PT_LOAD   1    /* p_type: loadable segment */

typedef struct elf32_ehdr {
    u8  ident[16];
    u16 type;
    u16 machine;
    u32 version;
    u32 entry;
    u32 phoff;
    u32 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} ATTR_PACKED elf32_ehdr;

typedef struct elf32_phdr {
    u32 type;
    u32 offset;
    u32 vaddr;
    u32 paddr;
    u32 filesz;
    u32 memsz;
    u32 flags;
    u32 align;
} ATTR_PACKED elf32_phdr;

int elf_validate(const elf32_ehdr* h, u32 image_size);

/*
 * elf_load — validate an ELF32 executable image and map its PT_LOAD
 * segments into @proc's address space.
 *
 * For each loadable segment:
 *   - physical pages are allocated (pmm) and filled from the file image,
 *     with the bss tail (memsz - filesz) zeroed;
 *   - a CAP_MAP_MEM capability covering the segment is granted to @proc;
 *   - vmm_map_fixed() maps the pages at the segment's linked virtual
 *     address (user-accessible, PTE_USER_PAGE).
 *
 * @proc        target process (created suspended by the caller)
 * @image       kernel VA of the ELF file image
 * @image_size  size of the image in bytes
 * @out_entry   receives the entry point (user VA)
 *
 * Returns 0 on success or a negative errno.
 */
int elf_load(pcb* proc, const void* image, u32 image_size, u32* out_entry);

/*
 * user_elf_demo — load and run the embedded hello ELF (user/hello.elf)
 * in a freshly created user process.  Called once from init_thread().
 */
int user_elf_demo(void);

#endif
