/*
 * user/server/server_msgs.h — shared message definitions for user-space
 * servers and their consumers (kb_server → terminal_server / games).
 *
 * Plain byte/integer layouts only.  Consumers include this header, then
 * user_mail_subscribe(MSG_KEY_EVENT) and filter incoming mails by magic.
 */
#ifndef SERVER_MSGS_H
#define SERVER_MSGS_H

#include "lib/types.h"

/* Mail magic for keyboard key events, broadcast by kb_server.  Value is
 * in the user space (0x1000+) range — never 0, and != MAIL_MAGIC_IRQ. */
#define MSG_KEY_EVENT   0x1001

/* Payload carried in mail.data[] of a MSG_KEY_EVENT mail. */
typedef struct key_event {
    u8  scancode;   /* scancode set 1 raw code (bit 7 set = key released) */
    char ascii;     /* translated printable char on press, 0 otherwise    */
    u8  pressed;    /* 1 = press, 0 = release                             */
} key_event;

/* ---- RTC server (portal RPC) ------------------------------------------
 * The rtc server is a portal service (registered as "rtc" in the
 * namespace).  The client portal_call()s one rtc_request; the server
 * writes results back into the same shm-mapped buffer and replies with a
 * status int (0 = ok, -3 = malformed/unknown cmd). */
typedef struct rtc_time {
    u32 year;       /* full year (e.g. 2026)      */
    u32 month;      /* 1..12                      */
    u32 day;        /* 1..31                      */
    u32 hour;       /* 0..23                      */
    u32 minute;     /* 0..59                      */
    u32 second;     /* 0..59                      */
} rtc_time;

enum {
    RTC_CMD_GET_TIME = 1,   /* read CMOS RTC, fills .time               */
    RTC_CMD_SLEEP_MS = 2,   /* busy-delay .sleep_ms via the PIT counter */
};

typedef struct rtc_request {
    u32      cmd;           /* RTC_CMD_*                      */
    u32      sleep_ms;      /* SLEEP_MS input                 */
    rtc_time time;          /* GET_TIME output (server fills) */
} rtc_request;

/* ---- Graphics (console portal control frames) -------------------------
 * The terminal server doubles as the graphics server: the console portal
 * accepts plain text payloads AND binary control frames (both start with
 * ESC, distinguished by the byte after it).  The framebuffer itself is
 * NOT carried in portal payloads — the client shm_share()s its frame
 * buffer with the terminal process and only portal_call()s a tiny
 * "blit" frame when a frame is ready (shared-memory-first, one mapping).
 *
 * Frame header (little-endian, sent as the portal payload):
 *   ESC 'G' cmd u8 reserved u32 a u32 b
 *   GFX_CTRL_SET_MODE: a = 0x03 (text) or 0x13 (320x200x256 graphics)
 *   GFX_CTRL_BLIT:     a = fb size in bytes (must equal GFX_FB_SIZE for
 *                      mode 0x13); the server memcpy()s the client's
 *                      shm-mapped frame buffer to 0xA0000
 *
 * The portal reply int is the status: 0 = ok, negative = error
 * (-3 = malformed, -1 = wrong fb size / not in graphics mode). */
#define GFX_FB_SIZE   (320 * 200)   /* mode 0x13: 320x200, 1 byte/pixel */
#define GFX_MODE_TEXT 0x03
#define GFX_MODE_13   0x13

enum {
    GFX_CTRL_SET_MODE = 1,
    GFX_CTRL_BLIT     = 2,
};

typedef struct gfx_ctrl {
    u8  esc;        /* 0x1b — marks a control frame */
    u8  tag;        /* 'G'  — graphics (vs '[' ANSI text) */
    u8  cmd;        /* GFX_CTRL_*                       */
    u8  reserved;
    u32 a;          /* SET_MODE: mode; BLIT: fb size    */
    u32 b;          /* unused                           */
} gfx_ctrl;

#endif
