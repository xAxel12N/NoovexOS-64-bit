#include "include/nvx/core/sysfeat.h"
#include "include/nvx/drivers/video/nvidia/nvsettings.h"
#include "include/nvx/drivers/video/nvidia/nvflip.h"
#include "include/nvx/drivers/video/fb/deskscale.h"
enum { GS_DRAW, GS_AUTO, GS_FLIP, GS_CUR, GS_EXT, GS_VRAM, GS_2D, GS_DP, GS_AMP, GS_ADA, GS_IBLT, GS_ITEAR, GS_S8X, GS_N };
enum { GS_ID=3400, GS_SCL=3440, GS_DPBTN=3450, GS_AMDTEAR=3451, GS_ARR=3452, GS_AMDSDMA=3456 };
static char gs_err[GS_N][96];
static void gs_seterr(int k,const char* m){ int n=0; if(m)while(m[n]&&m[n]!='\n'&&n<95){ gs_err[k][n]=m[n]; n++; } gs_err[k][n]=0; }
static int gs_heavy_id(int k){ return k==GS_DRAW||k==GS_FLIP||k==GS_VRAM||k==GS_CUR; }
static int gs_nv(int k){ switch(k){ case GS_S8X: return NVSET_SHADER8X; case GS_AUTO: return NVSET_BOOTSTART; case GS_FLIP: return NVSET_MODESET; case GS_CUR: return NVSET_CURSOR; case GS_EXT: return NVSET_EXTEND; case GS_VRAM: return NVSET_CARDMEM; case GS_2D: return NVSET_2D; case GS_DP: return NVSET_DPTRAIN; case GS_AMP: return NVSET_AMPERE; case GS_ADA: return NVSET_ADA; default: return -1; } }
static int gs_get(int k){
    extern int g_nvd_on, g_blt_present, g_igpu_flip; int f=gs_nv(k); if(f>=0)return nvset_get(f);
    switch(k){ case GS_DRAW: return g_nvd_on; case GS_IBLT: return g_blt_present; case GS_ITEAR: return g_igpu_flip; default: return 0; } }
static void gs_set(int k,int on){ gs_err[k][0]=0;
    switch(k){
    case GS_DRAW: { extern int nvd_desktop(void); extern char g_nvd_why[]; extern void gfx_stop(const char*); if(!on)gfx_stop("turned off in Settings"); else if(nvd_desktop())gs_seterr(k,g_nvd_why[0]?g_nvd_why:"The GPU could not take over drawing"); } break;
    case GS_AMP: { extern const char* nvd_ampere_cmd(const char*); nvd_ampere_cmd(on?"ampere on":"ampere off"); } break;
    case GS_ADA: { extern int nvada_cmd(const char*); nvset_set(NVSET_ADA,on); nvada_cmd(on?"on":"off"); } break;
    case GS_EXT: nvset_set(NVSET_EXTEND,on); break;
    case GS_IBLT: { extern void igpu_blt_mode(int); extern int g_blt_present; igpu_blt_mode(on); if(on&&!g_blt_present)gs_seterr(k,"The Intel blitter is not ready on this machine"); } break;
    case GS_ITEAR: { extern int igpu_flip_on(void); extern void igpu_flip_off(const char*); if(!on)igpu_flip_off("turned off in Settings"); else if(igpu_flip_on()<0)gs_seterr(k,"Could not start - needs Intel graphics"); } break;
    default: { int f=gs_nv(k); if(f>=0&&nvset_set(f,on)<0)gs_seterr(k,"Not available in this build"); } break; } }
static const char* gs_state(int k,char* b){ char* q=b; char* e=b+127; int f=gs_nv(k);
    if(f>=0&&nvset_state(f)==NVSET_ST_NO_HW&&nvset_no_card()&&f!=NVSET_EXTEND){ scopy(q,"No NVIDIA graphics card")[0]=0; return b; }
    if(f>=0){ const char* t=nvset_state_text(f); const char* le=nvset_last_error(f); if(!le||!le[0])le=gs_err[k];
        if(t[0]>='a'&&t[0]<='z'){ *q++=(char)(t[0]-32); t++; } while(*t&&q<e)*q++=*t++;
        if(le[0]){ const char* s=" - last error: "; while(*s&&q<e)*q++=*s++; while(*le&&*le!='\n'&&q<e)*q++=*le++; }
        *q=0; return b; }
    if(gs_err[k][0]){ q=scopy(q,"Did not work: "); int n=0; while(gs_err[k][n]&&q<e)*q++=gs_err[k][n++]; *q=0; return b; }
    switch(k){
    case GS_DRAW: { nvgsp_info_t ni; nvgsp_info(&ni); if(gs_get(k))q=scopy(q,"On - the NVIDIA GPU draws the desktop"); else if(ni.off&&ni.off_why&&ni.off_why[0]){ q=scopy(q,"Off: "); int n=0; while(ni.off_why[n]&&n<60)*q++=ni.off_why[n++]; } else q=scopy(q,ni.state<7?"Off - the NVIDIA card has not started yet":"Off - the CPU draws"); } break;
    case GS_IBLT: q=scopy(q,gs_get(k)?"On - the Intel blitter copies the picture":"Off"); break;
    case GS_ITEAR: q=scopy(q,gs_get(k)?"On - double buffering, no tearing":"Off"); break;
    default: break; }
    *q=0; return b; }
static int gs_rows(int cy,const char* title,const int* ks,const char** nm,int n){ int X1=st_x0+st_w0-24; int y=st_card(cy,64*n,title); char b[128];
    for(int i=0;i<n;i++){ int ry=y+i*64; if(i)st_sep(ry); st_rowtext_w(ry,nm[i],gs_state(ks[i],b),X1-60-(st_x0+20)); st_toggle(X1-44,ry+20,gs_get(ks[i]),GS_ID+ks[i]); }
    return y+64*n+24; }
static int st_gfx_scale(int cy){ static const char* L[5]={"Auto","100%","125%","150%","200%"}; u32 cur=fb_desk_scale(); nvflip_info_t fi; nvflip_info(&fi);
    int y=st_card(cy,108,"Desktop scale"); char b[160]; char* q=b;
    if(fi.up&&fi.mw){ q=scopy(q,"Screen "); q=udec(q,fi.mw); q=scopy(q,"x"); q=udec(q,fi.mh); q=scopy(q,", desktop "); q=udec(q,fi.w); q=scopy(q,"x"); q=udec(q,fi.h); if(fi.scale){ q=scopy(q," at "); q=udec(q,fi.scale); q=scopy(q,"%"); } if(!fi.native&&(fi.w!=fi.mw||fi.h!=fi.mh))q=scopy(q," (scaled by the display)"); }
    else q=scopy(q,"Used when native mode is on: Auto picks 150% on 4K screens and 100% up to 1440p");
    *q=0; pdstr(st_x0+20,y+14,b,ST_DIM,1); pdstr(st_x0+20,y+34,g_desk_why[0]?g_desk_why:"Larger scale = bigger text; falls back to a larger scale or the firmware size when memory is short",ST_DIM,1);
    for(int i=0;i<5;i++){ u32 p=i?desk_pct(i-1):0; st_btn(st_x0+20+i*90,y+60,80,L[i],GS_SCL+i,p==cur); }
    return y+108+24; }
static int st_gfx_nv(int cy){
    static const int K[7]={GS_FLIP,GS_CUR,GS_VRAM,GS_2D,GS_AMP,GS_ADA,GS_S8X};
    static const char* N[7]={"Native mode and tear-free flips","Hardware cursor","Desktop layers in card memory","2D engine","Allow RTX 30 cards","Allow RTX 40 cards","RTX 30/40 compute compositor"};
    cy=gs_rows(cy,"Screen and drawing switches",K,N,7);
    { int X1=st_x0+st_w0-24; int y=st_card(cy,64,"DisplayPort"); char b[128]; const char* d=gs_state(GS_DP,b); st_rowtext_w(y,"DisplayPort monitor",d,X1-146-(st_x0+20)); st_btn(X1-130,y+16,130,"Connect",GS_DPBTN,1); cy=y+64+24; }
    return st_gfx_scale(cy); }
static int st_gfx_screens(int cy){ static const int K[1]={GS_EXT}; static const char* N[1]={"Extend to the second screen"}; cy=gs_rows(cy,"Two screens",K,N,1);
    { extern int g_disp_extend, g_out_arr; if(g_disp_extend){ static const char* A[4]={"Right","Left","Above","Below"}; static const char* T[4]={"The second screen sits to the right of the first one","The second screen sits to the left of the first one","The second screen sits above the first one","The second screen sits below the first one"};
        int y=st_card(cy,108,"Arrangement"); int a=g_out_arr<0||g_out_arr>3?0:g_out_arr; pdstr(st_x0+20,y+14,T[a],ST_DIM,1); pdstr(st_x0+20,y+34,"The taskbar stays on the main screen; move the mouse or drag a window across the edge",ST_DIM,1);
        for(int i=0;i<4;i++){ st_btn(st_x0+20+i*110,y+60,100,A[i],GS_ARR+i,i==a); } cy=y+108+24; } }
    return cy; }
static int st_gfx_intel(int cy){ extern int igpu_info(const char**,u32*,u32*,u32*,u32*,u32*,u32*,u32*); const char* nm=0; u32 did=0,a=0,b=0,c=0,d=0,e=0,f=0; if(!igpu_info(&nm,&did,&a,&b,&c,&d,&e,&f))return cy;
    static const int K[1]={GS_IBLT}; static const char* N[1]={"Intel blitter"}; cy=gs_rows(cy,"Intel graphics",K,N,1);
    { int y=st_card(cy,44,0); pdstr(st_x0+20,y+14,"Gen 9 and Gen 12 (Tiger, Alder, Raptor Lake) are picked by themselves - no switch needed",ST_DIM,1); cy=y+44+24; }
    return cy; }
static int st_gfx_amdsdma(int cy){ extern int amdsdma_hw(void); extern const char* amdsdma_why(void); extern int g_amdsdma_live; if(!amdsdma_hw())return cy;
    int X1=st_x0+st_w0-24; int y=st_card(cy,64,"AMD copy engine"); char b[128]; char* q=b; int mx=(st_w0-160)/8-4; if(mx>110)mx=110; if(mx<24)mx=24; char* e=b+mx;
    if(g_amdsdma_live){ extern int g_amdsdma_sys; extern const char* amdsdma_gart_why(void); extern int amdsdma_psp_loaded(void); q=scopy(q,amdsdma_psp_loaded()?"On (PSP firmware)":"On"); if(g_amdsdma_sys)q=scopy(q," - RAM frames and fills on the GPU, CPU if one fails"); else { q=scopy(q," - VRAM copies only, RAM frames off: "); const char* w=amdsdma_gart_why(); while(*w&&q<e)*q++=*w++; } }
    else { const char* w=amdsdma_why(); const char* t=sysfeat_text(SYSF_AMD_SDMA); while(*t&&q<e)*q++=*t++; if(w&&w[0]){ const char* s2=" - "; while(*s2&&q<e)*q++=*s2++; while(*w&&q<e)*q++=*w++; } }
    *q=0; st_rowtext(y,"SDMA copy engine",b); st_toggle(X1-44,y+20,g_amdsdma_live,GS_AMDSDMA); return y+64+24; }
static int st_gfx_amd(int cy){ extern int amdflip_hw(void); extern const char* amdflip_why(void); extern int g_amdflip_live; if(!amdflip_hw())return cy;
    int X1=st_x0+st_w0-24; int y=st_card(cy,64,"AMD graphics"); char b[128]; char* q=b; char* e=b+127;
    if(g_amdflip_live)q=scopy(q,"On - native panel mode kept, page flips at vertical blank");
    else { const char* w=amdflip_why(); const char* t=sysfeat_text(SYSF_AMD_FLIP); while(*t&&q<e)*q++=*t++; if(w&&w[0]){ const char* s2=" - "; while(*s2&&q<e)*q++=*s2++; while(*w&&q<e)*q++=*w++; } }
    *q=0; st_rowtext_w(y,"Tear-free page flips",b,X1-60-(st_x0+20)); st_toggle(X1-44,y+20,g_amdflip_live,GS_AMDTEAR); return st_gfx_amdsdma(y+64+24); }
static void gs_toast(int k){ int f=gs_nv(k); const char* le=f>=0?nvset_last_error(f):""; if(gs_err[k][0])toast(gs_err[k]); else if(f>=0&&nvset_get(f)&&nvset_state(f)>=NVSET_ST_FALLBACK&&le&&le[0])toast(le); }
static int st_gfx_click(int id){ if(id>=GS_SCL&&id<GS_SCL+5){ u32 p=id>GS_SCL?desk_pct(id-GS_SCL-1):0; fb_desk_scale_set(p); toast(g_nvflip_live?"Desktop scale changed - the screen is set up again":"Desktop scale saved - used when native mode starts"); return 1; } if(id==GS_DPBTN||id==GS_AMDTEAR||id==GS_AMDSDMA)return 2; if(id>=GS_ARR&&id<GS_ARR+4){ extern int display_arrange(int); display_arrange(id-GS_ARR); return 1; } int k=id-GS_ID; if(k<0||k>=GS_N||k==GS_DP)return 0; if(gs_heavy_id(k))return 2; gs_set(k,!gs_get(k)); gs_toast(k); return 1; }
static void st_gfx_heavy(int id){ if(id==GS_AMDSDMA){ extern int amdsdma_setting(int); extern int g_amdsdma_live; extern const char* amdsdma_why(void); int on=!g_amdsdma_live; int r=amdsdma_setting(on); toast(!on?"AMD SDMA copy engine off":r==0?"AMD SDMA copy engine on":amdsdma_why()[0]?amdsdma_why():"AMD SDMA could not start"); return; } if(id==GS_AMDTEAR){ extern int amdflip_setting(int); extern int g_amdflip_live; extern const char* amdflip_why(void); int on=!g_amdflip_live; int r=amdflip_setting(on); toast(!on?"AMD tear-free page flips off":r==0?"AMD tear-free page flips on":amdflip_why()[0]?amdflip_why():"AMD tear-free page flips could not start"); return; } if(id==GS_DPBTN){ extern int nvgsp_cmd(const char*); extern int nvgsp_dp_present(void); gs_err[GS_DP][0]=0; if(nvset_allowed(NVSET_DPTRAIN)&&!nvgsp_dp_present()){ nvset_note(NVSET_DPTRAIN,NVSET_ST_NO_HW,"no DisplayPort monitor on a running NVIDIA card"); gs_seterr(GS_DP,"no DisplayPort monitor found on a running NVIDIA card"); toast("No DisplayPort monitor found on a running NVIDIA card"); return; } if(!nvset_try_begin(NVSET_DPTRAIN)){ gs_seterr(GS_DP,nvset_state_text(NVSET_DPTRAIN)); toast("DisplayPort is switched off or turned itself off after failures"); return; } int r=nvgsp_cmd("dp"); nvset_try_end(NVSET_DPTRAIN,r>0,"DisplayPort did not come up - see nvgsp-log.txt"); toast(r>0?"DisplayPort monitor on":"DisplayPort did not come up - see nvgsp-log.txt"); if(r<=0)gs_seterr(GS_DP,"DisplayPort did not come up - see nvgsp-log.txt"); return; }
    int k=id-GS_ID; if(k<0||k>=GS_N)return; gs_set(k,!gs_get(k)); gs_toast(k); }
