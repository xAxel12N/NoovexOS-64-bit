#ifndef NVX_DRIVERS_VIDEO_FB_EDID_H
#define NVX_DRIVERS_VIDEO_FB_EDID_H

#include "include/nvx/base.h"

typedef struct {
    int ok, digital, iface, bits, src;
    char maker[4], name[16], serial[16];
    u16 product, year;
    u8 ver, rev, cm_w, cm_h, ext;
    u32 nat_w, nat_h, nat_mhz, best_mhz, range_lo, range_hi;
} mon_edid_t;

int edid_parse(const u8* e, mon_edid_t* m);
void edid_boot_capture(void);
const mon_edid_t* mon_get(void);
const u8* mon_raw(void);
int mon_cur_mhz(u32 w, u32 h);
int mon_max_hz(void);
const char* mon_iface_name(void);

#endif
