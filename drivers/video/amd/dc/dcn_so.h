#ifndef NVX_DRIVERS_VIDEO_AMD_DCN_SO_H
#define NVX_DRIVERS_VIDEO_AMD_DCN_SO_H
#include "include/nvx/base.h"

#define DSO_PIPES 4
#define DSO_NREG 24

typedef struct { u32 (*rd)(void* c,u32 dw); void (*wr)(void* c,u32 dw,u32 v); u64 (*ns)(void* c); void* c; } dso_io_t;
typedef struct { u32 ver; const char* name; u32 otg_ctl, otg_fc, inuse, dmr, vmfb, crc, crcw, crcd; } dso_gen_t;
typedef struct { u64 fb_base, fb_top, fb_off, agp_bot, agp_top, agp_base; } dso_mc_t;
typedef struct { u64 cpu0, lim, scan_off; int via; } dso_map_t;
typedef struct { int crc_live, crc_same, inuse_ok, latched; u32 c0[3], c1[3], c2[3]; } dso_vfy_t;
typedef struct { int pipe, on, blank; u32 fmt, bpp, sw, pitch, vpw, vph, flipctl; u64 addr; } dso_pipe_t;
typedef struct { int n; u64 lo[DSO_NREG], hi[DSO_NREG]; } dso_resv_t;

const dso_gen_t* dso_gen(u32 dcn_ver,u16 did);
u32 dso_ver_for_did(u16 did);
int dso_read(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,dso_pipe_t* s);
int dso_active(const dso_io_t* io,const dso_gen_t* g,u32 seg2,dso_pipe_t* s);
int dso_dmcub_on(const dso_io_t* io,const dso_gen_t* g,u32 seg2);
void dso_resv(const dso_io_t* io,const dso_gen_t* g,u32 seg2,dso_resv_t* r);
u64 dso_place(u64 front,u64 len,u64 lim,const dso_resv_t* r);
int dso_flip(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,u64 addr);
int dso_pending(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe);
int dso_wait(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,u32 us);
int dso_mc_dcn(const dso_io_t* io,const dso_gen_t* g,u32 seg2,dso_mc_t* m);
int dso_mc_hub(const dso_io_t* io,u32 fbloc,u32 fboff,u32 agptop,dso_mc_t* m);
int dso_mc_same(const dso_mc_t* a,const dso_mc_t* b);
const char* dso_map(const dso_mc_t* m,u64 scan,u64 fbb,u64 bar,u64 barlen,u64 margin,dso_map_t* o);
void dso_resv_xl(dso_resv_t* r,const dso_mc_t* m);
u32 dso_frame(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe);
int dso_frames(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,u32 n,u32 us);
int dso_crc_on(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,u32 w,u32 h,u32* saved);
void dso_crc_restore(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,u32 saved);
int dso_crc(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,u32 c[3]);
int dso_verify(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe,u64 front,u64 back,volatile u32* backpx,u32 w,u32 h,dso_vfy_t* v);
int dso_gpint(const dso_io_t* io,const dso_gen_t* g,u32 seg2,u32 cmd,u32 param,u32 us,u32* resp);
int dso_psr_state(const dso_io_t* io,const dso_gen_t* g,u32 seg2,u32* raw);
int dso_inbox(const dso_io_t* io,const dso_gen_t* g,u32 seg2,u64* mc,u32* size);
int dso_rb_send(const dso_io_t* io,const dso_gen_t* g,u32 seg2,volatile u8* ring,u32 size,const u32* cmd,u32 us);
void dso_cmd_psr_off(u32 c[16]);
void dso_cmd_abm_bl(u32 c[16],u32 lvl16);
u64 dso_inuse(const dso_io_t* io,const dso_gen_t* g,u32 seg2,int pipe);

#endif
