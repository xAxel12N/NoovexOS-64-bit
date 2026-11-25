#ifndef NVX_DRIVERS_USB_UAC_H
#define NVX_DRIVERS_USB_UAC_H
#include "include/nvx/base.h"
typedef int (*uac_req_fn)(u8 rt,u8 req,u16 val,u16 idx,u16 len,int in,u8* data);
enum { UAC_E_IT=1, UAC_E_OT, UAC_E_FU, UAC_E_UNIT, UAC_E_CLK, UAC_E_CSEL, UAC_E_CMUL };
typedef struct { u8 id, kind, clk, nsrc; u8 src[4]; u16 type; u32 fc[3]; } uac_ent_t;
typedef struct { u8 ifn, alt, link, ep, sync, fb, fbint; u16 fbmps; } uac_as_t;
typedef struct { int v2, ac_if, nent, nas, ot, it, fu, clk, mic_it; u16 ot_type; uac_ent_t ent[32]; uac_as_t as[16]; } uac_topo_t;
typedef struct { int ok, mute, v2, fu, ac_if, nch; u8 ch[3]; short min, max, res; } uac_vol_t;
typedef struct { u32 nom, fb, acc, n, bad; int hs, shift, locked; } uac_rate_t;
int uac_topo_parse(const u8* c,int n,uac_topo_t* t);
int uac_out_path(uac_topo_t* t);
const char* uac_kind_name(u16 type);
int uac_as_find(const uac_topo_t* t,int ifn,int alt);
u32 uac_pick(const u32* r,int n,int cont,u32 pref);
u32 uac2_range_pick(const u8* d,int n,u32 pref,u32 dflt);
void uac_rate_init(uac_rate_t* r,u32 rate,int hs);
int uac_fb_take(uac_rate_t* r,const u8* d,int n);
u32 uac_rate_frames(uac_rate_t* r,u32 ivus);
int uac_vol_probe(const uac_topo_t* t,uac_req_fn req,uac_vol_t* v);
int uac_vol_value(const uac_vol_t* v,int pct);
int uac_vol_set(const uac_vol_t* v,uac_req_fn req,int pct);
void uac_out_attach(const u8* c,int n,int hs,uac_req_fn req);
void uac_out_detach(void);
int uac_out_present(void);
u32 uac_out_rate(const u32* r,int n,int cont,u32 dflt);
u32 uac_out_rate2(const u8* d,int n,u32 dflt);
int uac_out_clock(int dflt);
int uac_out_fb(int ifn,int alt,u8* ep,u16* mps,u8* intv);
void uac_out_fb_start(u32 rate,int hs);
void uac_out_fb_stop(void);
void uac_out_fb_data(const u8* d,int n);
int uac_out_fb_live(void);
u32 uac_out_frames(u32 ivus);
int uac_out_volume(int pct);
void uac_out_stream(u32 rate,int bits,int ch);
const char* uac_out_desc(void);
u32 uac_out_rate_pref(void);
void uac_out_set_rate_pref(u32 r);
extern u32 g_uac_rate_pref;
#endif
