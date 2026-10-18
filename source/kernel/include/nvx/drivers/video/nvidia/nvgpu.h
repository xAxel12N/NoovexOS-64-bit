#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVGPU_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVGPU_H

#include "include/nvx/base.h"

typedef struct { u32 pclk_khz; u16 hact,hfp,hsync,hbl; u16 vact,vfp,vsync,vbl; int hpol,vpol; } nv_mode;

extern int g_nv_bus,g_nv_slot,g_nv_fn;
extern u16 g_nv_dev;
extern u64 g_nv_bar0 , g_nv_bar1 , g_nv_bar3;
extern u32 g_nv_bar0_sz;
extern u32 g_nv_boot0;
extern int g_nv_arch , g_nv_impl , g_nv_rev;
extern u8 g_nv_vbios[65536];
extern int g_nv_vbios_len;
extern u32 g_nv_bit_off , g_nv_dcb_off;
extern int g_nv_ncon;
extern u8 g_nv_con_type[16], g_nv_con_or[16], g_nv_con_heads[16];

u32 nvr32(u32 off);
int nv_probe(void);
const char* nv_arch_name2(u32 boot0);
void nv_read_id(void);
int nv_load_vbios(void);
int nv_find_bit(void);
int nv_find_dcb(void);
void nv_parse_dcb(void);
const char* nv_con_name(u8 t);

#endif
