/*
 * user/hello.c — first user-mode ELF program.
 *
 * Runs in its own address space (loaded by kernel/elf.c), calls the
 * kernel only through the fixed syscall ABI (console output + yield +
 * a direct LOG to the user-mode log server), and never touches kernel
 * code or data directly.
 */
#include "userlib.h"

void _start(void)
{
    console_putstr("\n[user-elf] Hello from a real user-mode ELF!\n");
    console_putstr("[user-elf] Looping in user space (yield via syscall)...\n");

    /* LOG through the namespace-resolved log server ("log"). */
    user_log_str("[user-elf] Direct LOG via the log server (namespace 'log')!\n");

    /* RTC: read the CMOS clock through the rtc server ("rtc") and log it.
     * (No printf in the freestanding user link — hand-format digits.) */
    {
        rtc_time t;
        if (user_rtc_time(&t) == 0) {
            char buf[64];
            int i = 0;
            static const char* pfx = "[user-elf] RTC time: ";
            for (const char* p = pfx; *p; p++) buf[i++] = *p;
            u32 vals[6] = { t.year, t.month, t.day, t.hour, t.minute, t.second };
            for (int v = 0; v < 6; v++) {
                char d[10];
                int n = 0;
                u32 x = vals[v];
                if (x == 0) d[n++] = '0';
                while (x) { d[n++] = (char)('0' + x % 10); x /= 10; }
                while (n) buf[i++] = d[--n];
                buf[i++] = (v == 2) ? ' ' : (v == 5) ? '\n' : (v < 2) ? '-' : ':';
            }
            buf[i] = 0;
            user_log_str(buf);
        } else {
            user_log_str("[user-elf] RTC read failed (server not up?)\n");
        }
    }

    /* SLEEP_MS: sleep 2000 ms via the rtc server, then read the RTC again
     * — the two logged timestamps should differ by ~2 s. */
    if (user_rtc_sleep_ms(2000) == 0) {
        rtc_time t;
        if (user_rtc_time(&t) == 0) {
            char buf[64];
            int i = 0;
            static const char* pfx = "[user-elf] RTC time after 2000ms sleep: ";
            for (const char* p = pfx; *p; p++) buf[i++] = *p;
            u32 vals[6] = { t.year, t.month, t.day, t.hour, t.minute, t.second };
            for (int v = 0; v < 6; v++) {
                char d[10];
                int n = 0;
                u32 x = vals[v];
                if (x == 0) d[n++] = '0';
                while (x) { d[n++] = (char)('0' + x % 10); x /= 10; }
                while (n) buf[i++] = d[--n];
                buf[i++] = (v == 2) ? ' ' : (v == 5) ? '\n' : (v < 2) ? '-' : ':';
            }
            buf[i] = 0;
            user_log_str(buf);
        }
    } else {
        user_log_str("[user-elf] RTC sleep failed (server not up?)\n");
    }

    for (;;)
        user_yield();
}
