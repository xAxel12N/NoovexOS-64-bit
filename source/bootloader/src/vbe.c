#include "vbe.h"

extern void bios_int10(void);

struct rmregs {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint16_t es;
} __attribute__((packed));

#define VBE_INFO   0x4000u
#define MODE_INFO  0x4200u
#define RMREGS     0x4300u

static uint16_t rd16(const uint8_t* p){ return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

int vbe_set_mode(uint16_t w, uint16_t h, struct fb_info* out){
    volatile struct rmregs* r = (volatile struct rmregs*)RMREGS;
    uint8_t* vinfo = (uint8_t*)VBE_INFO;
    uint8_t* minfo = (uint8_t*)MODE_INFO;

    vinfo[0] = 'V'; vinfo[1] = 'B'; vinfo[2] = 'E'; vinfo[3] = '2';
    r->eax = 0x4F00; r->ebx = 0; r->ecx = 0; r->edx = 0; r->esi = 0;
    r->edi = VBE_INFO; r->es = 0;
    bios_int10();
    if((r->eax & 0xFFFF) != 0x004F) return -1;

    uint32_t mp = rd32(vinfo + 14);
    uint32_t list = ((mp >> 16) * 16) + (mp & 0xFFFF);
    uint16_t* modes = (uint16_t*)list;

    int best = -1;
    for(int i = 0; i < 1024 && modes[i] != 0xFFFF; i++){
        uint16_t m = modes[i];
        r->eax = 0x4F01; r->ecx = m; r->edi = MODE_INFO; r->es = 0;
        bios_int10();
        if((r->eax & 0xFFFF) != 0x004F) continue;
        uint16_t attr = rd16(minfo + 0);
        if(!(attr & 0x01)) continue;
        if(!(attr & 0x10)) continue;
        if(!(attr & 0x80)) continue;
        if(minfo[27] != 6) continue;
        uint16_t xr = rd16(minfo + 18);
        uint16_t yr = rd16(minfo + 20);
        uint8_t bpp = minfo[25];
        if(xr == w && yr == h && (bpp == 32 || bpp == 24)){
            best = m;
            out->addr = rd32(minfo + 40);
            out->pitch = rd16(minfo + 16);
            out->width = xr;
            out->height = yr;
            out->bpp = bpp;
            out->red_size = minfo[31];
            out->red_pos = minfo[32];
            out->green_size = minfo[33];
            out->green_pos = minfo[34];
            out->blue_size = minfo[35];
            out->blue_pos = minfo[36];
            if(bpp == 32) break;
        }
    }
    if(best < 0) return -1;

    r->eax = 0x4F02; r->ebx = (uint32_t)best | 0x4000;
    r->edi = 0; r->es = 0;
    bios_int10();
    if((r->eax & 0xFFFF) != 0x004F) return -1;
    return 0;
}

#define EDID_BUF 0x4400u
int vbe_edid_native(uint16_t* w, uint16_t* h){
    volatile struct rmregs* r = (volatile struct rmregs*)RMREGS;
    uint8_t* e = (uint8_t*)EDID_BUF;
    for(int i = 0; i < 128; i++) e[i] = 0;
    r->eax = 0x4F15; r->ebx = 0; r->ecx = 0; r->edx = 0; r->esi = 0; r->edi = 0; r->es = 0;
    bios_int10();
    if((r->eax & 0xFFFF) != 0x004F) return -1;
    r->eax = 0x4F15; r->ebx = 1; r->ecx = 0; r->edx = 0; r->esi = 0; r->edi = EDID_BUF; r->es = 0;
    bios_int10();
    if((r->eax & 0xFFFF) != 0x004F) return -2;
    if(!(e[0] == 0x00 && e[1] == 0xFF && e[2] == 0xFF && e[7] == 0x00)) return -3;
    uint8_t sum = 0; for(int i = 0; i < 128; i++) sum = (uint8_t)(sum + e[i]);
    if(sum != 0) return -4;
    const uint8_t* d = e + 54;
    if(d[0] == 0 && d[1] == 0) return -5;
    uint16_t hw = (uint16_t)(d[2] | ((d[4] & 0xF0) << 4));
    uint16_t vh = (uint16_t)(d[5] | ((d[7] & 0xF0) << 4));
    if(hw < 640 || vh < 400) return -6;
    *w = hw; *h = vh; return 0;
}
