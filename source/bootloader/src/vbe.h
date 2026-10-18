#ifndef VBE_H
#define VBE_H
#include "types.h"
struct fb_info {
    uint32_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t red_pos;
    uint8_t red_size;
    uint8_t green_pos;
    uint8_t green_size;
    uint8_t blue_pos;
    uint8_t blue_size;
};
int vbe_set_mode(uint16_t w, uint16_t h, struct fb_info* out);
int vbe_edid_native(uint16_t* w, uint16_t* h);
#endif
