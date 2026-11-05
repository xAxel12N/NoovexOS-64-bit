#ifndef NVX_DRIVERS_VIDEO_NVIDIA_NVGSP_H
#define NVX_DRIVERS_VIDEO_NVIDIA_NVGSP_H

#include "include/nvx/base.h"
#include "include/nvx/drivers/video/fb/damage.h"

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
u64 nvv_alloc_raw(u64 size,u64 align);
void nvv_free(u64 addr);
u64 nvv_free_bytes(void);
u64 nvv_total(void);
int nvv_count(void);

typedef struct { u64 pd3; u64 limit; int ok; } nvvmm;
int nvm_vmm_new(nvvmm* m,u64 limit);
int nvm_map(nvvmm* m,u64 va,u64 pa,u64 len,int sysmem,int priv,int ro);
int nvm_unmap(nvvmm* m,u64 va,u64 len);
void nvgsp_ce_unmap(u64 va,u64 len);
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
int nvgsp_ce_fill(u64 dst,u32 dpitch,u32 wpx,u32 lines,u32 val);
u32 nvgsp_ce_gen(void);
void nvgsp_ce_stats(u32* copies,u32* timeouts);
int nvgsp_present_on(void);
int nvgsp_fb_present(const u32* scene,u32 sw,u32 w,u32 h,volatile u32* fb,u32 pitchb);
int nvgsp_fb_present_rects(const u32* scene,u32 sw,u32 w,u32 h,const dmg_rect* r,int n,volatile u32* fb,u32 pitchb);
const char* nvgsp_gpu_name(void);
void nvgsp_status(void);

typedef struct { u64 va; u32 pitch, w, h, fmt; int vram; } nv2d_surf;
#define NV2D_COPY 256u
#define NV2D_OVER 257u
int nv2d_ready(void);
int nv2d_fill(const nv2d_surf* d,int x,int y,int w,int h,u32 c,u32 alpha);
int nv2d_blit(const nv2d_surf* d,int dx,int dy,int dw,int dh,const nv2d_surf* s,int sx,int sy,int sw,int sh,u32 alpha);
int nv2d_screen(nv2d_surf* out);
int nv2d_to_screen(const u32* src,u32 spitch_px,int sw,int sh,int dx,int dy,int scale,u64* src_va,const void** src_ptr);
void nv2d_stats(u32* ops,u32* subs,u32* timeouts,u32* frames);
void nv2d_reset(void);

typedef struct { const u8* code; u32 code_len, regs, bars, smem, pofs, psz; const u8* cb[8]; u32 cbl[8]; int ok; char why[64]; } nvkern;
typedef struct { u64 va; u32 pitch, w, h; } nvcsurf;
typedef struct { u32 k, x, y, w, h; int sdx, sdy, mdx, mdy; u32 c0, c1, a, inv, key, goff; nvcsurf d, s, m; } nvcop;
int nvc_parse(const u8* f,u32 n,nvkern* k);
int nvc_ready(void);
int nvc_op(const nvcop* o,int wait);
int nvc_submit(void);
void nvc_abort(void);
void nvc_stats(u32* ops,u32* subs,u32* waits,u32* tmo);
const char* nvc_why(void);
void nvc_set_ok(int ok);
void nvgsp_all_off(const char* why);
void nvgsp_tick(void);
int nvgsp_shutdown(void);
int nvgsp_vram_surface(u64 bytes,u64* gpu_va,u32** cpu);
void nvgsp_vram_stats(u32* n,u64* bytes,u64* bar1_left);
const char* nvgsp_off_why(void);
int nvgsp_off(void);
u64 nvgsp_off_age_s(void);
int nvgsp_on(char* msg,int max);
typedef struct { int state, present, present_ok, twod, twod_obj, compute, compute_loaded, drawing, off, autostart; const char* state_name; const char* off_why; const char* cp_why; u64 off_age_s;
    u32 frames, rframes, ce_timeouts, readback_bad, twod_ops, twod_timeouts, cp_ops, cp_subs, cp_timeouts, cp_last_us, gsp_errors, off_count, on_count, vram_surfaces; u64 vram_total, vram_free, vram_surface_bytes; u16 dev; } nvgsp_info_t;
int nvgsp_info(nvgsp_info_t* o);
typedef struct { u32 id, or_type, proto, dfp_flags; int connected, edid_ok, edid_len, nmodes; char maker[4]; u16 product; char name[14]; u16 mw[4], mh[4], mhz[4]; } nvgsp_mon_t;
int nvgsp_monitors(void);
int nvgsp_mon_get(const nvgsp_mon_t** list,u32* heads,u32* headmask,const char** why);
const char* nvgsp_mon_sig(const nvgsp_mon_t* m);

#endif
