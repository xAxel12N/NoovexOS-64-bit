#ifndef NVX_AMD_DC_STATE_H
#define NVX_AMD_DC_STATE_H
#include "include/nvx/base.h"
#define AMD_DC_MAXP 4
typedef struct { int en; u32 htot, vtot, hbs, hbe, vbs, vbe, odm_src; } amd_otg_t;
typedef struct { int blank; u32 w, h, pitch, fmt; u64 addr; } amd_hubp_t;
typedef struct { int en; u32 fe_src, be_fe, mode; } amd_dig_t;
typedef struct { int ok, nump, numdig; u32 ver, seg2; amd_otg_t otg[AMD_DC_MAXP]; amd_hubp_t hubp[AMD_DC_MAXP]; amd_dig_t dig[5]; int edid_aux, edid_len; u8 edid[256]; } amd_dc_t;
extern amd_dc_t g_amd_dc;
int amd_dc_scan(void);
int amd_dc_aux_i2c_read(int aux,u8 addr,u8 off,u8* buf,int len);
int amd_dc_edid(u8* out,int max);
int amd_dc_info(char* buf,int n);
#endif
