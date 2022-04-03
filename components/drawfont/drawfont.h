#include <stdint.h>

void init_font(void);
void draw_string(uint8_t* buf, int w, int h, int x, int y, const char* str, uint8_t color);
void draw_string_with_border(uint8_t* buf, int width, int height, int xpos, int ypos, const char* str, int thickness);