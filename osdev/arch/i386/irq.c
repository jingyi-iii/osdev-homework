#include "regs.h"
#include "arch_protm.h"
#include "arch_irq.h"
#include "sync/spinlock.h"
#include "kernel/errno.h"

#define INT_MASTER_CMD          (0x20)
#define INT_MASTER_DATA         (0x21)
#define INT_SLAVE_CMD           (0xA0)
#define INT_SLAVE_DATA          (0xA1)
#define	INT_VECTOR_IRQ0         (0x20)
#define	INT_VECTOR_IRQ8         (0x28)

#define IDT_GATE_TASK32         (0x85)
#define IDT_GATE_INT16          (0x86)
#define IDT_GATE_TRAP16         (0x87)
#define IDT_GATE_INT32          (0x8E)
#define IDT_GATE_TRAP32         (0x8F)
#define IDT_GATE_SYSCALL32      (0xEE)

#define NUM_EXCEPTIONS          (32)
#define NUM_INTERRUPTS          (16)

i32 irq_reenter_cnt = -1;
static idtmeta g_idtmeta = { 0 };
static ATTR_ALIGINED(idesc) idesc idt[IDT_ENTRIES] = { 0 };
void arch_isr_tbl(void);
void arch_syscall_entry(void);
static void hlt_handler(void) { for (;;) __asm__ volatile ("hlt"); }

static idesc gen_idesc(u32 isr, u16 sel_code, u8 flags)
{
    idesc desc = { 0 }; 

    desc.isr_low  = (u32)isr & 0xffff;
    desc.sel_code = sel_code;
    desc.attrs    = flags;
    desc.isr_high = (u32)isr >> 16;
    desc.reserved = 0;

    return desc;
}

static void arch_init_8259a(void)
{
    arch_outb(INT_MASTER_CMD,  0x11);
    arch_outb(INT_SLAVE_CMD,   0x11);
    arch_outb(INT_MASTER_DATA, INT_VECTOR_IRQ0);
    arch_outb(INT_SLAVE_DATA,  INT_VECTOR_IRQ8);
    arch_outb(INT_MASTER_DATA, 0x04);
    arch_outb(INT_SLAVE_DATA,  0x02);
    arch_outb(INT_MASTER_DATA, 0x01);
    arch_outb(INT_SLAVE_DATA,  0x01);
    arch_outb(INT_MASTER_DATA, 0xff);
    arch_outb(INT_SLAVE_DATA,  0xff);
}

void arch_unmask_irq(u16 irq_nr)
{
    unsigned char mask = 0;
    u8 port_val = 0;

    if (irq_nr < ARCH_IRQ_BEGIN || irq_nr > ARCH_IRQ_END)
        return;

    if (irq_nr >= RL_TIMER_IRQ_NO) {
        mask = ~((unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ8)));
        port_val = arch_inb(0xa1) & mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = ~((unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ0)));
        port_val = arch_inb(0x21) & mask;
        arch_outb(0x21, port_val);
    }
}

void arch_mask_irq(u16 irq_nr)
{
    unsigned char mask = 0;
    u8 port_val = 0;

    if (irq_nr < ARCH_IRQ_BEGIN || irq_nr > ARCH_IRQ_END)
        return;

    if (irq_nr >= RL_TIMER_IRQ_NO) {
        mask = (unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ8));
        port_val = arch_inb(0xa1) | mask;
        arch_outb(0xa1, port_val);
    } else {
        mask = (unsigned char)(1 << (irq_nr - INT_VECTOR_IRQ0));
        port_val = arch_inb(0x21) | mask;
        arch_outb(0x21, port_val);
    }
}

void arch_init_irq(void)
{
    int i = 0;

    arch_cli();
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt[i] = gen_idesc((u32)hlt_handler,
                           arch_get_sel(SYS_CODE),
                           IDT_GATE_INT32);
    }
    for (i = 0; i < NUM_EXCEPTIONS + NUM_INTERRUPTS; i++) {
        idt[i] = gen_idesc((u32)arch_isr_tbl + 256 * i,
                           arch_get_sel(SYS_CODE),
                           IDT_GATE_INT32);
    }

    // syscall
    idt[100] = gen_idesc((u32)arch_syscall_entry,
                           arch_get_sel(SYS_CODE),
                           IDT_GATE_SYSCALL32);

    g_idtmeta.limit = sizeof(idesc) * IDT_ENTRIES - 1;
    g_idtmeta.base = (u32)idt;
    arch_reload_idt(&g_idtmeta);
    arch_init_8259a();
}

/*
 * Trap into the syscall gate: ebx = handle, ecx = arg, edx = size.
 * arch_syscall_entry (irq.S) dispatches to syscall_dispatch(handle, arg,
 * size); its RESTORE_REGS_KEEP_EAX restore leaves the return value in EAX,
 * so after int $100 returns, EAX (captured here via "=a") holds the syscall
 * return value.
 */
int arch_syscall(u32 handle, void* data, size_t data_size)
{
    int ret;

    /*
     * Reentrancy guard: issuing int $100 while already inside a syscall
     * or an IRQ handler (irq_reenter_cnt == 0) hits arch_syscall_entry's
     * reenter skip — it only dispatches when the count is 0 after the
     * increment — and returns with EAX untouched, so the caller would
     * silently get a stale garbage value.  Refuse loudly instead so the
     * driver can retry after the outer syscall/ISR completes.
     * (irq_reenter_cnt: -1 = idle, 0 = inside a handler.)
     */
    if (irq_reenter_cnt != -1)
        return E_AGAIN;

    /*
     * Bind the arguments to the exact registers the gate expects
     * (ebx = handle, ecx = arg, edx = size) with the register-specific
     * constraints "b"/"c"/"d", and take the return value from EAX with
     * "=a".  Generic "g" constraints on stack arguments get miscompiled
     * here: GCC computes the memory operand against the wrong frame slot,
     * so the kernel ends up receiving garbage handle/arg/size (observed:
     * EBX = leftover EAX, ECX = handle, EDX = arg).  Register constraints
     * eliminate that ambiguity entirely.
     */
    __asm__ __volatile__(
            "int  $100              \n\t"
            : "=a"(ret)
            : "b"(handle), "c"(data), "d"(data_size)
            : "memory"
    );

    return ret;
}

/*
 * ============================================================================
 * Exception Handler — emergency register dump / stack trace via VGA + serial
 * No kernel services are used; all output goes directly to hardware.
 * ============================================================================
 */

/* Stack frame built by DECLARE_EXCEPTION_NOERR / DECLARE_EXCEPTION_ERR */
typedef struct {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp_v;
    u32 ebx, edx, ecx, eax;
    u32 error_code;
    u32 eip;
    u32 cs;
    u32 eflags;
    u32 user_esp;
    u32 user_ss;
} exception_frame;

static const char *exception_names[32] = {
    [0x00] = "#DE  Divide Error",
    [0x01] = "#DB  Debug",
    [0x02] = "NMI  Non-Maskable Interrupt",
    [0x03] = "#BP  Breakpoint",
    [0x04] = "#OF  Overflow",
    [0x05] = "#BR  BOUND Range Exceeded",
    [0x06] = "#UD  Invalid Opcode",
    [0x07] = "#NM  Device Not Available",
    [0x08] = "#DF  Double Fault",
    [0x09] = "     Coprocessor Segment Overrun",
    [0x0A] = "#TS  Invalid TSS",
    [0x0B] = "#NP  Segment Not Present",
    [0x0C] = "#SS  Stack Segment Fault",
    [0x0D] = "#GP  General Protection Fault",
    [0x0E] = "#PF  Page Fault",
    [0x0F] = "     (Intel reserved)",
    [0x10] = "#MF  x87 FPU Error",
    [0x11] = "#AC  Alignment Check",
    [0x12] = "#MC  Machine Check",
    [0x13] = "#XM  SIMD Floating-Point",
    [0x14] = "#VE  Virtualization",
    [0x1E] = "#SX  Security Exception",
};

/* ---- emergency output: VGA text-mode (0xB8000) ---- */
static void vga_emergency_putc(char c)
{
    static u16 *vga = (u16 *)0xB8000;
    static int pos;
    static const u8 COLOR = 0x4F;  /* white on red */

    switch (c) {
    case '\n':
        pos = (pos + 80) / 80 * 80;
        break;
    case '\r':
        pos = pos / 80 * 80;
        break;
    default:
        vga[pos] = (u16)(c | (COLOR << 8));
        pos++;
        break;
    }
    if (pos >= 80 * 25) {
        for (int i = 0; i < 80 * 24; i++)
            vga[i] = vga[i + 80];
        for (int i = 80 * 24; i < 80 * 25; i++)
            vga[i] = (u16)(' ' | (COLOR << 8));
        pos = 80 * 24;
    }
}

/* ---- emergency output: serial COM1 (0x3F8) ---- */
static void serial_emergency_putc(char c)
{
    while ((arch_inb(0x3FD) & 0x20) == 0)
        __asm__ volatile ("pause");
    arch_outb(0x3F8, (u8)c);
    if (c == '\n')
        serial_emergency_putc('\r');
}

/* ---- unified emergency output ---- */
static void emergency_puts(const char *s)
{
    while (*s) {
        vga_emergency_putc(*s);
        serial_emergency_putc(*s);
        s++;
    }
}

static void emergency_print_hex(u32 val)
{
    char buf[] = "00000000";
    for (int i = 7; i >= 0; i--) {
        u8 nibble = val & 0xF;
        buf[i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
        val >>= 4;
    }
    emergency_puts(buf);
}

static void emergency_print_dec(u32 val)
{
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (val == 0) buf[--i] = '0';
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    emergency_puts(&buf[i]);
}

/* ---- stack back-trace (EBP frame-pointer chain) ---- */
/*
 * Requires -fno-omit-frame-pointer (added to CFLAGS in makefile).
 *
 * Outputs a machine-parseable section (for 'make dump') followed by a
 * human-readable trace.  Offline resolution:
 *
 *   make dump                    — resolve EIPs from the last crash
 *   make dump LOG=serial.log     — resolve EIPs from a specific log
 */
static void emergency_stack_trace(u32 ebp_val, u32 crash_eip)
{
    extern u32 __kernel_start[];
    extern u32 __kernel_end[];
    const u32 kstart = (u32)__kernel_start;
    const u32 kend   = (u32)__kernel_end;

    u32 eips[16];
    int eip_count = 0;
    u32 *ebp = (u32 *)ebp_val;
    int depth;

    /* ---- pass 1: collect return EIPs ---- */
    for (depth = 0; depth < 16; depth++) {
        if (!ebp)
            break;

        u32 next_ebp = ebp[0];
        u32 ret_eip  = ebp[1];

        if (ret_eip < kstart || ret_eip >= kend)
            break;

        eips[eip_count++] = ret_eip;

        if (next_ebp == 0 || next_ebp < (u32)ebp || next_ebp >= kend)
            break;
        ebp = (u32 *)next_ebp;
    }

    /* ---- pass 2: machine-parseable block ---- */
    emergency_puts("\n--- STACK_TRACE_BEGIN ---\n");
    /* crash site: the exact instruction that faulted */
    emergency_puts("CRASH:");
    emergency_print_hex(crash_eip);
    emergency_puts("\n");
    /* call chain: frame 0 is where the fault happened, then callers */
    for (int i = 0; i < eip_count; i++) {
        emergency_puts("EIP:");
        emergency_print_hex(eips[i]);
        emergency_puts("\n");
    }
    emergency_puts("--- STACK_TRACE_END ---\n");

    /* ---- pass 3: human-readable trace ---- */
    emergency_puts("\n--- Stack Trace ---\n");
    /* crash site */
    emergency_puts("  CRASH at EIP=");
    emergency_print_hex(crash_eip);
    emergency_puts("\n");
    if (eip_count == 0) {
        emergency_puts("  (no call chain — -fno-omit-frame-pointer?)\n");
        return;
    }
    /* re-walk to also print EBP */
    ebp = (u32 *)ebp_val;
    for (int i = 0; i < eip_count; i++) {
        emergency_puts("  #");
        emergency_print_dec(i);
        emergency_puts("  EBP=");
        emergency_print_hex((u32)ebp);
        emergency_puts("  ret-to EIP=");
        emergency_print_hex(eips[i]);
        emergency_puts("\n");
        ebp = (u32 *)ebp[0];
    }
    emergency_puts("  (use 'make dump' offline to resolve EIP → function)\n");
}

/*
 * exception_handler — called from DECLARE_EXCEPTION_* macros in irq.S.
 * This function must never return; it halts the system after dumping state.
 */
void exception_handler(int exception_number, exception_frame *frame)
{
    emergency_puts("\n\n========================================\n");
    emergency_puts("         KERNEL EXCEPTION\n");
    emergency_puts("========================================\n");

    const char *name = (exception_number < 32 && exception_names[exception_number])
                       ? exception_names[exception_number]
                       : "(unknown exception)";
    emergency_puts("Exception: ");
    emergency_print_dec(exception_number);
    emergency_puts("  ");
    emergency_puts(name);
    emergency_puts("\n");

    /* error code */
    emergency_puts("Error Code: ");
    emergency_print_hex(frame->error_code);
    emergency_puts("\n");

    /* #PF extras */
    if (exception_number == 0x0E) {
        u32 cr2;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        emergency_puts("CR2 (fault address): ");
        emergency_print_hex(cr2);
        emergency_puts("\n");

        emergency_puts("Page fault type: ");
        if (frame->error_code & 0x1)  emergency_puts("protection-violation ");
        else                          emergency_puts("non-present ");
        if (frame->error_code & 0x2)  emergency_puts("write ");
        else                          emergency_puts("read ");
        if (frame->error_code & 0x4)  emergency_puts("user-mode ");
        else                          emergency_puts("kernel-mode ");
        if (frame->error_code & 0x8)  emergency_puts("reserved-bit ");
        if (frame->error_code & 0x10) emergency_puts("instruction-fetch ");
        emergency_puts("\n");
    }

    /* register dump */
    emergency_puts("\n--- Register Dump ---\n");
    emergency_puts("EAX="); emergency_print_hex(frame->eax);
    emergency_puts(" EBX="); emergency_print_hex(frame->ebx);
    emergency_puts(" ECX="); emergency_print_hex(frame->ecx);
    emergency_puts(" EDX="); emergency_print_hex(frame->edx);
    emergency_puts("\n");
    emergency_puts("ESI="); emergency_print_hex(frame->esi);
    emergency_puts(" EDI="); emergency_print_hex(frame->edi);
    emergency_puts(" EBP="); emergency_print_hex(frame->ebp);
    emergency_puts(" ESP="); emergency_print_hex(frame->esp_v);
    emergency_puts("\n");
    emergency_puts(" EIP="); emergency_print_hex(frame->eip);
    emergency_puts("  CS="); emergency_print_hex(frame->cs);
    emergency_puts(" EFLAGS="); emergency_print_hex(frame->eflags);
    emergency_puts("\n");
    emergency_puts(" DS="); emergency_print_hex(frame->ds);
    emergency_puts("  ES="); emergency_print_hex(frame->es);
    emergency_puts("  FS="); emergency_print_hex(frame->fs);
    emergency_puts("  GS="); emergency_print_hex(frame->gs);
    emergency_puts("\n");

    if (frame->cs & 0x3) {
        emergency_puts("User ESP="); emergency_print_hex(frame->user_esp);
        emergency_puts(" User SS="); emergency_print_hex(frame->user_ss);
        emergency_puts("\n");
    }

    /* stack trace */
    emergency_puts("\n--- Stack Trace ---\n");
    emergency_stack_trace(frame->ebp, frame->eip);

    emergency_puts("\n========================================\n");
    emergency_puts("System halted.\n");
    emergency_puts("========================================\n");

    __asm__ __volatile__("cli");
    for (;;) __asm__ __volatile__("hlt");
}
