#ifndef VGA_H
#define VGA_H
#include "types.h"
void vga_init(void);
void vga_set_graphics(int on);
void vga_clear(void);
void vga_setcolor(uint8_t fg, uint8_t bg);
void vga_putc(char c);
void vga_puts(const char* s);
void vga_puthex(uint32_t v);
void vga_putdec(uint32_t v);
void vga_put_at(int row, int col, const char* s, uint8_t attr);
void vga_fill_row(int row, uint8_t attr);
int vga_cols(void);
int vga_rows(void);
#define VGA_BLACK 0
#define VGA_BLUE 1
#define VGA_GREEN 2
#define VGA_CYAN 3
#define VGA_RED 4
#define VGA_MAGENTA 5
#define VGA_BROWN 6
#define VGA_LGRAY 7
#define VGA_DGRAY 8
#define VGA_LBLUE 9
#define VGA_LGREEN 10
#define VGA_WHITE 15
#endif
