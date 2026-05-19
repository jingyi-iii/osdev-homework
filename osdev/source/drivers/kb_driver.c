#include "kb_driver.h"
#include "spinlock.h"
#include "irq.h"
#include "arch_irq.h"
#include "core_api.h"
#include "string.h"
#include "heap.h"
#include "log_driver.h"

const unsigned int keymap[NR_SCAN_CODES * MAP_COLS] = {
/* scan-code			!Shift		Shift		E0 XX	*/
/* ==================================================================== */
/* 0x00 - none		*/	0,		    0,		    0,
/* 0x01 - ESC		*/	ESC,		ESC,		0,
/* 0x02 - '1'		*/	'1',		'!',		0,
/* 0x03 - '2'		*/	'2',		'@',		0,
/* 0x04 - '3'		*/	'3',		'#',		0,
/* 0x05 - '4'		*/	'4',		'$',		0,
/* 0x06 - '5'		*/	'5',		'%',		0,
/* 0x07 - '6'		*/	'6',		'^',		0,
/* 0x08 - '7'		*/	'7',		'&',		0,
/* 0x09 - '8'		*/	'8',		'*',		0,
/* 0x0A - '9'		*/	'9',		'(',		0,
/* 0x0B - '0'		*/	'0',		')',		0,
/* 0x0C - '-'		*/	'-',		'_',		0,
/* 0x0D - '='		*/	'=',		'+',		0,
/* 0x0E - BS		*/	BACKSPACE,	BACKSPACE,	0,
/* 0x0F - TAB		*/	TAB,		TAB,		0,
/* 0x10 - 'q'		*/	'q',		'Q',		0,
/* 0x11 - 'w'		*/	'w',		'W',		0,
/* 0x12 - 'e'		*/	'e',		'E',		0,
/* 0x13 - 'r'		*/	'r',		'R',		0,
/* 0x14 - 't'		*/	't',		'T',		0,
/* 0x15 - 'y'		*/	'y',		'Y',		0,
/* 0x16 - 'u'		*/	'u',		'U',		0,
/* 0x17 - 'i'		*/	'i',		'I',		0,
/* 0x18 - 'o'		*/	'o',		'O',		0,
/* 0x19 - 'p'		*/	'p',		'P',		0,
/* 0x1A - '['		*/	'[',		'{',		0,
/* 0x1B - ']'		*/	']',		'}',		0,
/* 0x1C - CR/LF		*/	ENTER,		ENTER,		PAD_ENTER,
/* 0x1D - l. Ctrl	*/	CTRL_L,		CTRL_L,		CTRL_R,
/* 0x1E - 'a'		*/	'a',		'A',		0,
/* 0x1F - 's'		*/	's',		'S',		0,
/* 0x20 - 'd'		*/	'd',		'D',		0,
/* 0x21 - 'f'		*/	'f',		'F',		0,
/* 0x22 - 'g'		*/	'g',		'G',		0,
/* 0x23 - 'h'		*/	'h',		'H',		0,
/* 0x24 - 'j'		*/	'j',		'J',		0,
/* 0x25 - 'k'		*/	'k',		'K',		0,
/* 0x26 - 'l'		*/	'l',		'L',		0,
/* 0x27 - ';'		*/	';',		':',		0,
/* 0x28 - '\''		*/	'\'',		'"',		0,
/* 0x29 - '`'		*/	'`',		'~',		0,
/* 0x2A - l. SHIFT	*/	SHIFT_L,	SHIFT_L,	0,
/* 0x2B - '\'		*/	'\\',		'|',		0,
/* 0x2C - 'z'		*/	'z',		'Z',		0,
/* 0x2D - 'x'		*/	'x',		'X',		0,
/* 0x2E - 'c'		*/	'c',		'C',		0,
/* 0x2F - 'v'		*/	'v',		'V',		0,
/* 0x30 - 'b'		*/	'b',		'B',		0,
/* 0x31 - 'n'		*/	'n',		'N',		0,
/* 0x32 - 'm'		*/	'm',		'M',		0,
/* 0x33 - ','		*/	',',		'<',		0,
/* 0x34 - '.'		*/	'.',		'>',		0,
/* 0x35 - '/'		*/	'/',		'?',		PAD_SLASH,
/* 0x36 - r. SHIFT	*/	SHIFT_R,	SHIFT_R,	0,
/* 0x37 - '*'		*/	'*',		'*',    	0,
/* 0x38 - ALT		*/	ALT_L,		ALT_L,  	ALT_R,
/* 0x39 - ' '		*/	' ',		' ',		0,
/* 0x3A - CapsLock	*/	CAPS_LOCK,	CAPS_LOCK,	0,
/* 0x3B - F1		*/	F1,		    F1,		    0,
/* 0x3C - F2		*/	F2,		    F2,		    0,
/* 0x3D - F3		*/	F3,		    F3,		    0,
/* 0x3E - F4		*/	F4,		    F4,		    0,
/* 0x3F - F5		*/	F5,		    F5,		    0,
/* 0x40 - F6		*/	F6,		    F6,		    0,
/* 0x41 - F7		*/	F7,		    F7,		    0,
/* 0x42 - F8		*/	F8,		    F8,		    0,
/* 0x43 - F9		*/	F9,		    F9,		    0,
/* 0x44 - F10		*/	F10,		F10,		0,
/* 0x45 - NumLock	*/	NUM_LOCK,	NUM_LOCK,	0,
/* 0x46 - ScrLock	*/	SCROLL_LOCK,SCROLL_LOCK,0,
/* 0x47 - Home		*/	PAD_HOME,	'7',		HOME,
/* 0x48 - CurUp		*/	PAD_UP,		'8',		UP,
/* 0x49 - PgUp		*/	PAD_PAGEUP,	'9',		PAGEUP,
/* 0x4A - '-'		*/	PAD_MINUS,	'-',		0,
/* 0x4B - Left		*/	PAD_LEFT,	'4',		LEFT,
/* 0x4C - MID		*/	PAD_MID,	'5',		0,
/* 0x4D - Right		*/	PAD_RIGHT,	'6',		RIGHT,
/* 0x4E - '+'		*/	PAD_PLUS,	'+',		0,
/* 0x4F - End		*/	PAD_END,	'1',		END,
/* 0x50 - Down		*/	PAD_DOWN,	'2',		DOWN,
/* 0x51 - PgDown	*/	PAD_PAGEDOWN,'3',		PAGEDOWN,
/* 0x52 - Insert	*/	PAD_INS,	'0',		INSERT,
/* 0x53 - Delete	*/	PAD_DOT,	'.',		DELETE,
/* 0x54 - Enter		*/	0,		    0,		    0,
/* 0x55 - ???		*/	0,		    0,		    0,
/* 0x56 - ???		*/	0,		    0,		    0,
/* 0x57 - F11		*/	F11,		F11,		0,	
/* 0x58 - F12		*/	F12,		F12,		0,	
/* 0x59 - ???		*/	0,		    0,		    0,	
/* 0x5A - ???		*/	0,		    0,		    0,	
/* 0x5B - ???		*/	0,		    0,		    GUI_L,	
/* 0x5C - ???		*/	0,		    0,		    GUI_R,	
/* 0x5D - ???		*/	0,		    0,		    APPS,	
/* 0x5E - ???		*/	0,		    0,		    0,	
/* 0x5F - ???		*/	0,		    0,		    0,
/* 0x60 - ???		*/	0,		    0,		    0,
/* 0x61 - ???		*/	0,		    0,		    0,	
/* 0x62 - ???		*/	0,		    0,		    0,	
/* 0x63 - ???		*/	0,		    0,		    0,	
/* 0x64 - ???		*/	0,		    0,		    0,	
/* 0x65 - ???		*/	0,		    0,		    0,	
/* 0x66 - ???		*/	0,		    0,		    0,	
/* 0x67 - ???		*/	0,		    0,		    0,	
/* 0x68 - ???		*/	0,		    0,		    0,	
/* 0x69 - ???		*/	0,		    0,		    0,	
/* 0x6A - ???		*/	0,		    0,		    0,	
/* 0x6B - ???		*/	0,		    0,		    0,	
/* 0x6C - ???		*/	0,		    0,		    0,	
/* 0x6D - ???		*/	0,		    0,		    0,	
/* 0x6E - ???		*/	0,		    0,		    0,	
/* 0x6F - ???		*/	0,		    0,		    0,	
/* 0x70 - ???		*/	0,		    0,		    0,	
/* 0x71 - ???		*/	0,		    0,		    0,	
/* 0x72 - ???		*/	0,		    0,		    0,	
/* 0x73 - ???		*/	0,		    0,		    0,	
/* 0x74 - ???		*/	0,		    0,		    0,	
/* 0x75 - ???		*/	0,		    0,		    0,	
/* 0x76 - ???		*/	0,		    0,		    0,	
/* 0x77 - ???		*/	0,		    0,		    0,	
/* 0x78 - ???		*/	0,		    0,		    0,	
/* 0x78 - ???		*/	0,		    0,		    0,	
/* 0x7A - ???		*/	0,		    0,		    0,	
/* 0x7B - ???		*/	0,		    0,		    0,	
/* 0x7C - ???		*/	0,		    0,		    0,	
/* 0x7D - ???		*/	0,		    0,		    0,	
/* 0x7E - ???		*/	0,		    0,		    0,
/* 0x7F - ???		*/	0,		    0,		    0
};

typedef struct kbuf {
    char *head;
    char *tail;
    char buf[MAX_KB_SIZE];
    uint32_t count;
} kbuf;

struct kb_listener {
    list_node node;
    kb_callback_fn cb;
};

struct kb_device {
    kbuf buf;                           /* circular input buffer        */
    struct platform_device plat_dev;    /* platform device structure    */
    list_node listener_list;            /* registered callback list     */
    spinlock* lock;                     /* protects listener_list       */
    irq* irq;                           /* keyboard IRQ descriptor      */
};

struct kb_device kb_device = {
    .plat_dev = {
        .dev = {
            .name = "keyboard",
            .type = "keyboard",
        },
        .num_res = 1,
        .resources[0] = {
            .type = PLAT_RES_IRQ,
            .irq.major = KEYBOARD_IRQ_NO,
            .irq.minor = 0,
        },
    },
};

/* ===========================================================
 *  Circular buffer operations
 * =========================================================== */
static void kbuf_reset(kbuf* kb)
{
    memset(kb->buf, 0, sizeof(kb->buf));
    kb->head = kb->buf;
    kb->tail = kb->buf;
    kb->count = 0;
}

static void kbuf_add(kbuf* kb, char chr)
{
    if (kb->count >= MAX_KB_SIZE)
        return;
    *kb->head = chr;
    kb->head += 1;
    kb->count += 1;
    if (kb->head >= kb->buf + MAX_KB_SIZE)
        kb->head = kb->buf;
}

static char kbuf_pop(kbuf* kb)
{
    if (!kb->count)
        return 0;
    char key = *kb->tail;
    kb->tail += 1;
    kb->count -= 1;
    if (kb->tail >= kb->buf + MAX_KB_SIZE)
        kb->tail = kb->buf;
    return key;
}

static int kbuf_is_empty(const kbuf* kb)
{
    return kb->count == 0;
}

static struct kb_device kbdev;

static uint8_t parse(uint8_t code)
{
    static int lshift = 0;
    static int rshift = 0;
    static int isCapsLocked = 0;
    int isPressed = 0;
    uint8_t key = 0;
    switch (code) {
    case 0xe1:
        break;
    case 0xe0:
        break;
    default:
        break;
    }    
    
    isPressed = (code & FLAG_BREAK ? 0 : 1);
    if (lshift || rshift) {
        if (!isCapsLocked)
            key = keymap[(code & 0x7f) * MAP_COLS + 1];
        else
            key = keymap[(code & 0x7f) * MAP_COLS];
    } else {
        if (!isCapsLocked)
            key = keymap[(code & 0x7f) * MAP_COLS];
        else
            key = keymap[(code & 0x7f) * MAP_COLS + 1];
    }
    
    switch (key) {
    case SHIFT_L:
        lshift = isPressed;
        key = 0;
        break;
    case SHIFT_R:
        rshift = isPressed;
        key = 0;
        break;
    case CAPS_LOCK:
        if (!isPressed)
            return 0;
        isCapsLocked = !isCapsLocked;
        key = 0;
        break;
    default:
        if (!isPressed)
            key = 0;
        break;
    }
    
    return key;
}

// ===========================================================

static void kb_handler(void* context)
{
    struct platform_bus_ops* ops = (struct platform_bus_ops*)context;
    if (!ops)
        return;

    KLOG("kb_handler triggered\n");

    uint8_t scancode = ops->in_port8(0x60);
    uint8_t key = parse(scancode);
    if (key)
        kbuf_add(&kb_device.buf, (char)key);

    /* distribute one key from the buffer to all registered listeners */
    char keybuf[2] = {0};
    keybuf[0] = kbuf_pop(&kb_device.buf);
    if (keybuf[0]) {
        spinlock_lock(kb_device.lock);
        list_for_each(pos, &kb_device.listener_list) {
            struct kb_listener* lsn = list_entry(pos, struct kb_listener, node);
            if (lsn->cb) {
                lsn->cb(keybuf, 1);
            }
        }
        spinlock_unlock(kb_device.lock);
    }
}

static int kb_probe(struct device *dev)
{
    ULOG("kb_probe");

    kb_device.lock = spinlock_alloc();
    list_init(&kb_device.listener_list);
    kbuf_reset(&kb_device.buf);

    struct platform_device* device = to_platform_device(dev);
    struct platform_resource* res = platform_device_get_resource(
        device, PLAT_RES_IRQ, 0);
    struct platform_bus_ops* ops = platform_device_get_ops(device);

    uint32_t irq_nr = res ? res->irq.major : KEYBOARD_IRQ_NO;

    int ret = irq_request(&kb_device.irq, "kbd", irq_nr, IRQ_ANY_MINOR, kb_handler, ops);
    if (ret)
        return ret;

    irq_unmask(kb_device.irq);
    return 0;
}

static int kb_remove(struct device *dev)
{
    (void)dev;
    ULOG("kb_remove");

    if (kb_device.irq) {
        irq_mask(kb_device.irq);
        irq_release(kb_device.irq);
        kb_device.irq = 0;
    }

    if (kb_device.lock) {
        spinlock_lock(kb_device.lock);
        list_init(&kb_device.listener_list);
        spinlock_unlock(kb_device.lock);
        spinlock_release(kb_device.lock);
        kb_device.lock = 0;
    }

    kbuf_reset(&kb_device.buf);
    return 0;
}

/* ===========================================================
 *  C API for registering/unregistering keyboard listeners
 * =========================================================== */

int kb_register_callback(kb_callback_fn cb)
{
    if (!cb)
        return -1;

    struct kb_listener* lsn = (struct kb_listener*)kmalloc(sizeof(*lsn));
    if (!lsn)
        return -1;

    lsn->cb = cb;

    spinlock_lock(kb_device.lock);
    list_add(&lsn->node, &kb_device.listener_list);
    spinlock_unlock(kb_device.lock);

    return 0;
}

void kb_unregister_callback(kb_callback_fn cb)
{
    if (!cb)
        return;

    spinlock_lock(kb_device.lock);
    list_for_each(pos, &kb_device.listener_list) {
        struct kb_listener* lsn = list_entry(pos, struct kb_listener, node);
        if (lsn->cb == cb) {
            list_del(&lsn->node);
            kfree(lsn);
            break;
        }
    }
    spinlock_unlock(kb_device.lock);
}

struct driver kb_driver = {
    .type = "keyboard",
    .probe = kb_probe,
    .remove = kb_remove,
};

void kb_init(void)
{
    KLOG("kb_init");

    platform_driver_register(&kb_driver);
    platform_device_register(&kb_device.plat_dev.dev);
}

void kb_exit(void)
{
    KLOG("kb_exit");
}
