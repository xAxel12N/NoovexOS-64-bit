#include "vga.h"
#include "serial.h"
#include "fb.h"

static volatile uint16_t* const VMEM = (volatile uint16_t*)0xB8000;
static int cx = 0, cy = 0;
static uint8_t color = 0x07;
static int g_graphics = 0;
void vga_set_graphics(int on){ g_graphics = on; }
#define COLS 80
#define ROWS 25

static void update_cursor(void){
    uint16_t pos = (uint16_t)(cy * COLS + cx);
    __asm__ volatile("outb %0,%1"::"a"((uint8_t)0x0F),"Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0,%1"::"a"((uint8_t)(pos & 0xFF)),"Nd"((uint16_t)0x3D5));
    __asm__ volatile("outb %0,%1"::"a"((uint8_t)0x0E),"Nd"((uint16_t)0x3D4));
    __asm__ volatile("outb %0,%1"::"a"((uint8_t)((pos >> 8) & 0xFF)),"Nd"((uint16_t)0x3D5));
}

void vga_setcolor(uint8_t fg, uint8_t bg){ if(g_graphics){ fb_setcolor(fg, bg); return; } color = (uint8_t)((bg << 4) | (fg & 0x0F)); }

void vga_clear(void){
    if(g_graphics){ fb_clear(); return; }
    for(int i = 0; i < COLS * ROWS; i++) VMEM[i] = (uint16_t)((color << 8) | ' ');
    cx = 0; cy = 0; update_cursor();
}

void vga_init(void){ vga_setcolor(VGA_LGRAY, VGA_BLACK); vga_clear(); }

static void scroll(void){
    for(int i = 0; i < COLS * (ROWS - 1); i++) VMEM[i] = VMEM[i + COLS];
    for(int i = COLS * (ROWS - 1); i < COLS * ROWS; i++) VMEM[i] = (uint16_t)((color << 8) | ' ');
    cy = ROWS - 1;
}

void vga_putc(char c){
    serial_putc(c);
    if(g_graphics){ fb_putc(c); return; }
    if(c == '\n'){ cx = 0; cy++; }
    else if(c == '\r'){ cx = 0; }
    else if(c == '\t'){ cx = (cx + 4) & ~3; }
    else if(c == '\b'){ if(cx > 0) cx--; VMEM[cy * COLS + cx] = (uint16_t)((color << 8) | ' '); }
    else {
        VMEM[cy * COLS + cx] = (uint16_t)((color << 8) | (uint8_t)c);
        cx++;
    }
    if(cx >= COLS){ cx = 0; cy++; }
    if(cy >= ROWS) scroll();
    update_cursor();
}

void vga_puts(const char* s){ while(*s) vga_putc(*s++); }

void vga_puthex(uint32_t v){
    const char* d = "0123456789ABCDEF";
    vga_puts("0x");
    for(int i = 28; i >= 0; i -= 4) vga_putc(d[(v >> i) & 0xF]);
}

void vga_putdec(uint32_t v){
    char buf[12];
    int i = 0;
    if(v == 0){ vga_putc('0'); return; }
    while(v){ buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while(i--) vga_putc(buf[i]);
}

void vga_put_at(int row, int col, const char* s, uint8_t attr){
    if(g_graphics){ fb_put_at(row, col, s, attr); return; }
    int p = row * COLS + col;
    while(*s && p < COLS * ROWS){ VMEM[p++] = (uint16_t)((attr << 8) | (uint8_t)*s++); }
}

void vga_fill_row(int row, uint8_t attr){
    if(g_graphics){ fb_fill_row(row, attr); return; }
    for(int i = 0; i < COLS; i++) VMEM[row * COLS + i] = (uint16_t)((attr << 8) | ' ');
}

int vga_cols(void){ return g_graphics ? fb_get_cols() : COLS; }
int vga_rows(void){ return g_graphics ? fb_get_rows() : ROWS; }
