#ifndef FB_H
#define FB_H
#include "types.h"
struct fb_info;
void fb_init(const struct fb_info* fb);
void fb_setcolor(uint8_t fg, uint8_t bg);
void fb_clear(void);
void fb_putc(char c);
void fb_put_at(int row, int col, const char* s, uint8_t attr);
void fb_fill_row(int row, uint8_t attr);
int fb_get_cols(void);
int fb_get_rows(void);
#endif
