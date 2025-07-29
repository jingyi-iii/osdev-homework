#include "terminal.h"

void Terminal::Flush(void)
{
	mCurrRow = 0;
	mCurrCol = 0;
	mCurrColor = ToVgaColor(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

	for (size_t y = 0; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			const size_t index = y * VGA_WIDTH + x;
			mpVgaBuffer[index] = ToVgaChar(' ', mCurrColor);
		}
	}
}

void Terminal::WriteAt(char chr, uint8_t color, size_t x, size_t y) 
{
	if (x >= VGA_WIDTH || y >= VGA_HEIGHT)
		return;

	const size_t index = y * VGA_WIDTH + x;
	mpVgaBuffer[index] = ToVgaChar(chr, color);

	mCurrCol = x;
	mCurrRow = y;
	mCurrColor = color;
	if (++mCurrCol >= VGA_WIDTH) {
		mCurrCol = 0;
		if (++mCurrRow >= VGA_HEIGHT) {
			mCurrRow = 0;
		}
	}
}

void Terminal::WriteAt(const char* str, uint8_t color, size_t x, size_t y)
{
	if (color)
		mCurrColor = color;

	while (*str != '\0') {
		WriteAt(*str, mCurrColor, x, y);
		str += 1;
		if (++x >= VGA_WIDTH) {
			x = 0;
			if (++y >= VGA_HEIGHT) {
				y = 0;
			}
		}
	}
}

void Terminal::Write(const char* str)
{
	WriteAt(str, mCurrColor, mCurrCol, mCurrRow);
}

