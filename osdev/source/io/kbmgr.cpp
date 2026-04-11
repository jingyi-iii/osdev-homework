#include "kbmgr.h"
#include "core_api.h"
#include "arch_regs.h"
#include "module.h"
#include "list.h"
#include "string.h"

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

extern "C" {

static void keyboard_handler(void* dev)
{
    (void)dev;
    KBMgr::GetInstance()->OnReceive(arch_inb(0x60));
}

static void keyboard_init(void)
{
    KBMgr::GetInstance()->Start();
}

module_init(keyboard_init);
}

uint8_t KDecoder::Parse(uint8_t code)
{
    static bool lshift = false;
    static bool rshift = false;
    static bool isCapsLocked = false;
    bool isPressed = false;
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

void KBuf::Reset(void)
{
    memset(mBuffer, 0, sizeof(mBuffer));
    mpHead = mBuffer;
    mpTail = mBuffer;
    mCount = 0;
}

void KBuf::Add(char chr)
{
    if (mCount >= MAX_KB_SIZE) {
        return;
    }
    *mpHead = chr;
    mpHead += 1;
    mCount += 1;
    if (mpHead >= mBuffer + MAX_KB_SIZE) {
        mpHead = mBuffer;
    }
}

char KBuf::Pop(void)
{
    if (!mCount)
        return 0;
    
    char key = *mpTail;
    mpTail += 1;
    mCount -= 1;
    if (mpTail >= mBuffer + MAX_KB_SIZE) {
        mpTail = mBuffer;
    }
    return key;
}

bool KBuf::IsEmpty() const
{
    return mCount == 0;
}

bool KBuf::IsFull() const
{
    return mCount >= MAX_KB_SIZE;
}

uint32_t KBuf::GetCount() const
{
    return mCount;
}

KBMgr::KBMgr(void)
{
    mLock = spinlock_alloc();
    list_init(&mDevList);
    mIrqDev = 0;

    irqdev_init(&mIrqDev, "kbd", KEYBOARD_IRQ_NO, 0, keyboard_handler);
}

KBMgr::~KBMgr(void)
{
    irqdev_release(mIrqDev);
    list_del(&mDevList);
    spinlock_release(mLock);
}

void KBMgr::OnReceive(uint8_t code)
{
    auto key = mDecoder.Parse(code);
    if (key)
        mKbuf.Add(key);

    char keybuf[2] = {0};
    keybuf[0] = GetOneKey();
    if (keybuf[0]) {
        list_for_each(pos, &mDevList) {
            iodev* dev = list_entry(pos, iodev, dev_node);
            if (dev->data_cb) {
                dev->data_cb(dev, keybuf, 1);
            }
        }
    }
}

void KBMgr::Start(void)
{
    mKbuf.Reset();

    if (mIrqDev)
        mIrqDev->unmask(mIrqDev);
}

void KBMgr::Stop(void)
{
    if (mIrqDev)
        mIrqDev->mask(mIrqDev);
}

uint8_t KBMgr::GetOneKey(void)
{
    if (mKbuf.IsEmpty())
        return 0;
        
    return mKbuf.Pop();
}

int KBMgr::AddDevice(iodev* dev)
{
    if (!dev)
        return -1;

    spinlock_lock(mLock);
    list_add(&dev->dev_node, &mDevList);
    spinlock_unlock(mLock);
    return 0;
}

void KBMgr::RemoveDevice(iodev* dev)
{
    if (!dev)
        return;

    spinlock_lock(mLock);
    list_del(&dev->dev_node);
    spinlock_unlock(mLock);
}

int KBMgr::Init(void){ return 0; }
int KBMgr::Read(char* buf, size_t size){ (void)buf; (void)size; return 0; }
int KBMgr::Write(const char* buf, size_t size){ (void)buf; (void)size; return 0; }
int KBMgr::Ctrl(int cmd, void* arg){ (void)cmd; (void)arg; return 0; }
int KBMgr::Shutdown(void){ return 0; }

extern "C" {

int kbdev_init(iodev **out_dev, const char* dev_name, iodev_cb cb)
{
    if (!out_dev)
        return -1;

    iodev* dev = nullptr;
    KBMgr* instance = KBMgr::GetInstance();

    io_alloc_dev(dev_name, &dev);
    if (dev && instance) {
        instance->_bind_c_interface(dev);
        dev->data_cb = cb;
        instance->AddDevice(dev);
    }

    *out_dev = dev;
    return (dev != nullptr) ? 0 : -1;
}

void kbdev_release(iodev **dev)
{
    if (!dev)
        return;

    KBMgr* instance = KBMgr::GetInstance();
    if (instance) {
        instance->RemoveDevice(*dev);
    }
    io_free_dev(*dev);
    *dev = 0;
}

}