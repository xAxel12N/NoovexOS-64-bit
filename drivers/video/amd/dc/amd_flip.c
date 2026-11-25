#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/acpi/acpi_dev.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/damage.h"
#include "drivers/video/amd/amd.h"
#include "drivers/video/amd/dc/dcn_so.h"

extern u32 g_amd_vram_mb;
int igpu_wc_map(u64 va,u64 len);
void blit_all(void);
int amdsdma_copy(const void* src,u32 spb,void* dst,u32 dpb,u32 w,u32 h);
int g_amdflip_live=0, g_amdflip_redo=0;
static struct { const dso_gen_t* g; u32 seg2; int pipe, cur, pend, dmcub, psr, via, crc, redo; u64 mc[2], len, lim; dso_resv_t rv; dso_mc_t m; dso_map_t map; u8* cpu[2]; u32 pitch, w, h, flips, late, timeouts, tries, psraw; char why[96]; } AF;

static u32 afl_rd(void* c,u32 dw){ (void)c; return amd_rreg(dw); }
static void afl_wr(void* c,u32 dw,u32 v){ (void)c; amd_wreg(dw,v); }
static u64 afl_ns(void* c){ (void)c; return hr_ns(); }
static const dso_io_t afl_io={afl_rd,afl_wr,afl_ns,0};

static void afl_why(const char* w){ char* q=AF.why; int n=0; while(w&&w[n]&&n<95){ *q++=w[n]; n++; } *q=0; }
static int afl_no(const char* w){ afl_why(w); char b[140],*p=b; p=scopy(p,"amdgpu: tear-free flips off - "); p=scopy(p,w); *p=0; klog(b); return -1; }
static int afl_bad(const char* w){ afl_no(w); return -2; }

static void afl_copy(u8* dst,u32 dp,const u32* s,u32 sp,u32 w,u32 h){
    { extern int g_amdsdma_sys; extern int amdsdma_present_rects(const u32*,u32,u32,u32,const dmg_rect*,int,u8*,u32); dmg_rect r={0,0,(int)w,(int)h}; if(g_amdsdma_sys&&amdsdma_present_rects(s,sp,w,h,&r,1,dst,dp))return; }
    for(u32 y=0;y<h;y++){ volatile u32* d=(volatile u32*)(dst+(u64)y*dp); const u32* r=s+(u64)y*sp; for(u32 x=0;x<w;x++)d[x]=r[x]; } }

static int afl_hub(dso_mc_t* m){
    if(!g_amd.disc_ok)return -1;
    u32 gc=g_amd.ip[AMD_IP_GC].maj, v=amd_ipver(AMD_IP_MMHUB), loc, off; int seg;
    if(gc==9){ loc=0x082C; off=0x0817; seg=0; }
    else if(gc==10){ if(v==AMD_IPV(2,3,0)||v==AMD_IPV(2,4,0)||v==AMD_IPV(2,4,1)){ loc=0x08EC; off=0x08D7; seg=1; } else { loc=0x086C; off=0x0857; seg=0; } }
    else if(gc==11&&(v>>16)==3&&((v>>8)&0xFF)>=3){ loc=0x09EC; off=0x09D7; seg=1; }
    else return -1;
    u32 base=g_amd.ip[AMD_IP_MMHUB].base[seg]; if(!base)return -1;
    return dso_mc_hub(&afl_io,base+loc,base+off,base+loc+2,m); }
static u8* afl_cpu(const dso_map_t* mp,const dso_mc_t* m,u64 mc){ return (u8*)(mp->cpu0+(mc-m->fb_base)); }

static const char* afl_layout(const dso_gen_t** gg,u32* ss,dso_pipe_t* p,int* ppi,u64* lim,u64* len,dso_resv_t* rv,dso_mc_t* m,dso_map_t* mp){
    if(!g_amd.found||!g_amd.mmio)return "no AMD display registers";
    if(BYPP!=4||g_fb_rgb||!FBB||!W||!H)return "the screen is not a 32-bit framebuffer";
    const dso_gen_t* g=dso_gen(amd_ipver(AMD_IP_DCN),g_amd.did); if(!g)return "this DCN display version is not supported";
    u32 s=g_amd.disc_ok?g_amd.ip[AMD_IP_DCN].base[2]:0; if(!s&&!g_amd.disc_ok&&g_amd.apu)s=0x34C0u; if(!s)return "display register block not found";
    int pi=dso_active(&afl_io,g,s,p); if(pi<0)return "no display pipe is scanning out";
    if(p->sw||p->bpp!=4)return "the firmware surface is not linear 32-bit";
    if(p->vpw!=W||p->vph!=H||p->pitch*4u!=PITCHB)return "the scanned-out surface differs from the desktop size";
    dso_mc_t md, mh; int rd=dso_mc_dcn(&afl_io,g,s,&md), rh=afl_hub(&mh);
    if(!rd&&!rh&&!dso_mc_same(&md,&mh))return "DCN and MMHUB disagree on the video memory window";
    if(!rd)*m=md; else if(!rh)*m=mh; else return "the video memory window could not be read from the memory controller";
    const char* e=dso_map(m,p->addr,(u64)FBB,g_amd.fb_pa,g_amd.fb_len,32ULL<<20,mp); if(e)return e;
    *len=((u64)PITCHB*H+0xFFFFULL)&~0xFFFFULL; dso_resv(&afl_io,g,s,rv); dso_resv_xl(rv,m);
    *gg=g; *ss=s; *ppi=pi; *lim=mp->lim; return 0; }

static u64 afl_slen;
u64 amd_vram_scratch_len(void){ return afl_slen; }
int amd_vram_scratch(u64* mc,volatile u8** cpu){
    static u64 sm; static u8* sc; if(sm){ *mc=sm; *cpu=sc; return 0; }
    u64 x, xl; dso_mc_t m; dso_map_t mp;
    if(g_amdflip_live){ m=AF.m; mp=AF.map; xl=AF.len; x=dso_place(AF.mc[1],AF.len,AF.lim,&AF.rv); }
    else { const dso_gen_t* g; u32 s; dso_pipe_t p; int pi; u64 lim, len; dso_resv_t rv;
        if(afl_layout(&g,&s,&p,&pi,&lim,&len,&rv,&m,&mp))return -1;
        u64 b=dso_place(p.addr,len,lim,&rv); x=b?dso_place(b,len,lim,&rv):0; xl=len; }
    if(!x)return -1;
    u8* xc=afl_cpu(&mp,&m,x); if(!acpi_hi_map((u64)xc))return -1;
    volatile u32* t=(volatile u32*)xc; t[0]=0x1234ABCDu; if(t[0]!=0x1234ABCDu)return -1;
    sm=x; sc=xc; afl_slen=xl; *mc=sm; *cpu=sc; return 0; }

static int afl_dmub_cmd(const dso_gen_t* g,u32 s,const dso_mc_t* m,const dso_map_t* mp,const u32* cmd){
    u64 ib; u32 sz; if(dso_inbox(&afl_io,g,s,&ib,&sz)<0)return -1;
    if(ib<m->fb_base||ib+sz>mp->lim+(32ULL<<20))return -1;
    u8* r=afl_cpu(mp,m,ib); if(!acpi_hi_map((u64)r)||!acpi_hi_map((u64)r+sz-1))return -1;
    return dso_rb_send(&afl_io,g,s,(volatile u8*)r,sz,cmd,20000); }

static const char* afl_psr(const dso_gen_t* g,u32 s,const dso_mc_t* m,const dso_map_t* mp){
    AF.psr=0; AF.psraw=0; if(!dso_dmcub_on(&afl_io,g,s))return 0;
    u32 raw=0; int r=dso_psr_state(&afl_io,g,s,&raw); AF.psraw=raw;
    if(r<0){ AF.psr=3; return 0; }
    if(!r){ AF.psr=1; return 0; }
    u32 c[16]; dso_cmd_psr_off(c); if(afl_dmub_cmd(g,s,m,mp,c))return "panel self refresh is active and the DMCUB inbox is not reachable";
    u64 t0=hr_ns(); while(hr_ns()-t0<100000000ULL){ if(dso_psr_state(&afl_io,g,s,&raw)==0){ AF.psr=2; return 0; } }
    return "panel self refresh is active and DMCUB did not leave it"; }

int amd_dmub_backlight(int pct){
    const dso_gen_t* g; u32 s; dso_pipe_t p; int pi; u64 lim, len; dso_resv_t rv; dso_mc_t m; dso_map_t mp;
    if(afl_layout(&g,&s,&p,&pi,&lim,&len,&rv,&m,&mp)||!dso_dmcub_on(&afl_io,g,s))return -1;
    u32 c[16]; dso_cmd_abm_bl(c,(u32)(pct<0?0:pct>100?100:pct)*0xFFFFu/100u); return afl_dmub_cmd(g,s,&m,&mp,c); }

int amdflip_on(void){
    if(g_amdflip_live)return 0;
    AF.tries++;
    if(!g_amd.found||!g_amd.mmio)return afl_no("no AMD display registers");
    if(BYPP!=4||g_fb_rgb||!FBB||!W||!H)return afl_no("the screen is not a 32-bit framebuffer");
    const dso_gen_t* g; u32 s; dso_pipe_t p; int pi; u64 lim, len; dso_resv_t rv; dso_mc_t m; dso_map_t mp;
    const char* e=afl_layout(&g,&s,&p,&pi,&lim,&len,&rv,&m,&mp); if(e)return afl_no(e);
    if(p.flipctl&1u)return afl_no("surface updates are locked by firmware");
    u64 back=dso_place(p.addr,len,lim,&rv); if(!back)return afl_no("no free video memory after the screen");
    u8* bc=afl_cpu(&mp,&m,back);
    if(!acpi_hi_map((u64)bc)||!acpi_hi_map((u64)bc+len-1))return afl_no("the second buffer cannot be mapped");
    igpu_wc_map((u64)bc,len);
    volatile u32* t=(volatile u32*)bc; u32 nw=(u32)(len/4); t[0]=0x5A17C0DEu; t[nw-1]=0xC0DE5A17u;
    if(t[0]!=0x5A17C0DEu||t[nw-1]!=0xC0DE5A17u)return afl_no("the second buffer did not keep data");
    e=afl_psr(g,s,&m,&mp); if(e)return afl_no(e);
    if(amdsdma_copy(FBB,PITCHB,bc,PITCHB,W,H))for(u32 y=0;y<H;y++){ volatile u32* d=(volatile u32*)(bc+(u64)y*PITCHB); volatile u32* r=(volatile u32*)(FBB+(u64)y*PITCHB); for(u32 x=0;x<W;x++)d[x]=r[x]; }
    dso_vfy_t v; int vr=dso_verify(&afl_io,g,s,pi,p.addr,back,(volatile u32*)bc,W,H,&v);
    if(vr==-1)return afl_bad("the display did not accept a new surface address");
    if(vr){ if(!v.latched)return afl_bad("the display did not switch buffers at vertical blank");
        if(!v.inuse_ok)return afl_bad("the display reads a different address than was flipped to");
        return afl_bad("the display showed different pixels from the second buffer (CRC) - address mapping wrong"); }
    AF.g=g; AF.seg2=s; AF.pipe=pi; AF.mc[0]=p.addr; AF.mc[1]=back; AF.cpu[0]=FBB; AF.cpu[1]=bc; AF.len=len; AF.lim=lim; AF.rv=rv; AF.m=m; AF.map=mp; AF.via=mp.via; AF.pitch=PITCHB; AF.w=W; AF.h=H;
    AF.crc=v.crc_live?2:v.crc_same?1:0;
    AF.cur=1; AF.pend=0; AF.late=0; AF.dmcub=dso_dmcub_on(&afl_io,g,s); AF.why[0]=0; g_amdflip_live=1;
    char b[240],*q=b; q=scopy(q,"amdgpu: tear-free page flips on "); q=scopy(q,g->name); q=scopy(q," pipe "); q=udec(q,(u32)pi); q=scopy(q," "); q=udec(q,W); *q++='x'; q=udec(q,H);
    q=scopy(q," buffers 0x"); q=uhex(q,p.addr,10); q=scopy(q," 0x"); q=uhex(q,back,10); q=scopy(q,mp.via==1?" via BAR":" via carve-out");
    q=scopy(q,AF.crc==2?", CRC verified":AF.crc==1?", CRC unchanged (not proven live)":", no CRC");
    q=scopy(q,AF.psr==2?", PSR disabled via DMCUB":AF.psr==3?", PSR state unknown":AF.dmcub?", DMCUB running":""); *q=0; klog(b);
    return 0; }

void amdflip_off(const char* why){
    if(!g_amdflip_live)return; g_amdflip_live=0; g_amdflip_redo=0; afl_why(why?why:"turned off");
    dso_wait(&afl_io,AF.g,AF.seg2,AF.pipe,50000);
    if(AF.cur!=0){ if(amdsdma_copy(AF.cpu[1],AF.pitch,AF.cpu[0],AF.pitch,AF.w,AF.h))for(u32 y=0;y<AF.h;y++){ volatile u32* d=(volatile u32*)(AF.cpu[0]+(u64)y*AF.pitch); volatile u32* r=(volatile u32*)(AF.cpu[1]+(u64)y*AF.pitch); for(u32 x=0;x<AF.w;x++)d[x]=r[x]; }
        dso_flip(&afl_io,AF.g,AF.seg2,AF.pipe,AF.mc[0]); dso_wait(&afl_io,AF.g,AF.seg2,AF.pipe,100000); AF.cur=0; }
    AF.pend=0; char b[140],*p=b; p=scopy(p,"amdgpu: tear-free flips off - "); p=scopy(p,AF.why); *p=0; klog(b); }

int amdflip_present(const u32* src,u32 sp,u32 w,u32 h){
    if(!g_amdflip_live)return 0;
    if(w!=AF.w||h!=AF.h){ amdflip_off("the desktop size changed"); return 0; }
    if(AF.pend){ int r=dso_wait(&afl_io,AF.g,AF.seg2,AF.pipe,50000);
        if(r==-1){ amdflip_off("display registers stopped answering"); return 0; }
        if(r<0){ AF.late++; g_amdflip_redo=1; if(++AF.timeouts>=3){ amdflip_off("page flips did not complete"); return 0; } return 0; }
        AF.timeouts=0; AF.pend=0; }
    g_amdflip_redo=0; int b=AF.cur^1; afl_copy(AF.cpu[b],AF.pitch,src,sp,w,h);
    if(dso_flip(&afl_io,AF.g,AF.seg2,AF.pipe,AF.mc[b])){ amdflip_off("the display refused a page flip"); return 0; }
    AF.cur=b; AF.pend=1; AF.flips++; return 1; }

void amdflip_boot(void){
    if(!g_amd.found||!g_amd.mmio||!dso_gen(amd_ipver(AMD_IP_DCN),g_amd.did))return;
    if(!sysfeat_begin(SYSF_AMD_FLIP)){ afl_why("switched off in Settings or after failures"); return; }
    int r=amdflip_on(); if(r==-2)sysfeat_fail(SYSF_AMD_FLIP,AF.why); else sysfeat_ok(SYSF_AMD_FLIP); }

int amdflip_setting(int on){
    sysfeat_set(SYSF_AMD_FLIP,on);
    if(!on){ amdflip_off("turned off in Settings"); blit_all(); return 0; }
    if(g_amdflip_live)return 0;
    if(!sysfeat_begin(SYSF_AMD_FLIP))return -1;
    int r=amdflip_on(); if(r==-2)sysfeat_fail(SYSF_AMD_FLIP,AF.why); else sysfeat_ok(SYSF_AMD_FLIP);
    if(!r)blit_all(); return r; }

int amdflip_hw(void){ return g_amd.found&&g_amd.mmio&&dso_gen(amd_ipver(AMD_IP_DCN),g_amd.did)!=0; }
const char* amdflip_why(void){ return AF.why; }

int amdflip_describe(char* o,int cap){
    if(!o||cap<2)return 0; char t[400],*p=t;
    p=scopy(p,"tear-free flips: ");
    if(g_amdflip_live){ p=scopy(p,"on, "); p=scopy(p,AF.g->name); p=scopy(p," pipe "); p=udec(p,(u32)AF.pipe); p=scopy(p," flips "); p=udec(p,AF.flips); p=scopy(p," late "); p=udec(p,AF.late);
        p=scopy(p,"\n  buffers 0x"); p=uhex(p,AF.mc[0],10); p=scopy(p," 0x"); p=uhex(p,AF.mc[1],10); p=scopy(p," in use 0x"); p=uhex(p,dso_inuse(&afl_io,AF.g,AF.seg2,AF.pipe),10);
        p=scopy(p,"\n  VRAM 0x"); p=uhex(p,AF.m.fb_base,10); p=scopy(p,"-0x"); p=uhex(p,AF.m.fb_top,10); p=scopy(p," phys 0x"); p=uhex(p,AF.m.fb_off,10); p=scopy(p,AF.via==1?" CPU via BAR":" CPU via carve-out");
        p=scopy(p,AF.crc==2?", CRC verified":AF.crc==1?", CRC same":", no CRC"); p=scopy(p,AF.psr==2?", PSR off (by us)":AF.psr==1?", PSR inactive":AF.psr==3?", PSR unknown":""); }
    else { p=scopy(p,"off - "); p=scopy(p,AF.why[0]?AF.why:"not started"); }
    p=scopy(p,"\n  "); p=scopy(p,sysfeat_text(SYSF_AMD_FLIP)); *p++='\n'; *p=0;
    int n=0; while(t[n]&&n<cap-1){ o[n]=t[n]; n++; } o[n]=0; return n; }
