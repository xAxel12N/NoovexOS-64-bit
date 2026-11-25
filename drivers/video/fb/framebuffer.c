#include "include/nvx/base.h"
#include "include/nvx/assets/font.h"
#include "include/nvx/assets/fontaa.h"
#include "include/nvx/arch/x86_64/cpu/simd.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/drivers/video/fb/gfx.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/video/fb/damage.h"
#include "include/nvx/drivers/video/nvidia/nvgsp.h"
#include "include/nvx/graphics/ui/outputs.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/graphics/ui/desktop.h"
#include "include/nvx/drivers/video/nvidia/nvflip.h"
#include "include/nvx/drivers/video/fb/deskscale.h"
#include "include/nvx/lib/base/fmt.h"
/* =========================================================================
   Framebuffer + back buffers
   ========================================================================= */

int g_fb_rgb=0; u32 g_uefi_ram_mb=0;
void copy_swap32(u32* d,const u32* s,int n){ for(int i=0;i<n;i++){ u32 v=s[i]; d[i]=(v&0xFF00FF00u)|((v>>16)&0xFFu)|((v&0xFFu)<<16); } }
u32 SCENE_STATIC[MAXW*MAXH]; u32* g_scene=SCENE_STATIC;
u32 BG_STATIC[MAXW*MAXH]; u32* BG=BG_STATIC;

u8  *FBB;                        /* hardware framebuffer         */
u32  PITCHB, W, H, SW;           /* SW = scene stride (=W)       */
u8   BPP; u32 BYPP;
u8  *FBB2;
u32  PITCHB2, W2, H2;
u8   BYPP2;
int  g_fb2_ok=0, g_fb2_rgb=0, g_mirror2=1;

int g_clip_y0=-0x40000000, g_clip_y1=0x40000000, g_clip_maxy=0;
void fb_clip_y(int y0,int y1){ g_clip_y0=y0; g_clip_y1=y1; g_clip_maxy=y0; }
void fb_clip_off(void){ g_clip_y0=-0x40000000; g_clip_y1=0x40000000; }
static inline void clip_note(int y){ if(y>g_clip_maxy)g_clip_maxy=y; }
void fill_rect(int x,int y,int w,int h,u32 c){
    clip_note(h>0?y+h:y); if(y<g_clip_y0){h-=g_clip_y0-y;y=g_clip_y0;} if(y+h>g_clip_y1)h=g_clip_y1-y;
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    if(g_gfx){ gq_fill(g_rt,x,y,w,h,c); return; }
    { extern int g_amdsdma_sys; extern int amdsdma_fill_sys(u32*,u32,u32,int,int,int,int,u32); if(g_amdsdma_sys&&(u64)w*h>=262144u&&amdsdma_fill_sys(SCENE,SW,H,x,y,w,h,c)==0)return; }
    if(g_accel2d){ for(int j=0;j<h;j++) sse_fill32(SCENE+(u64)(y+j)*SW+x, c, w); }
    else { for(int j=0;j<h;j++){ u32* r=SCENE+(u64)(y+j)*SW+x; for(int i=0;i<w;i++) r[i]=c; } }
}
static inline u32  getpx(int x,int y){ return ((u32)x<W&&(u32)y<H)?SCENE[(u32)y*SW+x]:0; }

u32 isqrt32(u32 v){ u32 r=0,b=1u<<30; while(b>v)b>>=2; while(b){ if(v>=r+b){v-=r+b;r=(r>>1)+b;} else r>>=1; b>>=2; } return r; }
int cut(int i,int j,int w,int h,int r,int m){
    if(r<=0)return 0;
    if((m&1)&&i<r&&j<r){int dx=r-i,dy=r-j; if(dx*dx+dy*dy>r*r)return 1;}
    if((m&2)&&i>=w-r&&j<r){int dx=i-(w-1-r),dy=r-j; if(dx*dx+dy*dy>r*r)return 1;}
    if((m&4)&&i<r&&j>=h-r){int dx=r-i,dy=j-(h-1-r); if(dx*dx+dy*dy>r*r)return 1;}
    if((m&8)&&i>=w-r&&j>=h-r){int dx=i-(w-1-r),dy=j-(h-1-r); if(dx*dx+dy*dy>r*r)return 1;}
    return 0;
}
/* anti-aliased rounded rectangle */
int g_round_off=0;
void fill_round(int x,int y,int w,int h,int r,int m,u32 c){ if(g_round_off){ fill_rect(x,y,w,h,c); return; }
    clip_note(h>0?y+h:y); if(y>=g_clip_y1||y+h<=g_clip_y0)return;
    if(g_gfx){ gfx_round(x,y,w,h,r,m,c,256); return; }
    for(int j=0;j<h;j++)for(int i=0;i<w;i++){
        int dx=-1,dy=0;
        if((m&1)&&i<r&&j<r){dx=r-i;dy=r-j;}
        else if((m&2)&&i>=w-r&&j<r){dx=i-(w-1-r);dy=r-j;}
        else if((m&4)&&i<r&&j>=h-r){dx=r-i;dy=j-(h-1-r);}
        else if((m&8)&&i>=w-r&&j>=h-r){dx=i-(w-1-r);dy=j-(h-1-r);}
        if(dx>=0){ u32 dd=isqrt32(((u32)(dx*dx+dy*dy))<<16); int cov=(int)(r*256)-(int)dd+128;
            if(cov<=0) continue; if(cov<256){ pblend(x+i,y+j,c,cov); continue; } }
        putpx(x+i,y+j,c);
    }
}
/* soft, layered drop shadow that skips the pixels the window will cover */
void shadow_soft(int wx,int wy,int ww,int wh,int wr){
    if(g_gfx){ gfx_shadow(wx,wy,ww,wh,wr); return; }
    int E[4]={12,8,5,2}, S[4]={14,10,6,3}, A[4]={6,8,10,12};
    for(int k=0;k<4;k++){ int e=E[k],s=S[k],a=A[k], x=wx-e,y=wy-e+s,w=ww+2*e,h=wh+2*e,r=wr+e;
        for(int j=0;j<h;j++)for(int i=0;i<w;i++){ int px=x+i,py=y+j;
            if(px>=wx&&px<wx+ww&&py>=wy&&py<wy+wh) continue;
            if(cut(i,j,w,h,r,15)) continue; pblend(px,py,rgb(0,0,0),a); } }
}
/* anti-aliased filled circle */
void fill_circle(int cx,int cy,int r,u32 c){
    clip_note(cy+r+1); if(cy-r-1>=g_clip_y1||cy+r+2<=g_clip_y0)return;
    if(g_gfx){ gfx_circle(cx,cy,r,c,256); return; }
    for(int j=-r-1;j<=r+1;j++)for(int i=-r-1;i<=r+1;i++){ int d2=i*i+j*j; if(d2>(r+1)*(r+1)) continue;
        u32 dd=isqrt32(((u32)d2)<<16); int cov=(int)(r*256)-(int)dd+128;
        if(cov<=0) continue; if(cov>=256) putpx(cx+i,cy+j,c); else pblend(cx+i,cy+j,c,cov); }
}

/* ---- text into SCENE ---- */
int slen(const char*s){ int n=0; while(s[n])n++; return n; }
void dchar(int x,int y,char ch,u32 c,int s){
    int idx=(u8)ch-32; if(idx<0||idx>=95)return; clip_note(y+14*s); if(y>=g_clip_y1||y+14*s<=g_clip_y0)return; if(g_gfx){ if(y<g_clip_y0||y+14*s>g_clip_y1)return; gfx_glyph(x,y,ch,c,s,0); return; } const unsigned char*g=fontaa[idx];
    (void)SCENE; for(int r=0;r<14;r++) for(int col=0;col<8;col++){ int a=g[r*8+col]; if(a<6)continue;
        for(int dy=0;dy<s;dy++) for(int dx=0;dx<s;dx++){ int px=x+col*s+dx, py=y+r*s+dy; if((u32)px<W&&(u32)py<H&&py>=g_clip_y0&&py<g_clip_y1){ u32*p=&g_scene[(u64)py*SW+px]; *p=a>=250?c:mix(*p,c,a); } } }
}
static const char U8L1[65]="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
char u8fold(const char* s,int* n){ u8 c=(u8)s[0]; if(c<0x80){ *n=1; return (char)c; }
    if(c==0xC3&&((u8)s[1]&0xC0)==0x80){ *n=2; return U8L1[(u8)s[1]-0x80]; }
    if(c>=0xC2){ int k=c>=0xF0?4:c>=0xE0?3:2, i=1; while(i<k&&((u8)s[i]&0xC0)==0x80)i++; *n=i; return '?'; }
    *n=1; return (char)c; }
int dstr(int x,int y,const char*s,u32 c,int sc){ while(*s){ int n; char ch=u8fold(s,&n); dchar(x,y,ch,c,sc); x+=8*sc; s+=n; } return x; }
void dstr_ctr(int cx,int y,const char*s,u32 c,int sc){ dstr(cx-(slen(s)*8*sc)/2,y,s,c,sc); }
/* ---- proportional text (variable width from the 8x14 bitmap ink bounds) ---- */
int g_gl[95], g_gw[95], g_gm_ready=0;
void gmetrics(void){ for(int idx=0;idx<95;idx++){ const unsigned char*g=fontaa[idx]; int lo=8,hi=-1;
        for(int r=0;r<14;r++){ for(int c=0;c<8;c++){ if(g[r*8+c]>40){ if(c<lo)lo=c; if(c>hi)hi=c; } } }
        if(hi<0){ g_gl[idx]=0; g_gw[idx]=0; } else { g_gl[idx]=lo; g_gw[idx]=hi-lo+1; } } g_gm_ready=1; }
int gadv(char ch,int s){ int idx=(u8)ch-32; if(idx<0||idx>=95)return 4*s; if(g_gw[idx]==0)return 4*s; return (g_gw[idx]+1)*s; }
int pw_sub(const char*str,int a,int b,int s){ int w=0; for(int i=a;str[i]&&i<b;i++)w+=gadv(str[i],s); return w; }
int pw_str(const char*str,int s){ int w=0; for(int i=0;str[i];){ int n; char ch=u8fold(str+i,&n); w+=gadv(ch,s); i+=n; } return w; }
static void pdch(int x,int y,char ch,u32 c,int s){ int idx=(u8)ch-32; if(idx<0||idx>=95||g_gw[idx]==0)return; clip_note(y+14*s); if(y>=g_clip_y1||y+14*s<=g_clip_y0)return; const unsigned char*gp=fontaa[idx]; int ox=x-g_gl[idx]*s; if(g_gfx){ if(y<g_clip_y0||y+14*s>g_clip_y1)return; gfx_glyph(ox,y,ch,c,s,1); return; }
    (void)SCENE; for(int r=0;r<14;r++){ for(int col=0;col<8;col++){ int a=gp[r*8+col]; if(a<6)continue;
        if(s==1){ int px=ox+col,py=y+r; if((u32)px<W&&(u32)py<H&&py>=g_clip_y0&&py<g_clip_y1){ u32*p=&g_scene[(u64)py*SW+px]; *p=mix(*p,c,a); } }
        else { for(int dy=0;dy<s;dy++)for(int dx=0;dx<s;dx++){ int px=ox+col*s+dx,py=y+r*s+dy; if((u32)px<W&&(u32)py<H&&py>=g_clip_y0&&py<g_clip_y1){ u32*p=&g_scene[(u64)py*SW+px]; *p=mix(*p,c,a); } } } } } }
int pdstr(int x,int y,const char*str,u32 c,int s){ for(int i=0;str[i];){ int n; char ch=u8fold(str+i,&n); pdch(x,y,ch,c,s); x+=gadv(ch,s); i+=n; } return x; }
int pdsub(int x,int y,const char*str,int a,int b,u32 c,int s){ for(int i=a;str[i]&&i<b;i++){ pdch(x,y,str[i],c,s); x+=gadv(str[i],s); } return x; }
void pdstr_ctr(int cx,int y,const char*s,u32 c,int sc){ pdstr(cx-pw_str(s,sc)/2,y,s,c,sc); }

/* ---- present SCENE -> hardware framebuffer (honours pitch + bpp) ---- */
    /* fwd: OLED setting + PIT ticks */
static u32* g_shadow=0; static u32 g_sh_w=0, g_sh_h=0, g_sh_sw=0; static int g_sh_valid=0;
int g_dmg_on=1; u32 g_dmg_frames=0, g_dmg_full=0, g_dmg_idle=0, g_dmg_last_px=0, g_dmg_last_n=0, g_dmg_gpu=0;
static dmg_rect g_dr[DMG_MAX];
void fb_shadow_invalidate(void){ g_sh_valid=0; }
static int sh_ready(void){
    if(!g_dmg_on)return 0;
    if(!g_shadow||g_sh_w!=W||g_sh_h!=H||g_sh_sw!=SW){ if(g_shadow)kfree(g_shadow); g_shadow=(u32*)kmalloc((unsigned long)SW*H*4UL+64UL); g_sh_valid=0;
        if(!g_shadow){ g_dmg_on=0; return 0; } g_sh_w=W; g_sh_h=H; g_sh_sw=SW; }
    return 1; }
void bkl_yield(void);
static void sh_rect(int x,int y,int w,int h){ for(int r=0;r<h;r++){ if((r&63)==63)bkl_yield(); u32* d=g_shadow+(u64)(y+r)*SW+(u32)x; const u32* s=SCENE+(u64)(y+r)*SW+(u32)x; if(g_accel2d)sse_copy32(d,s,w); else for(int i=0;i<w;i++)d[i]=s[i]; } }
void fb_shadow_note(int x,int y,int w,int h){
    if(!g_sh_valid||!g_shadow||g_sh_w!=W||g_sh_h!=H||g_sh_sw!=SW)return;
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;} if(x+w>(int)W)w=(int)W-x; if(y+h>(int)H)h=(int)H-y; if(w<=0||h<=0)return;
    sh_rect(x,y,w,h); }
static void cpu_rect(int x,int y,int w,int h){
    for(int r=0;r<h;r++){ if((r&31)==31)bkl_yield(); u32* d=(u32*)(FBB+(u64)(y+r)*PITCHB)+x; const u32* s=SCENE+(u64)(y+r)*SW+(u32)x;
        if(g_fb_rgb)copy_swap32(d,s,w); else if(g_accel2d)sse_copy32(d,s,w); else for(int i=0;i<w;i++)d[i]=s[i]; } }
static int dmg_present(void){
    const u32* sc=SCENE; int full=!g_sh_valid; int n=0;
    if(!full){ n=dmg_rects(sc,g_shadow,SW,(int)W,(int)H,g_dr,DMG_MAX); if(n<0||dmg_area(g_dr,n)*10>(u64)W*H*7)full=1; }
    if(full){ g_dr[0].x=0; g_dr[0].y=0; g_dr[0].w=(int)W; g_dr[0].h=(int)H; n=1; g_dmg_full++; }
    if(!n){ g_dmg_idle++; g_dmg_last_n=0; g_dmg_last_px=0; g_dmg_frames++; return 1; }
    int gpu=0;
    if(!g_fb_rgb&&nvgsp_present_on()) gpu=full?nvgsp_fb_present(sc,SW,W,H,(volatile u32*)FBB,PITCHB):nvgsp_fb_present_rects(sc,SW,W,H,g_dr,n,(volatile u32*)FBB,PITCHB);
    if(!gpu&&!g_fb_rgb){ extern int g_amdsdma_sys; extern int amdsdma_present_rects(const u32*,u32,u32,u32,const dmg_rect*,int,u8*,u32); if(g_amdsdma_sys)gpu=amdsdma_present_rects(sc,SW,W,H,g_dr,n,FBB,PITCHB); }
    if(!gpu) for(int i=0;i<n;i++)cpu_rect(g_dr[i].x,g_dr[i].y,g_dr[i].w,g_dr[i].h); else g_dmg_gpu++;
    for(int i=0;i<n;i++)sh_rect(g_dr[i].x,g_dr[i].y,g_dr[i].w,g_dr[i].h);
    g_sh_valid=1; g_dmg_frames++; g_dmg_last_n=(u32)n; g_dmg_last_px=(u32)dmg_area(g_dr,n); return 1; }
u32 g_fb_fww=0, g_fb_fwh=0; int g_fb_native=0;
extern u32* SCENE_NC; extern u32 SCENE_NC_STATIC[MAXW*MAXH];
extern int fs_find_name(const char*); extern char* fsd(int); extern int fs_save_bin(const char*,const u8*,int,int); extern void nvx3_sync_all(void);
static u32 *db_s=0, *db_b=0, *db_n=0; static u32 db_w=0, db_h=0; u32 g_desk_cpu_mb=0, g_desk_scale=0; char g_desk_why[96]; static int g_desk_sl=0; volatile int g_desk_rescale=0;
static int db_inuse(void){ return db_s&&(g_scene==db_s||BG==db_b||SCENE_NC==db_n); }
static void db_free(void){ if(db_inuse())return; kfree(db_s); kfree(db_b); kfree(db_n); db_s=db_b=db_n=0; db_w=db_h=0; g_desk_cpu_mb=0; }
static int db_get(u32 w,u32 h){
    if(db_s&&db_w==w&&db_h==h)return 1;
    if(db_inuse())return 0;
    db_free();
    unsigned long n=(unsigned long)w*h*4UL; db_s=(u32*)kmalloc(n); db_b=db_s?(u32*)kmalloc(n):0; db_n=db_b?(u32*)kmalloc(n):0;
    void* pr=db_n?kmalloc(n+(unsigned long)DESK_RESERVE):0;
    if(!pr){ kfree(db_s); kfree(db_b); kfree(db_n); db_s=db_b=db_n=0; char* q=scopy(g_desk_why,"not enough memory for a "); q=udec(q,w); q=scopy(q,"x"); q=udec(q,h); q=scopy(q," desktop - scaled instead"); *q=0; return 0; }
    kfree(pr); db_w=w; db_h=h; g_desk_cpu_mb=(u32)(((u64)n*3u)>>20); g_desk_why[0]=0; return 1; }
u32 fb_desk_scale(void){
    if(!g_desk_sl){ g_desk_sl=1; int k=fs_find_name("deskscale.cfg"); const char* d=k>=0?fsd(k):0; u32 v=0; int ok=d!=0;
        for(int i=0;ok&&i<3;i++){ if(d[i]<'0'||d[i]>'9')ok=0; else v=v*10u+(u32)(d[i]-'0'); } if(ok&&desk_pct_ok(v))g_desk_scale=v; }
    return g_desk_scale; }
int fb_desk_scale_set(u32 p){
    if(!desk_pct_ok(p))return -1;
    fb_desk_scale(); g_desk_scale=p; u8 b[4]={(u8)('0'+p/100),(u8)('0'+p/10%10),(u8)('0'+p%10),'\n'};
    fs_save_bin("deskscale.cfg",b,4,0); nvx3_sync_all(); g_desk_rescale=1; return 0; }
int fb_desktop_resize(u32 w,u32 h,int apply){
    if(!g_fb_fww){ g_fb_fww=W; g_fb_fwh=H; }
    int back=w==g_fb_fww&&h==g_fb_fwh, big=desk_big(w,h,MAXW,MAXH);
    if(!back&&(w<640||h<480||w>DESK_MAXW||h>DESK_MAXH||BYPP!=4||g_fb_rgb||g_panel_oled))return 0;
    if(big&&!db_get(w,h))return 0;
    if(!big&&!apply)db_free();
    if(!apply||(w==W&&h==H))return 1;
    if(g_gfx)gfx_stop("the desktop size changed");
    u32 ow=W, oh=H; W=w; H=h; SW=w; g_fb_native=!back; fb_shadow_invalidate();
    if(big){ g_scene=db_s; BG=db_b; SCENE_NC=db_n; }
    else { if(g_scene==db_s)g_scene=SCENE_STATIC; if(BG==db_b)BG=BG_STATIC; if(SCENE_NC==db_n)SCENE_NC=SCENE_NC_STATIC; db_free(); }
    for(int i=0;i<NWIN;i++){ Win* n=&WN[i]; out_resize_win(&n->x,&n->y,&n->w,&n->h,n->max,0,ow,oh,W,H); out_resize_win(&n->sx,&n->sy,&n->sw,&n->sh,0,1,ow,oh,W,H); }
    { extern int mx, my; int mo=out_resize_pt(&mx,&my,ow,oh,W,H); out_sync(); if(mo!=1)out_mouse_clamp(&mx,&my,(int)W,(int)H); }
    build_bg(); return 1; }
static int nvflip_dmg(void){
    const u32* sc=SCENE; int n=-1;
    if(sh_ready()&&g_sh_valid){ n=dmg_rects(sc,g_shadow,SW,(int)W,(int)H,g_dr,DMG_MAX); if(n<0||n>NVFLIP_RMAX||dmg_area(g_dr,n)*10>(u64)W*H*7)n=-1; }
    u32 w0=W, h0=H; if(!nvflip_present_rects(sc,SW,W,H,g_dr,n))return 0;
    if(W!=w0||H!=h0||!g_shadow||g_sh_w!=W||g_sh_h!=H||g_sh_sw!=SW){ g_sh_valid=0; return 1; }
    if(n<0)sh_rect(0,0,(int)W,(int)H); else for(int i=0;i<n;i++)sh_rect(g_dr[i].x,g_dr[i].y,g_dr[i].w,g_dr[i].h);
    g_sh_valid=1; if(n<0)g_dmg_full++; else if(!n)g_dmg_idle++; g_dmg_frames++; return 1; }
void blit_all(void){
    g_scene_quiet++; void blit_all2(void); blit_all2(); g_scene_quiet--; }
void blit_all2(void){
    if(g_desk_rescale){ g_desk_rescale=0; if(g_nvflip_live){ nvflip_off("the desktop scale changed"); if(g_fb_native)fb_desktop_resize(g_fb_fww,g_fb_fwh,1); nvflip_on(); } }
    if(!g_nvflip_live&&!g_fb_native&&db_s)db_free();
    if(g_gfx)gfx_sync();
    { extern int g_igpu_flip; extern void igpu_pre_blit(void); if(g_igpu_flip)igpu_pre_blit(); }
    /* OLED care: shift the whole image by 1px through 4 positions, changing every ~30s */
    int shx=0,shy=0; if(g_panel_oled){ int ph=(int)((g_ticks/3000ULL)&3); shx=(ph==1)?1:(ph==3)?-1:0; shy=(ph==2)?1:0; }
    { static int s_fl=0; if(s_fl!=g_nvflip_live){ s_fl=g_nvflip_live; g_sh_valid=0; } }
    if(g_fb_native&&!nvflip_native(0,0)){ fb_desktop_resize(g_fb_fww,g_fb_fwh,1); goto presented; }
    if(g_nvflip_live){ if(BYPP==4&&!g_fb_rgb&&shx==0&&shy==0&&nvflip_dmg())goto presented; g_sh_valid=0; if(g_fb_native)goto presented; }
    { extern int g_amdflip_live; extern int amdflip_present(const u32*,u32,u32,u32); if(g_amdflip_live){ g_sh_valid=0; if(BYPP==4&&!g_fb_rgb&&shx==0&&shy==0&&amdflip_present(SCENE,SW,W,H))goto presented; } }
    { extern int g_igpu_flip; if(BYPP==4&&shx==0&&shy==0&&!g_igpu_flip&&sh_ready()&&dmg_present())goto presented; g_sh_valid=0; }
    { extern int g_igpu_flip, g_blt_present; extern int igpu_blt_present(void); if(g_igpu_flip&&g_blt_present&&!g_fb_rgb&&BYPP==4&&shx==0&&shy==0&&igpu_blt_present()==0)goto presented; }
    { extern int g_igpu_flip; extern void igpu_present_dirty_all(void); if(g_igpu_flip)igpu_present_dirty_all(); }
    {
      if(BYPP==4&&!g_fb_rgb&&shx==0&&shy==0&&nvgsp_present_on()&&nvgsp_fb_present(SCENE,SW,W,H,(volatile u32*)FBB,PITCHB))goto presented; }
    for(u32 y=0;y<H;y++){ int sr=(int)y-shy; if(sr<0)sr=0; if(sr>=(int)H)sr=(int)H-1; u32*s=SCENE+(u64)sr*SW;
        if(BYPP==4){ u32*d=(u32*)(FBB+(u64)y*PITCHB);
            if(g_fb_rgb){ if(shx==0)copy_swap32(d,s,(int)W); else for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; copy_swap32(d+x,s+sx,1); } }
            else if(!g_accel2d){ for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; d[x]=s[sx]; } }
            else if(shx==0) sse_copy32(d,s,(int)W);
            else if(shx==1){ d[0]=s[0]; sse_copy32(d+1,s,(int)W-1); }
            else { sse_copy32(d,s+1,(int)W-1); d[W-1]=s[W-1]; } }
        else { u8*d=FBB+(u64)y*PITCHB;
            if(g_accel2d && shx==0) sse_pack24(d,s,(int)W);
            else for(u32 x=0;x<W;x++){ int sx=(int)x-shx; if(sx<0)sx=0; if(sx>=(int)W)sx=(int)W-1; u32 c=s[sx]; d[0]=(u8)c;d[1]=(u8)(c>>8);d[2]=(u8)(c>>16); d+=3; } } }
    presented:
    { extern int g_igpu_flip; extern void igpu_post_blit(void); if(g_igpu_flip)igpu_post_blit(); }
    if(g_fb2_ok){ display_cfg_tick(); if(g_mirror2)blit_mirror2(); else if(blit_extend2())blit_mirror2(); }
    else if(g_disp_extend)display_hotplug(0);
}

/* ---- mirror the composed SCENE onto a second firmware-lit display (nearest-neighbour scale) ---- */
void blit_mirror2(void){
    if(!FBB2||!W2||!H2||!W||!H)return;
    for(u32 y=0;y<H2;y++){
        u32 sy=(u32)((u64)y*H/H2); if(sy>=H)sy=H-1; u32* srow=SCENE+(u64)sy*SW;
        if(BYPP2==4){ u32* d=(u32*)(FBB2+(u64)y*PITCHB2);
            for(u32 x=0;x<W2;x++){ u32 sx=(u32)((u64)x*W/W2); if(sx>=W)sx=W-1; u32 v=srow[sx];
                d[x]=g_fb2_rgb?((v&0xFF00FF00u)|((v>>16)&0xFFu)|((v&0xFFu)<<16)):v; } }
        else { u8* d=FBB2+(u64)y*PITCHB2;
            for(u32 x=0;x<W2;x++){ u32 sx=(u32)((u64)x*W/W2); if(sx>=W)sx=W-1; u32 c=srow[sx]; d[0]=(u8)c;d[1]=(u8)(c>>8);d[2]=(u8)(c>>16); d+=3; } }
    }
}

