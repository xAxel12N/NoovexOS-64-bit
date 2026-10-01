#include "fb.h"
#include "vbe.h"
#include "string.h"

static volatile uint8_t* fb_base;
static uint32_t fb_pitch, fb_w, fb_h, fb_bpp_bytes;
static int fb_cols, fb_rows, fb_cx, fb_cy;
static uint8_t rpos, rsize, gpos, gsize, bpos, bsize;
static uint8_t cur_fg = 7, cur_bg = 0;
static const uint8_t* const font = (const uint8_t*)0x5000;

static const uint32_t pal[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

static uint32_t pack(uint8_t idx){
    uint32_t rgb = pal[idx & 15];
    uint32_t r = (rgb >> 16) & 0xFF;
    uint32_t g = (rgb >> 8) & 0xFF;
    uint32_t b = rgb & 0xFF;
    return ((r >> (8 - rsize)) << rpos) | ((g >> (8 - gsize)) << gpos) | ((b >> (8 - bsize)) << bpos);
}

static void put_span(uint32_t x, uint32_t y, uint32_t w, uint32_t pixel){
    volatile uint8_t* row = fb_base + y * fb_pitch + x * fb_bpp_bytes;
    if(fb_bpp_bytes == 4){
        volatile uint32_t* p = (volatile uint32_t*)row;
        for(uint32_t i = 0; i < w; i++) p[i] = pixel;
    } else {
        for(uint32_t i = 0; i < w; i++){
            row[i * 3] = (uint8_t)pixel;
            row[i * 3 + 1] = (uint8_t)(pixel >> 8);
            row[i * 3 + 2] = (uint8_t)(pixel >> 16);
        }
    }
}

static void fillrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t pixel){
    for(uint32_t j = 0; j < h; j++) put_span(x, y + j, w, pixel);
}

static void glyph(int px, int py, uint8_t ch, uint32_t fg, uint32_t bg){
    const uint8_t* g = font + (uint32_t)ch * 16;
    for(int r = 0; r < 16; r++){
        uint8_t bits = g[r];
        volatile uint8_t* row = fb_base + (uint32_t)(py + r) * fb_pitch + (uint32_t)px * fb_bpp_bytes;
        for(int c = 0; c < 8; c++){
            uint32_t pix = (bits & (0x80 >> c)) ? fg : bg;
            if(fb_bpp_bytes == 4){
                ((volatile uint32_t*)row)[c] = pix;
            } else {
                row[c * 3] = (uint8_t)pix;
                row[c * 3 + 1] = (uint8_t)(pix >> 8);
                row[c * 3 + 2] = (uint8_t)(pix >> 16);
            }
        }
    }
}

void fb_init(const struct fb_info* fb){
    fb_base = (volatile uint8_t*)fb->addr;
    fb_pitch = fb->pitch;
    fb_w = fb->width;
    fb_h = fb->height;
    fb_bpp_bytes = fb->bpp / 8;
    rpos = fb->red_pos; rsize = fb->red_size;
    gpos = fb->green_pos; gsize = fb->green_size;
    bpos = fb->blue_pos; bsize = fb->blue_size;
    fb_cols = (int)(fb_w / 8);
    fb_rows = (int)(fb_h / 16);
    fb_cx = fb_cy = 0;
    cur_fg = 7; cur_bg = 0;
    fb_clear();
}

void fb_setcolor(uint8_t fg, uint8_t bg){ cur_fg = fg; cur_bg = bg; }

void fb_clear(void){
    fillrect(0, 0, fb_w, fb_h, pack(cur_bg));
    fb_cx = fb_cy = 0;
}

static void scroll(void){
    memmove((void*)fb_base, (void*)(fb_base + 16 * fb_pitch), (fb_h - 16) * fb_pitch);
    fillrect(0, fb_h - 16, fb_w, 16, pack(cur_bg));
}

void fb_putc(char c){
    if(c == '\n'){ fb_cx = 0; fb_cy++; }
    else if(c == '\r'){ fb_cx = 0; }
    else if(c == '\t'){ fb_cx = (fb_cx + 4) & ~3; }
    else if(c == '\b'){ if(fb_cx > 0){ fb_cx--; glyph(fb_cx * 8, fb_cy * 16, ' ', pack(cur_fg), pack(cur_bg)); } }
    else { glyph(fb_cx * 8, fb_cy * 16, (uint8_t)c, pack(cur_fg), pack(cur_bg)); fb_cx++; }
    if(fb_cx >= fb_cols){ fb_cx = 0; fb_cy++; }
    if(fb_cy >= fb_rows){ scroll(); fb_cy = fb_rows - 1; }
}

void fb_put_at(int row, int col, const char* s, uint8_t attr){
    uint32_t fg = pack(attr & 0x0F);
    uint32_t bg = pack((attr >> 4) & 0x0F);
    int px = col * 8, py = row * 16;
    while(*s){ glyph(px, py, (uint8_t)*s++, fg, bg); px += 8; }
}

void fb_fill_row(int row, uint8_t attr){
    fillrect(0, (uint32_t)(row * 16), fb_w, 16, pack((attr >> 4) & 0x0F));
}

int fb_get_cols(void){ return fb_cols; }
int fb_get_rows(void){ return fb_rows; }
