#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVADA_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVADA_H

#include "include/nvx/base.h"

#define NVADA_ADDR2 0x1000u

typedef struct {
    u8* img; u32 len;
    u32 imem_off, imem_base, imem_size, dmem_off, dmem_base, dmem_size, dmem_sign, boot_addr;
    u32 fuse_ver, engine_id, ucode_id, sig_loc, sig_size, sig_cnt, if_off;
    const u8* sigs;
} nvada_fw;

typedef struct {
    const char* name;
    u32 gsp, sec2;
    const char* sig_section;
    int (*flcn_reset)(u32 f);
    int (*fw_load)(u32 f,const nvada_fw* fw);
    int (*fw_boot)(u32 f,const nvada_fw* fw,u32 mb0,u32 mb1,u32* o0,u32* o1);
    int (*booter_sig)(const nvada_fw* fw);
    int (*fwsec_sig)(const nvada_fw* fw);
    int (*gsp_reset)(void);
    int (*riscv_active)(u32 f);
} nvada_hooks;

extern int g_nv_ada_on;
extern const nvada_hooks g_nvada_hooks;
struct nvgsp_gen;
extern const struct nvgsp_gen nvgsp_gen_ad10x;

const char* nvada_chip(u32 boot0);
int nvada_booter_parse(const u8* f,u32 len,nvada_fw* o);
int nvada_fwsec_parse(const u8* rom,u32 rom_len,u32 desc,nvada_fw* o);
int nvada_fwsec_patch(nvada_fw* fw,u32 cmd,u64 frts_addr,u64 frts_size);
int nvada_sign(nvada_fw* fw,int idx);
int nvada_booter_run(u64 wpr_meta_pa,int unload);
int nvb_rom_get(const u8** rom,u32* len,u32* img0,u32* imgd);
int nvada_elf_has(const u8* f,u32 n,const char* name,u32* size);
int nvada_boot(void);
int nvada_cmd(const char* a);

#endif
