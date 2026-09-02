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

#endif
