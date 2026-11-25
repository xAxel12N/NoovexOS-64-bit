#ifndef NVX_DRIVERS_VIDEO_AMD_ATOM_H
#define NVX_DRIVERS_VIDEO_AMD_ATOM_H

#include "include/nvx/base.h"

#define AMD_ATOM_MAXCON 8
#define AMD_VBIOS_MAX (256u*1024u)

typedef struct { u16 objid, enc, extenc, devtag; u8 type, enumid, prio, encid; } amd_atom_con_t;

typedef struct {
    int ok, vram_copy;
    u16 rom_hdr, cmd_tbl, data_tbl, fwi_off, isi_off, obj_off, lcd_off, smu_off;
    u8 rom_frev, rom_crev, fwi_frev, fwi_crev, isi_frev, isi_crev, obj_frev, obj_crev;
    u16 ssvid, ssdid;
    u32 fw_rev, boot_sclk_10khz, boot_mclk_10khz, fw_cap, scratch_addr;
    u16 vddc_mv, vddgfx_mv;
    u32 isi_vbios_misc, isi_gpucap, isi_syscfg;
    u8 memtype, umach, htc_lim, htc_hyst, edp_bpc, edp_boot_bl, min_bl;
    u16 bl_pwm_hz;
    int ncon;
    amd_atom_con_t con[AMD_ATOM_MAXCON];
    char pn[44], ver[33], date[17], why[48];
} amd_atom_t;

extern amd_atom_t g_amd_atom;

int amd_atom_check(const u8* b,u32 len);
u32 amd_vfct_find(const u8* t,u32 tlen,u8 bus,u8 slot,u8 fn,u16 ven,u16 did,u32* olen);
int amd_atom_parse(const u8* b,u32 len,amd_atom_t* a);
const char* amd_atom_con_name(u8 type);
const char* amd_atom_mem_name(u8 t);
int amd_atom_describe(const amd_atom_t* a,char* buf,int n);
int amd_atom_info(char* buf,int n);
int amd_atom_table(const char* name,u32* off,u8* frev,u8* crev,u16* size);

#endif
