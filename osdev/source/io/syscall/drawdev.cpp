#include "drawdev.h"
#include "arch/arch_regs.h"

/* VGA CRTC register ports */
#define VGA_CRTC_INDEX    0x3D4
#define VGA_CRTC_DATA     0x3D5
#define VGA_MISC_WRITE    0x3C2
#define VGA_SEQ_INDEX     0x3C4
#define VGA_SEQ_DATA      0x3C5
#define VGA_GC_INDEX      0x3CE
#define VGA_GC_DATA       0x3CF
#define VGA_AC_INDEX      0x3C0
#define VGA_AC_DATA       0x3C1

/* VGA Misc Output Register bits */
#define VGA_MISC_COLOR    0x01
#define VGA_MISC_MEM2     0x02
#define VGA_MISC_MEM3     0x04
#define VGA_MISC_SYNC     0x08
#define VGA_MISC_OE       0x10

/* VGA Sequencer registers */
#define VGA_SEQ_RESET     0x00
#define VGA_SEQ_CLOCK     0x01
#define VGA_SEQ_MAP_MASK  0x02
#define VGA_SEQ_CHAR_MAP  0x03
#define VGA_SEQ_MEM_MODE  0x04

/* VGA Graphics Controller registers */
#define VGA_GC_SET_RESET  0x00
#define VGA_GC_ENABLE_SR  0x01
#define VGA_GC_COLOR_CMP  0x02
#define VGA_GC_DATA_ROTATE 0x03
#define VGA_GC_READ_MAP   0x04
#define VGA_GC_MODE       0x05
#define VGA_GC_MISC       0x06

/* VGA CRTC registers for mode 13h */
#define VGA_CRTC_OFFSET_REG     0x13
#define VGA_CRTC_UNDERLINE_LOC  0x14
#define VGA_CRTC_MODE_CTRL      0x17

class DrawDev {
private:
    bool mIsGraphicsMode;
    vga_fb_info mFbInfo;

    DrawDev(void) : mIsGraphicsMode(false), mFbInfo() {
        mFbInfo.framebuffer = (uint8_t*)VGA_FRAMEBUFFER_ADDR;
        mFbInfo.width = 0;
        mFbInfo.height = 0;
        mFbInfo.bpp = 0;
        mFbInfo.pitch = 0;
        mFbInfo.is_graphics = 0;
    }
    ~DrawDev(void) = default;

public:
    IODEV_CPP_BIND_CLASS(DrawDev);

    static DrawDev* GetInstance(void)
    {
        static DrawDev inst;
        return &inst;
    }

    int Init(void);
    int Read(char* buf, size_t size);
    int Write(const char* buf, size_t size);
    int Ctrl(int cmd, void* arg);
    int Shutdown(void);

private:
    int SwitchToGraphicsMode(void);
    int SwitchToTextMode(void);
    void SetPalette(uint8_t index, const vga_palette* color);
    void SetPixel(int32_t x, int32_t y, uint8_t color);
    void ProgramVgaRegisters(void);
    void RestoreVgaRegisters(void);
};

/* Program VGA hardware registers for mode 13h (320x200x8bpp) */
void DrawDev::ProgramVgaRegisters(void)
{
    /* Write the Miscellaneous Output Register */
    arch_outb(VGA_MISC_WRITE, VGA_MISC_COLOR | VGA_MISC_SYNC | VGA_MISC_OE);

    /* Unprotect CRTC registers 0-7 */
    arch_outb(VGA_CRTC_INDEX, 0x11);
    uint8_t old_val = arch_inb(VGA_CRTC_DATA);
    arch_outb(VGA_CRTC_DATA, old_val & 0x7F);

    /* Program the Sequencer registers */
    arch_outb(VGA_SEQ_INDEX, VGA_SEQ_RESET);
    arch_outb(VGA_SEQ_DATA, 0x01);  /* Extended mode */

    arch_outb(VGA_SEQ_INDEX, VGA_SEQ_CLOCK);
    arch_outb(VGA_SEQ_DATA, 0x0F);  /* 8 and 16 bit access */

    arch_outb(VGA_SEQ_INDEX, VGA_SEQ_MAP_MASK);
    arch_outb(VGA_SEQ_DATA, 0x0E);  /* Enable all planes */

    arch_outb(VGA_SEQ_INDEX, VGA_SEQ_CHAR_MAP);
    arch_outb(VGA_SEQ_DATA, 0x00);  /* Alpha mode */

    arch_outb(VGA_SEQ_INDEX, VGA_SEQ_MEM_MODE);
    arch_outb(VGA_SEQ_DATA, 0x0E);  /* Disable odd/even, enable chain4 */

    arch_outb(VGA_SEQ_INDEX, VGA_SEQ_RESET);
    arch_outb(VGA_SEQ_DATA, 0x03);  /* Normal operation */

    /* Program the Graphics Controller registers */
    arch_outb(VGA_GC_INDEX, VGA_GC_SET_RESET);
    arch_outb(VGA_GC_DATA, 0x00);

    arch_outb(VGA_GC_INDEX, VGA_GC_ENABLE_SR);
    arch_outb(VGA_GC_DATA, 0x00);

    arch_outb(VGA_GC_INDEX, VGA_GC_COLOR_CMP);
    arch_outb(VGA_GC_DATA, 0x00);

    arch_outb(VGA_GC_INDEX, VGA_GC_DATA_ROTATE);
    arch_outb(VGA_GC_DATA, 0x00);

    arch_outb(VGA_GC_INDEX, VGA_GC_READ_MAP);
    arch_outb(VGA_GC_DATA, 0x00);

    arch_outb(VGA_GC_INDEX, VGA_GC_MODE);
    arch_outb(VGA_GC_DATA, 0x40);  /* 256-color mode */

    arch_outb(VGA_GC_INDEX, VGA_GC_MISC);
    arch_outb(VGA_GC_DATA, 0x01);  /* Graphics mode, disable odd/even */

    /* Program the CRTC registers */
    arch_outb(VGA_CRTC_INDEX, 0x00);
    arch_outb(VGA_CRTC_DATA, 0x5F);  /* Horizontal Total */

    arch_outb(VGA_CRTC_INDEX, 0x01);
    arch_outb(VGA_CRTC_DATA, 0x4F);  /* Horizontal Display Enable End */

    arch_outb(VGA_CRTC_INDEX, 0x02);
    arch_outb(VGA_CRTC_DATA, 0x50);  /* Start Horizontal Blank */

    arch_outb(VGA_CRTC_INDEX, 0x03);
    arch_outb(VGA_CRTC_DATA, 0x82);  /* End Horizontal Blank */

    arch_outb(VGA_CRTC_INDEX, 0x04);
    arch_outb(VGA_CRTC_DATA, 0x54);  /* Start Horizontal Retrace */

    arch_outb(VGA_CRTC_INDEX, 0x05);
    arch_outb(VGA_CRTC_DATA, 0x81);  /* End Horizontal Retrace */

    arch_outb(VGA_CRTC_INDEX, 0x06);
    arch_outb(VGA_CRTC_DATA, 0xBF);  /* Vertical Total */

    arch_outb(VGA_CRTC_INDEX, 0x07);
    arch_outb(VGA_CRTC_DATA, 0x1E);  /* Overflow register */

    arch_outb(VGA_CRTC_INDEX, 0x09);
    arch_outb(VGA_CRTC_DATA, 0x41);  /* Max Scan Line */

    arch_outb(VGA_CRTC_INDEX, 0x0A);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Cursor Start */

    arch_outb(VGA_CRTC_INDEX, 0x0B);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Cursor End */

    arch_outb(VGA_CRTC_INDEX, 0x0C);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Start Address High */

    arch_outb(VGA_CRTC_INDEX, 0x0D);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Start Address Low */

    arch_outb(VGA_CRTC_INDEX, VGA_CRTC_UNDERLINE_LOC);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Underline Location */

    arch_outb(VGA_CRTC_INDEX, VGA_CRTC_OFFSET_REG);
    arch_outb(VGA_CRTC_DATA, 0x28);  /* Offset (320/8 = 40 = 0x28) */

    arch_outb(VGA_CRTC_INDEX, 0x0E);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Cursor Location High */

    arch_outb(VGA_CRTC_INDEX, 0x0F);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Cursor Location Low */

    arch_outb(VGA_CRTC_INDEX, 0x15);
    arch_outb(VGA_CRTC_DATA, 0x00);  /* Start Address High */

    arch_outb(VGA_CRTC_INDEX, 0x16);
    arch_outb(VGA_CRTC_DATA, 0xEA);  /* Vertical Display Enable End */

    arch_outb(VGA_CRTC_INDEX, 0x17);
    arch_outb(VGA_CRTC_DATA, 0xAC);  /* CRTC Mode Control - byte mode */

    arch_outb(VGA_CRTC_INDEX, 0x18);
    arch_outb(VGA_CRTC_DATA, 0xDF);  /* Start Vertical Blank */

    /* Reprotect CRTC registers 0-7 */
    arch_outb(VGA_CRTC_INDEX, 0x11);
    arch_outb(VGA_CRTC_DATA, old_val);
}

/* Switch to VGA 256-color graphics mode (mode 13h) */
int DrawDev::SwitchToGraphicsMode(void)
{
    /* Program VGA hardware registers directly (no BIOS in protected mode) */
    ProgramVgaRegisters();

    /* Update framebuffer info */
    mFbInfo.framebuffer = (uint8_t*)VGA_FRAMEBUFFER_ADDR;
    mFbInfo.width = VGA_FB_WIDTH;
    mFbInfo.height = VGA_FB_HEIGHT;
    mFbInfo.bpp = VGA_FB_BPP;
    mFbInfo.pitch = VGA_FB_WIDTH;
    mFbInfo.is_graphics = 1;
    mIsGraphicsMode = true;

    return 0;
}

/* Switch back to standard VGA text mode (80x25) */
int DrawDev::SwitchToTextMode(void)
{
    /* Simple approach: write 0x03 to VGA text mode port */
    /* In protected mode without BIOS, we program CRTC directly */
    
    /* Reset Graphics Controller to text mode defaults */
    arch_outb(VGA_GC_INDEX, VGA_GC_MODE);
    arch_outb(VGA_GC_DATA, 0x10);  /* Text mode */

    arch_outb(VGA_GC_INDEX, VGA_GC_MISC);
    arch_outb(VGA_GC_DATA, 0x00);  /* Text mode, odd/even enabled */

    /* Reset Sequencer to text mode */
    arch_outb(VGA_SEQ_INDEX, VGA_SEQ_MEM_MODE);
    arch_outb(VGA_SEQ_DATA, 0x02);  /* Odd/even mode, text mode */

    /* Set CRTC offset for text mode (80 chars / 2 = 40 = 0x28) */
    arch_outb(VGA_CRTC_INDEX, VGA_CRTC_OFFSET_REG);
    arch_outb(VGA_CRTC_DATA, 0x28);

    /* Update framebuffer info */
    mFbInfo.width = 0;
    mFbInfo.height = 0;
    mFbInfo.bpp = 0;
    mFbInfo.pitch = 0;
    mFbInfo.is_graphics = 0;
    mIsGraphicsMode = false;

    return 0;
}

/* Set DAC palette register for 256-color mode */
void DrawDev::SetPalette(uint8_t index, const vga_palette* color)
{
    /* Write palette index to DAC Write Address Register */
    arch_outb(0x3C8, index);

    /* Write RGB values to DAC Data Register */
    arch_outb(0x3C9, color->r & 0x3F);
    arch_outb(0x3C9, color->g & 0x3F);
    arch_outb(0x3C9, color->b & 0x3F);
}

/* Set a pixel in graphics mode */
void DrawDev::SetPixel(int32_t x, int32_t y, uint8_t color)
{
    if (!mIsGraphicsMode)
        return;

    if (x < 0 || x >= (int32_t)mFbInfo.width || y < 0 || y >= (int32_t)mFbInfo.height)
        return;

    /* In mode 13h with chain-4 enabled, pixel offset = y * 320 + x */
    uint32_t offset = (uint32_t)(y * VGA_FB_WIDTH + x);
    mFbInfo.framebuffer[offset] = color;
}

int DrawDev::Init(void)
{
    return 0;
}

int DrawDev::Read(char* buf, size_t size)
{
    (void)buf;
    (void)size;
    return 0;
}

int DrawDev::Write(const char* buf, size_t size)
{
    (void)buf;
    (void)size;
    return 0;
}

int DrawDev::Shutdown(void)
{
    /* Return to text mode on shutdown */
    if (mIsGraphicsMode)
        SwitchToTextMode();
    return 0;
}

int DrawDev::Ctrl(int cmd, void* arg)
{
    switch (cmd) {
    case GRAPHIC_MODE:
        return SwitchToGraphicsMode();
    case CHAR_MODE:
        return SwitchToTextMode();
    case GET_FB_INFO: {
        if (!arg)
            return -1;
        vga_fb_info* info = (vga_fb_info*)arg;
        *info = mFbInfo;
        return 0;
    }
    case SET_PIXEL: {
        if (!arg || !mIsGraphicsMode)
            return -1;
        pixel_cmd* pc = (pixel_cmd*)arg;
        SetPixel(pc->x, pc->y, pc->color);
        return 0;
    }
    case SET_PALETTE: {
        if (!arg)
            return -1;
        palette_cmd* pal = (palette_cmd*)arg;
        SetPalette(pal->index, &pal->color);
        return 0;
    }
    }

    return -1;
}

extern "C" {

int drawdev_init(iodev **out_dev)
{
    if (!out_dev)
        return -1;

    iodev* dev = nullptr;
    DrawDev* instance = DrawDev::GetInstance();

    io_alloc_dev("draw_dev", &dev);
    if (dev) {
        instance->_bind_c_interface(dev);
    }

    *out_dev = dev;
    return (dev != nullptr) ? 0 : -1;
}

}