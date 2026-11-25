#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVGSP_GEN_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVGSP_GEN_H

#include "include/nvx/base.h"

typedef struct {
    const u8* img; u64 pa;
    u32 imem_off, imem_base, imem_size, imem_sec;
    u32 dmem_off, dmem_base, dmem_size;
    u32 bootvec, dmem_sign, engine_id, ucode_id, fuse_ver;
} nvgsp_ldr;

typedef struct nvgsp_cls {
    u32 chan, user, ce, twod, threed, compute;
    u32 disp, core, wndw, wimm, curs;
    u32 doorbell, wndw_storage_pitch;
    u8 qmd_major, sm;
} nvgsp_cls;

typedef struct nvgsp_gen {
    const char* name;
    u16 chip_lo, chip_hi;
    int untested;
    const char* sig_section;
    const char* fw_dir;
    const char* booter_load, *booter_unload, *bootloader, *gsp;
    u64 frts_size, heap_os, heap_min;
    u32 gsp_cap;
    int (*flcn_reset)(u32 f);
    int (*flcn_load)(u32 f,const nvgsp_ldr* l);
    int (*flcn_boot)(u32 f,const nvgsp_ldr* l,u32 mb0,u32 mb1,u32* o0,u32* o1,u32 ms);
    int (*booter_sig)(u32 cnt,const nvgsp_ldr* l);
    int (*fwsec_sig)(u32 cnt,const nvgsp_ldr* l);
    int (*gsp_reset)(void);
    int (*riscv_active)(void);
    const nvgsp_cls* cls;
    const u8* blob[8];
} nvgsp_gen;

#define NVGSP_ADDR2_GA102 0x1000u
#define NVGSP_PCI_GA102_LO 0x2200u
#define NVGSP_PCI_GA107_HI 0x25FFu
#define NVGSP_TOP_SEC2 0x0Du
#define NVGSP_PMC_ENABLE 0x000600u
#define NVGSP_PCI_AD10X_LO 0x2680u
#define NVGSP_PCI_AD10X_HI 0x28FFu

extern const nvgsp_gen nvgsp_gen_tu10x, nvgsp_gen_tu11x, nvgsp_gen_ga10x;
extern const nvgsp_cls nvgsp_cls_tu10x, nvgsp_cls_ga10x, nvgsp_cls_ad10x;
extern int g_nv_ampere_on;
extern const nvgsp_gen* g_nvgen;

const nvgsp_gen* nvgsp_gen_for(u32 chip);
int nvgsp_gen_pci_ok(u16 dev);
int nvgsp_gen_fuse_idx(u32 reg_fuse,u32 sig_fuse,u32 cnt);
int nvgsp_gen_ga102_reset(u32 f);
int nvgsp_gen_ga102_load(u32 f,const nvgsp_ldr* l);
int nvgsp_gen_ga102_boot(u32 f,const nvgsp_ldr* l,u32 mb0,u32 mb1,u32* o0,u32* o1,u32 ms);
int nvgsp_gen_ga102_booter_sig(u32 cnt,const nvgsp_ldr* l);
int nvgsp_gen_ga102_fwsec_sig(u32 cnt,const nvgsp_ldr* l);
int nvgsp_gen_ga102_gsp_reset(void);
int nvgsp_gen_ga102_riscv_active(void);
int nvgsp_gen_hooked(void);
const nvgsp_cls* nvgsp_cls_cur(void);
u32 nvgsp_gen_top_reset(u32 type);
int nvgsp_gen_ga102_pmc(u32 mask,int on);
u32 nvb_fwsec_desc(void);
const u8* nvb_rom(u32* len);
const char* nvd_ampere_cmd(const char* arg);
int nvd_ampere(void);

#endif
