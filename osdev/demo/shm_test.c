/*******************************************************************************
 *                                                                             *
 *    SHM Test — verify shm_share / shm_unshare really share data             *
 *                                                                             *
 *    Uses two KERNEL processes (kernel processes share the kernel address     *
 *    space, so plain globals can be used for handshaking).  The source        *
 *    process allocates a buffer and fills it with a known pattern, then       *
 *    shares it into the target process via shm_share().  The target reads     *
 *    the pattern through the shared VA returned by shm_share(), writes a      *
 *    modified pattern back, and the source confirms the write-back is         *
 *    visible — proving both processes touch the same physical pages.          *
 *    Finally shm_unshare() tears the mapping and the capability down.         *
 *                                                                             *
 *******************************************************************************/

#include "drivers/terminal_server.h"
#include "drivers/timer_server.h"
#include "drivers/log_server.h"
#include "kernel/process.h"
#include "mm/vmm.h"
#include "ipc/shm.h"
#include "lib/string.h"

#define SHM_TEST_PAGES   4
#define SHM_TEST_SIZE    (SHM_TEST_PAGES * PAGE_SIZE)
#define SHM_TEST_PATTERN 0xA5

/* Completion flag consumed by the menu's run_test_suite(). */
extern volatile int test_finished_flag;

/* Handshake state shared between the source runner and the target process.
 * Kernel processes share the kernel address space, so these globals are
 * visible on both sides. */
static volatile int     shm_target_ready   = 0;  /* target is up and knows its pid */
static volatile int     shm_target_done    = 0;  /* target finished verification   */
static volatile int     shm_target_result  = -1; /* 0 = pattern matched            */
static volatile int     shm_target_release = 0;  /* source done; target may exit   */
static volatile int32_t shm_target_pid     = -1;
static          int     shm_source_size    = 0;
static volatile void*   shm_target_va      = 0;  /* VA returned by shm_share()     */

/* ------------------------------------------------------------------ *
 *  Target process: verify the pattern through the shared mapping,    *
 *  write a modified pattern back, then report the result.            *
 * ------------------------------------------------------------------ */
static void shm_target_main(void)
{
    volatile unsigned char* p;
    int i;
    int ok = 1;

    shm_target_pid = proc_get_pid();
    shm_target_ready = 1;

    /* Wait until the source has shared the buffer and published the VA,
     * or has aborted the test (share failed). */
    while (!shm_target_va && !shm_target_release)
        thread_yield();

    if (!shm_target_va) {
        /* source aborted: nothing to verify */
        shm_target_result = -1;
        shm_target_done = 1;
        proc_exit(proc_get_pid());
    }

    p = (volatile unsigned char*)shm_target_va;

    /* 1) the source's pattern must be readable via the shared mapping */
    for (i = 0; i < shm_source_size; i++) {
        if (p[i] != (unsigned char)SHM_TEST_PATTERN) {
            ok = 0;
            break;
        }
    }

    /* 2) write a modified pattern back through the same mapping */
    if (ok) {
        for (i = 0; i < shm_source_size; i++)
            p[i] = (unsigned char)(SHM_TEST_PATTERN + 1);
    }

    shm_target_result = ok ? 0 : -1;
    shm_target_done = 1;

    /* 在源进程完成 shm_unshare 之前保持存活。一旦 proc_exit，内核会销毁
     * 目标的 vcb 与能力表，源进程的 shm_unshare 将找不到目标进程。 */
    while (!shm_target_release)
        thread_yield();

    proc_exit(proc_get_pid());
}

/* ------------------------------------------------------------------ *
 *  Test runner: allocate + fill, share into the target, verify the   *
 *  write-back, then unshare.                                         *
 * ------------------------------------------------------------------ */
void shm_test_main(void)
{
    pcb* self = get_current_process();
    unsigned char* buf = 0;
    void* out_va = 0;
    int i;
    int ok = 1;

    /* 重置握手状态：这些静态全局变量跨多次运行会残留，不重置会导致
     * 第二次运行用上一次的 ready=1 / pid / done=1，逻辑完全错乱。 */
    shm_target_ready   = 0;
    shm_target_done    = 0;
    shm_target_result  = -1;
    shm_target_release = 0;
    shm_target_pid     = -1;
    shm_source_size    = 0;
    shm_target_va      = 0;

    terminal_write("\n=== SHM (shared memory) test ===\n");

    /* 1. allocate a source buffer in this process's address space */
    buf = (unsigned char*)vmm_alloc_pages(&self->vcb, SHM_TEST_PAGES, PTE_USER_PAGE);
    if (!buf) {
        terminal_write("[SHM] FAIL: vmm_alloc_pages\n");
        test_finished_flag = 1;
        proc_exit(proc_get_pid());
        return;   /* unreachable if proc_exit works */
    }
    for (i = 0; i < SHM_TEST_SIZE; i++)
        buf[i] = (unsigned char)SHM_TEST_PATTERN;

    shm_source_size = SHM_TEST_SIZE;

    /* 2. spawn the target process (KERNEL: shares the address space) */
    proc_create(PROC_PRIV_KERNEL, shm_target_main, 0);
    while (!shm_target_ready)
        thread_yield();

    /* 3. share the buffer into the target; out_va is the target's VA */
    if (shm_share((int32_t)shm_target_pid, buf, SHM_TEST_SIZE, &out_va) != 0) {
        terminal_write("[SHM] FAIL: shm_share\n");
        shm_target_release = 1;         /* tell the target to give up and exit */
        while (!shm_target_done)
            thread_yield();
        vmm_free_pages(&self->vcb, buf);
        test_finished_flag = 1;
        proc_exit(proc_get_pid());
        return;   /* unreachable if proc_exit works */
    }
    shm_target_va = out_va;

    /* 4. wait for the target to verify + write back */
    while (!shm_target_done)
        thread_yield();

    if (shm_target_result != 0) {
        terminal_write("[SHM] FAIL: target could not read the shared data\n");
        ok = 0;
    }

    /* 5. the modified pattern must be visible at the source buffer */
    if (ok) {
        for (i = 0; i < SHM_TEST_SIZE; i++) {
            if (buf[i] != (unsigned char)(SHM_TEST_PATTERN + 1)) {
                ok = 0;
                break;
            }
        }
        if (!ok)
            terminal_write("[SHM] FAIL: target write-back not visible at source\n");
    }

    /* 6. tear down the shared mapping + capability */
    if (shm_unshare((int32_t)shm_target_pid, out_va) != 0)
        terminal_write("[SHM] FAIL: shm_unshare\n");
    else
        terminal_write("[SHM] OK: shm_unshare\n");

    shm_target_release = 1;             /* let the target exit now */
    vmm_free_pages(&self->vcb, buf);

    if (ok)
        terminal_write("[SHM] PASS: data shared correctly between two processes\n\n");
    else
        terminal_write("[SHM] FAIL: shared memory test failed\n\n");

    /* 停留 3 秒，让结果可见再返回菜单 */
    timer_delay_ms(3000);

    test_finished_flag = 1;
    proc_exit(proc_get_pid());
}

/* ================================================================== *
 *  SHM stress test — repeatedly share / unshare the same buffer to  *
 *  hammer the capability + vmm map/unmap teardown paths and catch    *
 *  leaks / double-frees.  One target process, N rounds, a fresh      *
 *  pattern every round.                                              *
 * ================================================================== */
#define SHM_STRESS_ROUNDS  8
#define SHM_STRESS_PAGES   16
#define SHM_STRESS_PATTERN 0x5A

/* Stress handshake.  A monotonic round counter is the only sync the
 * target needs: the source bumps it only after a successful share, so
 * the target never observes a half-initialised round. */
static volatile int     shm_stress_ready  = 0;   /* target is up */
static volatile int     shm_stress_done   = 0;   /* target finished current round */
static volatile int     shm_stress_result = -1;  /* 0 = pattern matched */
static volatile int32_t shm_stress_pid    = -1;
static          int     shm_stress_size   = 0;
static volatile void*   shm_stress_va     = 0;   /* VA for the current round */
static volatile int     shm_stress_round  = 0;   /* publish counter */
static volatile int     shm_stress_exit   = 0;   /* abort / finish */

static void shm_stress_target_main(void)
{
    volatile unsigned char* p;
    int i;
    int ok;
    int cur = 0;

    shm_stress_pid = proc_get_pid();
    shm_stress_ready = 1;

    for (;;) {
        /* wait for the next published round, or abort */
        while (shm_stress_round == cur && !shm_stress_exit)
            thread_yield();
        if (shm_stress_exit)
            break;
        cur = shm_stress_round;

        p = (volatile unsigned char*)shm_stress_va;
        ok = 1;
        for (i = 0; i < shm_stress_size; i++) {
            if (p[i] != (unsigned char)SHM_STRESS_PATTERN) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            for (i = 0; i < shm_stress_size; i++)
                p[i] = (unsigned char)(SHM_STRESS_PATTERN + 1);
        }

        shm_stress_result = ok ? 0 : -1;
        shm_stress_done = 1;
    }

    proc_exit(proc_get_pid());
}

void shm_stress_main(void)
{
    pcb* self = get_current_process();
    unsigned char* buf = 0;
    void* out_va = 0;
    char msg[64];
    int round;
    int i;
    int ok = 1;

    /* reset handshake state — survives across runs like the SHM test */
    shm_stress_ready  = 0;
    shm_stress_done   = 0;
    shm_stress_result = -1;
    shm_stress_pid    = -1;
    shm_stress_size   = 0;
    shm_stress_va     = 0;
    shm_stress_round  = 0;
    shm_stress_exit   = 0;

    terminal_write("\n=== SHM stress test ===\n");

    buf = (unsigned char*)vmm_alloc_pages(&self->vcb, SHM_STRESS_PAGES, PTE_USER_PAGE);
    if (!buf) {
        terminal_write("[SHMS] FAIL: vmm_alloc_pages\n");
        test_finished_flag = 1;
        proc_exit(proc_get_pid());
        return;
    }
    shm_stress_size = SHM_STRESS_PAGES * PAGE_SIZE;

    proc_create(PROC_PRIV_KERNEL, shm_stress_target_main, 0);
    while (!shm_stress_ready)
        thread_yield();

    for (round = 0; round < SHM_STRESS_ROUNDS; round++) {
        /* fresh pattern for this round */
        for (i = 0; i < shm_stress_size; i++)
            buf[i] = (unsigned char)SHM_STRESS_PATTERN;
        shm_stress_done   = 0;
        shm_stress_result = -1;

        /* 每轮进度：屏幕 + serial 各一条，方便判断是卡住还是在进行中 */
        snprintf(msg, sizeof(msg), "[SHMS] round %d/%d\n", round + 1, SHM_STRESS_ROUNDS);
        terminal_write(msg);
        LOG("[SHMS] round %d/%d start", round + 1, SHM_STRESS_ROUNDS);

        if (shm_share((int32_t)shm_stress_pid, buf, shm_stress_size, &out_va) != 0) {
            terminal_write("[SHMS] FAIL: shm_share\n");
            LOG("[SHMS] round %d FAIL: shm_share", round + 1);
            ok = 0;
            shm_stress_exit = 1;
            break;
        }
        LOG("[SHMS] round %d shared va=0x%x", round + 1, (uint32_t)out_va);
        shm_stress_va = out_va;
        shm_stress_round = round + 1;        /* publish: target wakes */

        while (!shm_stress_done)
            thread_yield();
        LOG("[SHMS] round %d target done result=%d", round + 1, shm_stress_result);

        if (shm_stress_result != 0) {
            terminal_write("[SHMS] FAIL: target pattern mismatch\n");
            ok = 0;
        }
        if (ok) {
            for (i = 0; i < shm_stress_size; i++) {
                if (buf[i] != (unsigned char)(SHM_STRESS_PATTERN + 1)) {
                    ok = 0;
                    break;
                }
            }
            if (!ok)
                terminal_write("[SHMS] FAIL: write-back not visible\n");
        }

        if (shm_unshare((int32_t)shm_stress_pid, out_va) != 0) {
            terminal_write("[SHMS] FAIL: shm_unshare\n");
            LOG("[SHMS] round %d FAIL: shm_unshare", round + 1);
            ok = 0;
        } else {
            LOG("[SHMS] round %d unshared", round + 1);
        }
    }

    shm_stress_exit = 1;                     /* let the target exit */
    vmm_free_pages(&self->vcb, buf);

    if (ok) {
        snprintf(msg, sizeof(msg), "[SHMS] PASS: %d share/unshare rounds OK\n\n", SHM_STRESS_ROUNDS);
        LOG("[SHMS] PASS: %d share/unshare rounds OK", SHM_STRESS_ROUNDS);
    } else {
        snprintf(msg, sizeof(msg), "[SHMS] FAIL: stress test failed (round %d)\n\n", round);
        LOG("[SHMS] FAIL: stress test failed (round %d)", round);
    }
    terminal_write(msg);

    /* 停留 3 秒，让结果可见再返回菜单 */
    timer_delay_ms(3000);

    test_finished_flag = 1;
    proc_exit(proc_get_pid());
}
