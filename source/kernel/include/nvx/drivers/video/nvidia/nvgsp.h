#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVGSP_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVGSP_H

#include "include/nvx/base.h"

#define NVF_GSP  0x110000u
#define NVF_SEC2 0x840000u

typedef struct {
    u64 fb_size, vga_addr, vga_size, frts_addr, frts_size, boot_addr, boot_size, elf_addr, elf_size;
    u64 heap_addr, heap_size, wpr2_addr, wpr2_size, nonwpr_addr, nonwpr_size;
} nvfb_layout;

typedef struct {
    u32 imem_base, imem_size, imem_sec_base, imem_sec_size, dmem_off, dmem_base, dmem_size, if_off, total;
    u8* buf;
} nvfwsec;

extern nvfb_layout g_nvl;
extern char g_nv_log[];
extern int g_nv_log_n;
extern int g_nv_step;
extern char g_nv_why[160];

void nvw32(u32 off,u32 v);
void nvm32(u32 off,u32 mask,u32 v);
int nv_poll(u32 off,u32 mask,u32 val,u32 ms);

void nvl_s(const char* s);
void nvl_x(u64 v);
void nvl_d(u64 v);
void nvl_nl(void);
void nvl_kv(const char* k,u64 v);
void nvl_reset(void);
int nv_fail(int step,const char* why);

int nv_gfw_wait(void);
u64 nv_vram_size(void);
u64 nv_vga_workspace(u64 fb);
u64 nv_wpr_heap_size(u64 fb);
int nv_fb_layout(u64 elf_size,u64 boot_size);

u32 nvp_rd32(u64 vram);
void nvp_wr32(u64 vram,u32 v);
void nvp_wr64(u64 vram,u64 v);
u64 nvp_rd64(u64 vram);
void nvp_fill(u64 vram,u64 len,u32 v);

int nvv_init(u64 lo,u64 hi);
void nvv_reserve(u64 lo,u64 hi);
u64 nvv_alloc(u64 size,u64 align);
void nvv_free(u64 addr);
u64 nvv_free_bytes(void);
u64 nvv_total(void);
int nvv_count(void);

typedef struct { u64 pd3; u64 limit; int ok; } nvvmm;
int nvm_vmm_new(nvvmm* m,u64 limit);
int nvm_map(nvvmm* m,u64 va,u64 pa,u64 len,int sysmem,int priv,int ro);
u64 nvm_pd3_entry0(nvvmm* m);
int nvm_inst_join(nvvmm* m,u64 inst);
int nvm_tlb_flush(u64 pdb,int bar);

int nvf_reset(u32 f);
void nvf_imem(u32 f,u32 dst,const u8* src,u32 len,u32 tag,int secure);
void nvf_dmem(u32 f,u32 dst,const u8* src,u32 len);
int nvf_boot(u32 f,u32 bootvec,u32 mb0,u32 mb1,u32* o0,u32* o1,u32 ms);
void nvf_dump(u32 f,const char* name);
int nvb_read_rom(void);
int nvb_find_fwsec(nvfwsec* fw);
int nv_run_fwsec(u8* bl,u32 bl_len,u32 cmd);
int nv_booter(const u8* file,u32 len,u64 wpr_meta_pa,int unload);

int nvgsp_cmd(const char* arg);
int nvgsp_ce_ready(void);
u64 nvgsp_ce_map(u64 kva,u64 len);
int nvgsp_ce_copy2d(u64 src,u32 spitch,u64 dst,u32 dpitch,u32 wbytes,u32 lines);
void nvgsp_ce_stats(u32* copies,u32* timeouts);
const char* nvgsp_gpu_name(void);
void nvgsp_status(void);

#endif
