/*
 * arch/i386/mem.c — x86 page-table-backed user-range validation.
 *
 * The page directory/table layout (PDE/PTE, PD_INDEX/PT_INDEX/PAGE_OFFSET)
 * is x86-specific, so the walk lives here; kernel code only calls
 * arch_validate_user_range() (see arch_mem.h).
 */

#include "arch_mem.h"
#include "mm/paging.h"     /* PAGE_MASK, PAGE_SIZE, PTE_*, PD_INDEX, PT_INDEX, PAGE_OFFSET, USER_SPACE_TOP */
#include "arch_irq.h"      /* arch_get_cr3 */

int arch_validate_user_range(const void* user_addr, size_t n, int for_write)
{
    u32 addr = (u32)user_addr;
    u32 cr3;

    if (n == 0)
        return 1;
    if (addr + n < addr)              /* 32-bit overflow */
        return 0;
    if (addr >= USER_SPACE_TOP || addr + n > USER_SPACE_TOP)
        return 0;                     /* outside user space */

    cr3 = arch_get_cr3() & PAGE_MASK;

    while (n > 0) {
        u32 pde = *(volatile u32*)(cr3 + PD_INDEX(addr) * 4);
        if (!(pde & PTE_PRESENT) || !(pde & PTE_USER) ||
            (for_write && !(pde & PTE_RW)))
            return 0;

        if (pde & 0x80) {
            /* 4MB page (PS bit): the whole region shares the PDE's
             * permissions, there is no page table to walk. */
            if (!(pde & PTE_USER) || (for_write && !(pde & PTE_RW)))
                return 0;
        } else {
            u32 pt_base = pde & PAGE_MASK;
            u32 pte = *(volatile u32*)(pt_base + PT_INDEX(addr) * 4);
            if (!(pte & PTE_PRESENT) || !(pte & PTE_USER) ||
                (for_write && !(pte & PTE_RW)))
                return 0;
        }

        size_t remain = PAGE_SIZE - PAGE_OFFSET(addr);
        if (remain > n)
            remain = n;
        addr += remain;
        n -= remain;
    }
    return 1;
}
