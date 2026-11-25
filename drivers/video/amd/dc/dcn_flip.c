#include "include/nvx/base.h"
#include "include/nvx/shell/terminal.h"
#include "drivers/video/amd/amd.h"
#include "drivers/video/amd/dc/dcn_flip.h"

#define DF_SEG2_APU    0x34C0u
#define DF_HUBP_STRIDE 0xDCu
#define DF_OTG_STRIDE  0x80u
#define DFR_SURF_CFG  0x05E5u
#define DFR_TILING    0x05E7u
#define DFR_VP_START  0x05E9u
#define DFR_VP_DIM    0x05EAu
#define DFR_HUBP_CNTL 0x05F3u
#define DFR_PITCH     0x0607u
#define DFR_ADDR      0x060Au
#define DFR_ADDR_HI   0x060Bu
#define DFR_FLIP      0x061Bu
#define DFR_OTG_CTL   0x1B41u
#define DFR_OTG_POS   0x1B4Au
#define DFR_OTG_FC    0x1B4Cu
#define DFF_LOCK      0x001u
#define DFF_IMM       0x002u
#define DFF_PENDING   0x100u

static u32 df_seg2(void){
    u32 v=amd_ipver(AMD_IP_DCN);
    int known=(v>>8)==AMD_IPV(2,0,0)>>8||(v>>8)==AMD_IPV(2,1,0)>>8||(v>>8)==AMD_IPV(3,0,0)>>8||(v>>8)==AMD_IPV(3,1,0)>>8;
    if(!g_amd.mmio||!known)return 0;
    if(g_amd.disc_ok&&g_amd.ip[AMD_IP_DCN].base[2])return g_amd.ip[AMD_IP_DCN].base[2];
    if(g_amd.apu&&(v==AMD_IPV(2,1,0)||v==AMD_IPV(3,1,2)))return DF_SEG2_APU;
    return 0; }
int dcn_flip_ok(void){ return df_seg2()!=0; }
static u32 df_hreg(int p,u32 r){ return df_seg2()+r+(u32)p*DF_HUBP_STRIDE; }
static u32 df_oreg(int o,u32 r){ return df_seg2()+r+(u32)o*DF_OTG_STRIDE; }
static u32 df_bpp(u32 fmt){ if(fmt>=8&&fmt<=13)return 4; if(fmt>=1&&fmt<=4)return 2; if(fmt>=24&&fmt<=27)return 8; return 0; }

int dcn_scan_read(int p,dcn_scan_t* s){
    if(!dcn_flip_ok()||p<0||p>=DCN_FLIP_PIPES||!s)return -1;
    u32 cfg=amd_rreg(df_hreg(p,DFR_SURF_CFG)); if(cfg==0xFFFFFFFFu)return -2;
    s->pipe=p; s->otg=p; s->fmt=cfg&0x7F; s->bpp=df_bpp(s->fmt);
    s->sw=amd_rreg(df_hreg(p,DFR_TILING))&0x1F; s->blank=amd_rreg(df_hreg(p,DFR_HUBP_CNTL))&1;
    s->pitch=(amd_rreg(df_hreg(p,DFR_PITCH))&0x3FFF)+1;
    u32 vs=amd_rreg(df_hreg(p,DFR_VP_START)),vd=amd_rreg(df_hreg(p,DFR_VP_DIM));
    s->vpx=vs&0x3FFF; s->vpy=(vs>>16)&0x3FFF; s->vpw=vd&0x3FFF; s->vph=(vd>>16)&0x3FFF;
    s->flipctl=amd_rreg(df_hreg(p,DFR_FLIP));
    s->addr=((u64)(amd_rreg(df_hreg(p,DFR_ADDR_HI))&0xFFFF)<<32)|amd_rreg(df_hreg(p,DFR_ADDR));
    s->otg_on=amd_rreg(df_oreg(p,DFR_OTG_CTL))&1; s->frame=amd_rreg(df_oreg(p,DFR_OTG_FC))&0xFFFFFF; s->vcount=amd_rreg(df_oreg(p,DFR_OTG_POS))&0x7FFF;
    return 0; }

int dcn_scan_active(dcn_scan_t* s){
    for(int p=0;p<DCN_FLIP_PIPES;p++){ if(dcn_scan_read(p,s))continue; if(!s->blank&&s->addr&&s->otg_on&&s->bpp&&s->vpw&&s->vph)return p; }
    return -1; }

int dcn_wait_frames(int o,int n){
    if(!dcn_flip_ok()||o<0||o>=DCN_FLIP_PIPES||!(amd_rreg(df_oreg(o,DFR_OTG_CTL))&1))return -1;
    u32 f0=amd_rreg(df_oreg(o,DFR_OTG_FC))&0xFFFFFF; u64 t0=hr_ns(),lim=(u64)(n+2)*50000000ULL;
    while(((amd_rreg(df_oreg(o,DFR_OTG_FC))-f0)&0xFFFFFF)<(u32)n){ if(hr_ns()-t0>lim)return -1; cpu_pause(); }
    return 0; }

int dcn_flip(int p,u64 addr,u32 pitch,int imm){
    if(!dcn_flip_ok()||p<0||p>=DCN_FLIP_PIPES||!addr||(addr&0xFF)||(addr>>48)||!pitch||pitch>0x4000)return -1;
    u32 cur=(amd_rreg(df_hreg(p,DFR_PITCH))&0x3FFF)+1; int lk=cur!=pitch;
    if(lk)amd_wreg_mask(df_hreg(p,DFR_FLIP),DFF_LOCK,DFF_LOCK);
    amd_wreg_mask(df_hreg(p,DFR_FLIP),DFF_IMM,imm?DFF_IMM:0);
    if(lk)amd_wreg_mask(df_hreg(p,DFR_PITCH),0x3FFF,pitch-1);
    amd_wreg(df_hreg(p,DFR_ADDR_HI),(u32)(addr>>32)&0xFFFF);
    amd_wreg(df_hreg(p,DFR_ADDR),(u32)addr);
    if(lk)amd_wreg_mask(df_hreg(p,DFR_FLIP),DFF_LOCK,0);
    int r=amd_wait(df_hreg(p,DFR_FLIP),DFF_PENDING,0,100000);
    u64 rb=((u64)(amd_rreg(df_hreg(p,DFR_ADDR_HI))&0xFFFF)<<32)|amd_rreg(df_hreg(p,DFR_ADDR));
    return r?r:(rb==addr?0:-3); }

static void df_hex(u64 v){ char b[19]; int i=18; b[i]=0; do{ b[--i]="0123456789ABCDEF"[v&15]; v>>=4; }while(v&&i>2); b[--i]='x'; b[--i]='0'; term_puts(b+i); }
static void df_dec(u64 v){ char b[21]; int i=20; b[i]=0; do{ b[--i]=(char)('0'+v%10); v/=10; }while(v&&i); term_puts(b+i); }
static void df_kv(const char* k,u64 v,int hex){ term_puts(k); if(hex)df_hex(v); else df_dec(v); }

static void df_show(const dcn_scan_t* s){
    df_kv("  pipe ",s->pipe,0); df_kv(" otg",s->otg,0); term_puts(s->otg_on?" on":" off"); term_puts(s->blank?" blanked":" scanning");
    df_kv(" addr ",s->addr,1); df_kv(" pitch ",s->pitch,0); df_kv("px fmt ",s->fmt,0); df_kv(" sw ",s->sw,0);
    df_kv(" vp ",s->vpw,0); term_puts("x"); df_dec(s->vph); df_kv("+",s->vpx,0); term_puts(","); df_dec(s->vpy); df_kv(" frame ",s->frame,0); term_puts("\n"); }

static void df_restore(const dcn_scan_t* o){
    for(int i=0;i<3;i++){ if(!dcn_flip(o->pipe,o->addr,o->pitch,0))break; amd_wreg_mask(df_hreg(o->pipe,DFR_FLIP),DFF_LOCK,0); }
    amd_wreg(df_hreg(o->pipe,DFR_FLIP),o->flipctl); }

int dcn_flip_test(void){
    dcn_scan_t o,c; int p=dcn_scan_active(&o);
    if(p<0){ term_puts("amdgpu flip: no active DCN pipe (unsupported DCN or screen not driven)\n"); return -1; }
    term_puts("amdgpu flip: before\n"); df_show(&o);
    if(o.sw){ term_puts("amdgpu flip: surface is tiled, test needs linear GOP surface\n"); return -1; }
    u32 lines=64; u64 row=(u64)o.pitch*o.bpp; while(((row*lines)&0xFF)&&lines<1024)lines<<=1;
    if((row*lines)&0xFF||lines>=o.vph){ term_puts("amdgpu flip: no aligned offset\n"); return -1; }
    u64 na=o.addr+row*lines;
    term_puts("amdgpu flip: scrolling scanout by "); df_dec(lines); term_puts(" lines for 2 s\n");
    dcn_wait_frames(o.otg,1);
    int r=dcn_flip(p,na,o.pitch,0);
    if(!r){ dcn_scan_read(p,&c); df_show(&c); u64 t0=hr_ns(); while(hr_ns()-t0<2000000000ULL)cpu_pause(); }
    df_restore(&o);
    dcn_scan_read(p,&c);
    int ok=c.addr==o.addr&&c.pitch==o.pitch;
    term_puts("amdgpu flip: after\n"); df_show(&c);
    term_puts(r?"amdgpu flip: FAILED (flip not latched) - restored\n":ok?"amdgpu flip: OK - scanout moved and restored\n":"amdgpu flip: restore mismatch\n");
    return r?r:(ok?0:-4); }

void dcn_flip_shell(int argc,char** argv){
    if(!dcn_flip_ok()){ term_puts("amdgpu flip: no supported AMD DCN (2.x/3.0/3.1) found\n"); return; }
    if(argc>=3&&argv[2][0]=='t'){ dcn_flip_test(); return; }
    dcn_scan_t s; int any=0;
    for(int p=0;p<DCN_FLIP_PIPES;p++){ if(dcn_scan_read(p,&s))continue; if(!s.otg_on&&s.blank&&!s.addr)continue; df_show(&s); any=1; }
    if(!any)term_puts("amdgpu flip: no pipes in use\n");
    term_puts("usage: amdgpu flip [test]\n"); }
int amd_flip_test(void){ return dcn_flip_test(); }
