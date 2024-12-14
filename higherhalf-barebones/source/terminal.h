#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum VgaColor {
	VGA_COLOR_BLACK 		= 0,
	VGA_COLOR_BLUE 			= 1,
	VGA_COLOR_GREEN 		= 2,
	VGA_COLOR_CYAN 			= 3,
	VGA_COLOR_RED 			= 4,
	VGA_COLOR_MAGENTA 		= 5,
	VGA_COLOR_BROWN 		= 6,
	VGA_COLOR_LIGHT_GREY 	= 7,
	VGA_COLOR_DARK_GREY 	= 8,
	VGA_COLOR_LIGHT_BLUE 	= 9,
	VGA_COLOR_LIGHT_GREEN 	= 10,
	VGA_COLOR_LIGHT_CYAN 	= 11,
	VGA_COLOR_LIGHT_RED 	= 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN 	= 14,
	VGA_COLOR_WHITE 		= 15,
};

static inline uint8_t ToVgaColor(enum VgaColor fg, enum VgaColor bg) 
{
	return fg | bg << 4;
}

static inline uint16_t ToVgaChar(uint8_t chr, uint8_t color) 
{
	return (uint16_t)chr | (uint16_t) color << 8;
}

class Terminal {
public:
	void Flush(void); 
	void WriteAt(char chr, uint8_t color, size_t x, size_t y);
	void WriteAt(const char* str, uint8_t color, size_t x, size_t y);
	void Write(const char* str);

private:
	const size_t VGA_WIDTH = 80;
	const size_t VGA_HEIGHT = 25;
	uint16_t* const mpVgaBuffer = (uint16_t*)0x007FE000;
	size_t mCurrRow = 0;
	size_t mCurrCol = 0;
	uint8_t mCurrColor = 0;
};

