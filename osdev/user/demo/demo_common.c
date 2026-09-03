/*
 * user/demo/demo_common.c — implementations of the demo_common.h shims.
 */
#include "demo_common.h"

/*
 * Lines printed since the last terminal_flush().  The suites' check_flush()
 * pagination uses terminal_get_row() exactly like the old terminal server's
 * cursor row — but here we count our own newlines, since the user side
 * cannot see the server's cursor.
 */
static int g_row = 0;

static void count_lines(const char* s)
{
    if (!s)
        return;
    for (; *s; s++) {
        if (*s == '\n')
            g_row++;
    }
}

/* Emit an ANSI SGR colour selector (ESC[<code>m) through the console
 * portal.  The terminal server parses these and switches its VGA text
 * attribute, so coloured output survives the portal round-trip. */
static void console_sgr(u8 code)
{
    char s[6];
    int i = 0;

    s[i++] = 0x1b;
    s[i++] = '[';
    if (code == 0) {
        s[i++] = '0';
    } else {
        s[i++] = '0' + (code / 10) % 10;
        s[i++] = '0' + code % 10;
    }
    s[i++] = 'm';
    s[i] = 0;

    console_putstr(s);
}

void terminal_write(const char* s)
{
    count_lines(s);
    console_putstr(s);
}

void terminal_write_color(const char* s, u8 attr)
{
    u8 fg = attr & 0x0F;   /* foreground nibble of the VGA attribute byte */

    count_lines(s);

    /* VGA fg -> ANSI SGR: 0-7 dark (30-37), 8-15 bright (90-97). */
    if (fg >= 8)
        console_sgr((u8)(90 + fg - 8));
    else
        console_sgr((u8)(30 + fg));

    console_putstr(s);

    /* Reset so subsequent text (e.g. the test name) is plain. */
    console_sgr(0);
}

void terminal_flush(int mode)
{
    (void)mode;

    /* ESC[2J through the console portal: the terminal server blanks the
     * whole screen and homes its cursor, so the next write starts clean
     * instead of mixing with stale output. */
    static const char clear_seq[] = { 0x1b, '[', '2', 'J' };
    user_portal_call(PORTAL_ID_CONSOLE, (void*)clear_seq, sizeof(clear_seq));

    g_row = 0;
}

int terminal_get_row(void)
{
    return g_row;
}

void terminal_switch_to_text_mode(void)
{
}

void timer_delay_ms(u32 ms)
{
    user_delay_ms(ms);
}

void log_write(const char* s)
{
    user_log_str(s);
}
