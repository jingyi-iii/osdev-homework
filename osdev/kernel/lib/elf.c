/*
 * kernel/elf.c — minimal ELF32 executable loader.
 *
 * Loads a separately-linked user program (see user/) into a user
 * process's address space:
 *   - parses the ELF header and program headers,
 *   - allocates physical pages per PT_LOAD segment, copies the file
 *     content in and zero-fills the bss tail,
 *   - grants CAP_MAP_MEM for each segment and maps it at its linked
 *     virtual address via vmm_map_fixed().
 *
 * The caller creates the target process (born TS_PENDING by default),
 * loads the ELF, then unblocks it.  Segments live at high user addresses
 * (linked at USER_PROGRAM_BASE in user/user.ld), far above the kernel's
 * low identity map.
 */

#include "lib/elf.h"
#include "kernel/errno.h"
#include "lib/string.h"
#include "mm/pmm.h"
#include "paging.h"
#include "mm/vmm.h"
#include "kernel/capability.h"
#include "kernel/log.h"

int elf_validate(const elf32_ehdr* h, u32 image_size)
{
    if (image_size < sizeof(elf32_ehdr))
        return E_INVAL;
    if (h->ident[0] != 0x7F || h->ident[1] != 'E' ||
        h->ident[2] != 'L'  || h->ident[3] != 'F')
        return E_INVAL;
    if (h->ident[4] != 1 /* ELFCLASS32 */ ||
        h->ident[5] != 1 /* ELFDATA2LSB */)
        return E_INVAL;
    if (h->type != ELF_ET_EXEC || h->machine != ELF_EM_386)
        return E_INVAL;
    if (h->phentsize != sizeof(elf32_phdr))
        return E_INVAL;
    if (h->phnum == 0 || h->phnum > 64)
        return E_INVAL;
    if (h->phoff + (u32)h->phnum * sizeof(elf32_phdr) > image_size)
        return E_INVAL;
    if (h->entry >= USER_SPACE_TOP)
        return E_INVAL;
    return 0;
}

int elf_load(pcb* proc, const void* image, u32 image_size, u32* out_entry)
{
    const elf32_ehdr* h = (const elf32_ehdr*)image;
    const elf32_phdr* ph;
    int loaded = 0;

    if (!proc || !out_entry)
        return E_INVAL;
    if (image_size < sizeof(elf32_ehdr))
        return E_INVAL;

    ph = (const elf32_phdr*)((const u8*)image + h->phoff);
    for (u32 i = 0; i < h->phnum; i++) {
        u32 filesz, memsz, vaddr, page_base, offset, npages, pa;
        cap_mem cmem;

        if (ph[i].type != ELF_PT_LOAD)
            continue;

        filesz = ph[i].filesz;
        memsz  = ph[i].memsz;
        vaddr  = ph[i].vaddr;
        if (memsz == 0)
            continue;
        if (memsz < filesz)
            return E_INVAL;
        if (vaddr + memsz < vaddr || vaddr + memsz > USER_SPACE_TOP)
            return E_INVAL;
        if (ph[i].offset + filesz > image_size)
            return E_INVAL;

        page_base = vaddr & ~(PAGE_SIZE - 1);
        offset    = vaddr - page_base;
        npages    = (memsz + offset + PAGE_SIZE - 1) / PAGE_SIZE;

        pa = pmm_alloc_pages(npages);
        if (!pa)
            return E_NOMEM;

        /* The kernel is identity-mapped, so the physical pages are
         * directly accessible at `pa` while the image is copied in. */
        memcpy((void*)(pa + offset), (const u8*)image + ph[i].offset, filesz);
        memset((void*)(pa + offset + filesz), 0, memsz - filesz);

        cmem.base  = pa;
        cmem.size  = npages * PAGE_SIZE;
        cmem.flags = PTE_USER_PAGE;
        if (cap_grant(proc, CAP_MAP_MEM, &cmem) != 0) {
            pmm_free_pages(pa, npages);
            return E_PERM;
        }

        if (vmm_map_fixed(proc, pa, (void*)page_base,
                          npages * PAGE_SIZE, PTE_USER_PAGE, 1) != 0) {
            cap_revoke(proc, CAP_MAP_MEM, &cmem);
            pmm_free_pages(pa, npages);
            return E_LIMIT;
        }

        loaded = 1;
    }

    if (!loaded)
        return E_INVAL;

    *out_entry = h->entry;
    return 0;
}
