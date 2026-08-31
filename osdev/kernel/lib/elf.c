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
#include "mm/paging.h"
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
                          npages * PAGE_SIZE, PTE_USER_PAGE) != 0) {
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

// int user_elf_demo(void)
// {
//     const u8* image = user_hello_elf_start;
//     u32 image_size = (u32)(user_hello_elf_end - user_hello_elf_start);
//     u32 entry = 0;
//     i32 pid;
//     pcb* proc;
//     int ret;

//     if (image_size < sizeof(elf32_ehdr)) {
//         LOG("user elf: embedded image too small (%u)", image_size);
//         return E_INVAL;
//     }

//     /*
//      * Validate the whole image FIRST (pure header/program-header parse,
//      * no allocation, no process created).  A malformed ELF therefore
//      * never spawns a process that would have to be torn down again.
//      *
//      * Note the ordering constraint: elf_load() maps into the target
//      * process's address space, so the process MUST exist before loading.
//      * The process is born TS_PENDING by default (proc_create) and is only
//      * unblocked after the segments are mapped — so the only failures
//      * left after this point are genuine resource errors (OOM / cap), and
//      * those are cleaned up by proc_exit(pid) below (p_exit frees every
//      * thread, the PCB, the address space and the capabilities — no
//      * zombie process is left behind).
//      */
//     ret = elf_validate((const elf32_ehdr*)image, image_size);
//     if (ret) {
//         LOG("user elf: validation failed (%d)", ret);
//         return ret;
//     }

//     /* Peek the entry point BEFORE creating the process so the main thread
//      * can be created with the right initial EIP (still suspended). */
//     entry = ((const elf32_ehdr*)image)->entry;
//     if (entry >= USER_SPACE_TOP) {
//         LOG("user elf: bad entry 0x%x", entry);
//         return E_INVAL;
//     }

//     pid = proc_create(PROC_PRIV_USER, (task_entry_t)(uptr)entry, 0);
//     if (pid < 0) {
//         LOG("user elf: proc_create failed (%d)", pid);
//         return pid;
//     }

//     proc = get_process_by_pid(pid);
//     if (!proc) {
//         proc_exit(pid);
//         return E_NOTFOUND;
//     }

//     ret = elf_load(proc, image, image_size, &entry);
//     if (ret) {
//         LOG("user elf: load failed (%d)", ret);
//         proc_exit(pid);
//         return ret;
//     }

//     LOG("user elf: loaded pid %d, entry=0x%x", pid, entry);

//     /* Segments are mapped and the main thread's EIP already points at the
//      * entry — let it run. */
//     proc_unblock(pid);
//     return 0;
// }
