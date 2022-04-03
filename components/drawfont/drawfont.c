#include <stdint.h>
#include <string.h>

//#include "SmallFont.h"
#include "Arial_round_16x24.h"

const uint8_t COLOR_BLACK = 0x00;
const uint8_t COLOR_WHITE = 0xff;

typedef struct  {
	uint8_t w;
	uint8_t h;
	uint8_t offset;
	uint8_t cnt;
	unsigned char *font;
} Font;

Font cfont;

void init_font(void)
{
	cfont.w = Arial_round_16x24[0];
	cfont.h = Arial_round_16x24[1];
	cfont.offset = Arial_round_16x24[2];
	cfont.cnt = Arial_round_16x24[3];
	cfont.font = &(Arial_round_16x24[4]);
}

void draw_string(uint8_t * buf, int w, int h, int x, int y, const char *str, uint8_t color)
{
	if (cfont.font == NULL) {
		return;
	}

	int length = strlen(str);
	buf = buf + x + (w * y);
	
	for (int pos = 0; pos < length; ++pos) {
		char letter = str[pos];
		unsigned char* buf_font = cfont.font + ((letter - cfont.offset) * ((cfont.w * cfont.h) >> 3));
		uint8_t* buf_draw = buf + (pos * cfont.w);
		
		for (int row = 0; row < cfont.h; ++row) {
			uint8_t *buf_draw_char = buf_draw;

			for (int col = 0; col < (cfont.w >> 3) ; ++col) {
				for (int pixelpos = 0; pixelpos < 8; pixelpos++) {
					if ((*buf_font) & (1 << (7 - pixelpos))) {
						*buf_draw_char = color;
					}
					buf_draw_char += 1;
				}
				buf_font += 1;				
			}
			
			buf_draw += w;
		}
	}
}

void draw_string_with_border(uint8_t* buf, int width, int height, int xpos, int ypos, const char* str, int thickness)
{
	draw_string(buf, width, height, xpos - thickness, ypos, str, COLOR_BLACK);
    draw_string(buf, width, height, xpos + thickness, ypos, str, COLOR_BLACK);
    draw_string(buf, width, height, xpos, ypos - thickness, str, COLOR_BLACK);
    draw_string(buf, width, height, xpos, ypos + thickness, str, COLOR_BLACK);
    draw_string(buf, width, height, xpos, ypos, str, COLOR_WHITE);
}