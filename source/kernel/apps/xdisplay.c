#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/net/http.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/mm/pmm_vmm.h"
#include "include/nvx/arch/x86_64/hrtimer.h"

int desk_win_exposed(int i);
void present_rect(int x,int y,int w,int h);

u64* g_xfb_pg; int g_xfb_n; int g_xfb_live; extern int g_xfull;
static u64 g_xd_last;

void xfb_register(u64* frames,int n){ if(g_xfb_pg)kfree(g_xfb_pg); g_xfb_pg=(u64*)kmalloc(sizeof(u64)*(unsigned long)(n?n:1)); if(!g_xfb_pg){ g_xfb_n=0; return; } for(int i=0;i<n;i++)g_xfb_pg[i]=frames[i]; g_xfb_n=n; g_xfb_live=1; }
static u64 g_xd_cr3, g_xd_va; static u64** g_xd_pte; static u8* g_xd_bits; static u8* g_xd_prev; static int g_xd_ok, g_xd_cap; static u64 g_xd_bt;
u32 g_xd_pages, g_xd_scans, g_xd_fulls; u64 g_xd_ns, g_xd_calls, g_xd_cns;
void xfb_drop(u64 frame){ for(int i=0;i<g_xfb_n;i++) if(g_xfb_pg[i]==frame){ g_xfb_live=0; g_xfb_n=0; g_xd_ok=0; return; } }
void xfb_track(u64 cr3,u64 va,int n){
    g_xd_ok=0; cr3&=~0xFFFULL; if(n<=0||n!=g_xfb_n)return;
    if(n>g_xd_cap){ if(g_xd_pte)kfree(g_xd_pte); if(g_xd_bits)kfree(g_xd_bits); g_xd_pte=(u64**)kmalloc(sizeof(u64*)*(unsigned long)n); g_xd_bits=(u8*)kmalloc((unsigned long)(n+7)/8); if(g_xd_prev)kfree(g_xd_prev); g_xd_prev=(u8*)kmalloc((unsigned long)(n+7)/8); if(!g_xd_pte||!g_xd_bits||!g_xd_prev){ g_xd_cap=0; return; } g_xd_cap=n; }
    for(int i=0;i<n;i++){ u64* e=vm_walk(cr3,va+((u64)i<<12),0); if(!e||!(*e&1)||(*e&PTE_ADDR)!=g_xfb_pg[i])return; g_xd_pte[i]=e; }
    for(int i=0;i<(n+7)/8;i++){ g_xd_bits[i]=0xFF; g_xd_prev[i]=0; }
    g_xd_cr3=cr3; g_xd_va=va; g_xd_ok=1; g_xd_bt=0; }
void xfb_untrack_cr3(u64 cr3){ if(g_xd_ok&&(cr3&~0xFFFULL)==g_xd_cr3)g_xd_ok=0; }
void xfb_untrack_range(u64 cr3,u64 a,u64 e){ if(g_xd_ok&&(cr3&~0xFFFULL)==g_xd_cr3&&a<g_xd_va+((u64)g_xfb_n<<12)&&e>g_xd_va)g_xd_ok=0; }
int g_xd_off;
static int xfb_collect(void){
    if(!g_xd_ok||!g_xd_bits||g_xd_off)return -1;
    if(g_ticks-g_xd_bt>=300){ g_xd_bt=g_ticks; for(int i=0;i<(g_xfb_n+7)/8;i++)g_xd_bits[i]=0xFF; g_xd_fulls++; }
    int n=0; u64 t0=hr_ns();
    for(int i=0;i<g_xfb_n;i++){ u64 e=*g_xd_pte[i]; if(!(e&1)||(e&PTE_ADDR)!=g_xfb_pg[i]){ g_xd_ok=0; return -1; }
        if(e&0x40ULL){ __atomic_fetch_and(g_xd_pte[i],~0x40ULL,__ATOMIC_SEQ_CST); g_xd_bits[i>>3]|=(u8)(1<<(i&7)); n++; } }
    if(n){ extern void smp_tlb_shootdown(void); smp_tlb_shootdown(); if((cpu_get_pt()&~0xFFFULL)==g_xd_cr3)cpu_set_pt(cpu_get_pt()); g_xd_pages+=(u32)n; }
    for(int i=0;i<(g_xfb_n+7)/8;i++){ u8 nw=(u8)(g_xd_bits[i]&~g_xd_prev[i]); u8 cur=g_xd_bits[i]; g_xd_bits[i]|=g_xd_prev[i]; g_xd_prev[i]=cur; (void)nw; }
    g_xd_cns+=hr_ns()-t0;
    for(int i=0;i<(g_xfb_n+7)/8;i++) if(g_xd_bits[i])return 1;
    return 0; }
static int xd_pg(int i){ return (g_xd_bits[i>>3]>>(i&7))&1; }
static void xd_clear(void){ for(int i=0;i<(g_xfb_n+7)/8;i++)g_xd_bits[i]=0; }
static int xd_rows(u64 d,u32 bpl,u32 h,int* r0,int* r1){
    int a=-1,b=-1; for(int i=0;i<g_xfb_n;i++){ if(!xd_pg(i))continue; u64 s=(u64)i<<12, e=s+4096; if(e<=d)continue; long y0=s<d?0:(long)((s-d)/bpl), y1=(long)((e-1-d)/bpl); if(y0>=(long)h)continue; if(y1>=(long)h)y1=(long)h-1; if(a<0||y0<a)a=(int)y0; if(y1>b)b=(int)y1; }
    *r0=a; *r1=b; return a>=0; }
int xfb_track_ok(void){ return g_xd_ok; }

static u32 xfb_u32(u64 off){ u64 pg=off>>12; if((int)pg>=g_xfb_n)return 0; const u8* p=(const u8*)g_xfb_pg[pg]+(off&0xFFF); return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u32 bsw(u32 v){ return (v>>24)|((v>>8)&0xFF00u)|((v<<8)&0xFF0000u)|(v<<24); }
static int xfb_geom(u32* w,u32* h,u32* bpl,u64* data){
    if(!g_xfb_live||g_xfb_n<1)return 0;
    u32 hs=xfb_u32(0); int sw=0; if(hs>0x10000){ sw=1; hs=bsw(hs); }
    #define F(k) (sw?bsw(xfb_u32((u64)(k)*4)):xfb_u32((u64)(k)*4))
    u32 bpp=F(11); *w=F(4); *h=F(5); *bpl=F(12); u32 nc=F(19);
    #undef F
    if(bpp!=32||*w==0||*h==0||*w>4096||*h>4096)return 0;
    *data=(u64)hs+(u64)nc*12; if(*data+(u64)(*bpl)*(*h)>(u64)g_xfb_n*4096)return 0; return 1; }
static void xd_area(int i,int* x,int* y,int* w,int* h){ *x=WN[i].x+2; *y=WN[i].y+TITLEH+2; *w=WN[i].w-4; *h=WN[i].h-TITLEH-4; }
static void xd_blit_rows(int i,int ra,int rb){
    int ax,ay,aw,ah; xd_area(i,&ax,&ay,&aw,&ah); u32 w,h,bpl; u64 d;
    if(!xfb_geom(&w,&h,&bpl,&d))return;
    int cw=aw<(int)w?aw:(int)w, chh=ah<(int)h?ah:(int)h; if(rb>=chh)rb=chh-1;
    for(int r=ra;r<=rb;r++){ int sy=ay+r; if(sy<0||(u32)sy>=H)continue; u64 o=d+(u64)r*bpl;
        for(int c=0;c<cw;){ u64 oo=o+(u64)c*4; u64 pg=oo>>12; if((int)pg>=g_xfb_n)break; int run=(int)((4096-(oo&0xFFF))/4); if(run>cw-c)run=cw-c;
            const u32* src=(const u32*)((const u8*)g_xfb_pg[pg]+(oo&0xFFF)); int sx=ax+c;
            for(int k=0;k<run;k++){ int px=sx+k; if((u32)px<W)SCENE[(u32)sy*SW+(u32)px]=src[k]&0xFFFFFFu; } c+=run; } } }
static void xd_blit(int i){ xd_blit_rows(i,0,1<<30); }
void xdisp_draw(int i){
    u32 w,h,bpl; u64 d; int ok=xfb_geom(&w,&h,&bpl,&d);
    char t[64],*p=t; p=scopy(p,"X Display  :0"); if(ok){ p=scopy(p,"  -  "); p=udec(p,w); *p++='x'; p=udec(p,h); } *p=0;
    win_frame(i,t);
    int ax,ay,aw,ah; xd_area(i,&ax,&ay,&aw,&ah); fill_rect(ax,ay,aw,ah,rgb(16,18,24));
    if(!ok){ dstr(ax+20,ay+20,"No X server is running.",rgb(230,230,240),1);
        dstr(ax+20,ay+44,"Start one in the Terminal:  startx",rgb(160,200,255),1);
        dstr(ax+20,ay+68,"Then run X programs with DISPLAY=127.0.0.1:0, for example:  xdemo &",rgb(160,170,190),1); return; }
    xd_blit(i); }
int xdisp_fast(void){
    if(!WN[W_XDISP].open||WN[W_XDISP].min||!g_xfb_live)return 0;
    if(g_ticks-g_xd_last<5)return 0; g_xd_last=g_ticks;
    int dv=xfb_collect(); if(dv==0)return 0;
    if(!desk_win_exposed(W_XDISP)){ if(dv>0)xd_clear(); return 1; }
    int ax,ay,aw,ah; xd_area(W_XDISP,&ax,&ay,&aw,&ah);
    if(dv>0){ u32 w,h,bpl; u64 d; int r0,r1; if(xfb_geom(&w,&h,&bpl,&d)&&xd_rows(d,bpl,h,&r0,&r1)){ xd_clear(); if(r0>=ah)return 0; if(r1>=ah)r1=ah-1; xd_blit_rows(W_XDISP,r0,r1); present_rect(ax,ay+r0,aw,r1-r0+1); return 0; } xd_clear(); }
    xd_blit(W_XDISP); present_rect(ax,ay,aw,ah); return 0; }
static u8 g_xq[512][8]; static int g_xq_h, g_xq_t; static int g_xb_prev, g_xr_prev, g_xmx=-1, g_xmy=-1;
static void xq_push(int type,int code,int down,int x,int y){ int n=(g_xq_t+1)&511; if(n==g_xq_h)return; u8* r=g_xq[g_xq_t]; r[0]=(u8)type; r[1]=(u8)code; r[2]=(u8)down; r[3]=0; r[4]=(u8)x; r[5]=(u8)(x>>8); r[6]=(u8)y; r[7]=(u8)(y>>8); g_xq_t=n; }
int xin_read(char* buf,int len){ int c=0; while(c+8<=len&&g_xq_h!=g_xq_t){ for(int k=0;k<8;k++)buf[c+k]=(char)g_xq[g_xq_h][k]; g_xq_h=(g_xq_h+1)&511; c+=8; } return c; }
int xin_ready(void){ return g_xq_h!=g_xq_t; }
void xdisp_mouse(int btn,int rbtn,int sc){
    extern int g_xover; if(g_xfull||g_xover)return;
    if(!g_xfb_live||!WN[W_XDISP].open||WN[W_XDISP].min||focus!=W_XDISP){ g_xb_prev=btn; g_xr_prev=rbtn; return; }
    int ax,ay,aw,ah; xd_area(W_XDISP,&ax,&ay,&aw,&ah); int in=mx>=ax&&my>=ay&&mx<ax+aw&&my<ay+ah; int x=mx-ax, y=my-ay;
    if(in&&(x!=g_xmx||y!=g_xmy)){ xq_push(1,0,0,x,y); g_xmx=x; g_xmy=y; }
    if(btn!=g_xb_prev&&(in||!btn)){ xq_push(2,1,btn?1:0,x,y); }
    if(rbtn!=g_xr_prev&&(in||!rbtn)){ xq_push(2,3,rbtn?1:0,x,y); }
    if(in&&sc){ int b=sc>0?4:5; xq_push(2,b,1,x,y); xq_push(2,b,0,x,y); }
    g_xb_prev=btn; g_xr_prev=rbtn; }
void xdisp_key(int c){ if(!g_xfb_live)return; extern volatile unsigned char kdown[128]; extern int kalt; int n=(g_xq_t+1)&511; if(n==g_xq_h)return; u8* r=g_xq[g_xq_t]; r[0]=3; r[1]=(u8)(c&0xFF); r[2]=1; r[3]=(u8)((kdown[0x1D]?1:0)|(kalt?2:0)); r[4]=r[5]=r[6]=r[7]=0; g_xq_t=n; }
int g_xfull; static u64 g_xf_last; static int g_xf_drawn;
extern int g_fb0_open, g_fb0_dirty;
void xfull_enter(void){ if(g_xfull||!g_xfb_live)return; g_xfull=1; g_fb0_open++; g_xf_last=0; g_xf_drawn=0; }
void xfull_exit(void){ if(!g_xfull)return; g_xfull=0; if(g_fb0_open>0)g_fb0_open--; }
int xdisp_pending_tick(void);
void xfull_tick(void){
    xdisp_pending_tick(); if(!g_xfull)return; if(!g_xfb_live){ xfull_exit(); return; }
    if(g_ticks-g_xf_last<(u64)(xfb_track_ok()&&!g_xd_off?1:4))return; g_xf_last=g_ticks;
    u32 w,h,bpl; u64 d; if(!xfb_geom(&w,&h,&bpl,&d))return;
    u32 ya=0, yb=H; int dv=xfb_collect(); if(dv==0)return;
    if(dv>0&&g_xf_drawn){ int r0,r1; if(!xd_rows(d,bpl,h,&r0,&r1)){ xd_clear(); return; } xd_clear(); ya=(u32)r0; yb=(u32)r1+1; if(yb>H)yb=H; if(ya>=yb)return; }
    else if(dv>0)xd_clear();
    g_xf_drawn=1;
    for(u32 y=ya;y<yb;y++){ u32* row=&SCENE[y*SW]; if(y>=h){ for(u32 x=0;x<W;x++)row[x]=0; continue; }
        u64 o=d+(u64)y*bpl; u32 x=0;
        while(x<W&&x<w){ u64 oo=o+(u64)x*4; u64 pg=oo>>12; if((int)pg>=g_xfb_n)break; u32 run=(u32)((4096-(oo&0xFFF))/4); if(run>w-x)run=w-x; if(run>W-x)run=W-x;
            const u32* src=(const u32*)((const u8*)g_xfb_pg[pg]+(oo&0xFFF)); for(u32 k=0;k<run;k++)row[x+k]=src[k]&0xFFFFFFu; x+=run; }
        for(;x<W;x++)row[x]=0; }
    g_fb0_dirty=1; }
void xfull_mouse(int btn,int rbtn,int sc){
    if(mx!=g_xmx||my!=g_xmy){ xq_push(1,0,0,mx,my); g_xmx=mx; g_xmy=my; }
    if(btn!=g_xb_prev)xq_push(2,1,btn?1:0,mx,my);
    if(rbtn!=g_xr_prev)xq_push(2,3,rbtn?1:0,mx,my);
    if(sc){ int b=sc>0?4:5; xq_push(2,b,1,mx,my); xq_push(2,b,0,mx,my); }
    g_xb_prev=btn; g_xr_prev=rbtn; }
int g_xfull_pending;
int xdisp_pending_tick(void){ if(g_xfull_pending&&g_xfb_live){ g_xfull_pending=0; xfull_enter(); return 1; } return 0; }
#define XKEY 0x00010203u
static u32* g_xshadow; static u32 g_xsh_w, g_xsh_h; static int g_xo_full=1;
int g_xover, g_xkbd; static int g_xcap; static u32 g_xsum; static u64 g_xo_last;
extern int g_tby;
static int xo_geom(u32* w,u32* h,u32* bpl,u64* d){ return xfb_geom(w,h,bpl,d); }
static u32 xo_px(u64 d,u32 bpl,int x,int y){ u64 o=d+(u64)y*bpl+(u64)x*4; u64 pg=o>>12; if((int)pg>=g_xfb_n)return XKEY; return *(const u32*)((const u8*)g_xfb_pg[pg]+(o&0xFFF))&0xFFFFFFu; }
int xfb_has_window(void){ u32 w,h,bpl; u64 d; if(!xo_geom(&w,&h,&bpl,&d))return 0; int n=0;
    for(u32 y=4;y<h;y+=16) for(u32 x=4;x<w;x+=16){ u32 c=xo_px(d,bpl,(int)x,(int)y); if(c!=XKEY&&c!=0)n++; }
    return n>=20; }
int xover_hit(int x,int y){ if(!g_xover||!g_xfb_live)return 0; u32 w,h,bpl; u64 d; if(!xo_geom(&w,&h,&bpl,&d))return 0; if(x<0||y<0||(u32)x>=w||(u32)y>=h||y>=g_tby)return 0; return xo_px(d,bpl,x,y)!=XKEY; }
void xover_draw(void){
    if(!g_xover||!g_xfb_live)return; u32 w,h,bpl; u64 d; if(!xo_geom(&w,&h,&bpl,&d))return;
    if(g_xshadow&&g_xsh_w==(w<W?w:W)){ u32 mh=h<(u32)g_tby?h:(u32)g_tby; if(mh>H)mh=H; if(mh>g_xsh_h)mh=g_xsh_h;
        for(u32 y=0;y<mh;y++){ u32* row=&SCENE[y*SW]; const u32* sh=&g_xshadow[y*g_xsh_w]; for(u32 x=0;x<g_xsh_w;x++){ u32 c=sh[x]; if(c!=XKEY)row[x]=c; } } return; }
    u32 mh=h<(u32)g_tby?h:(u32)g_tby; if(mh>H)mh=H; u32 mw=w<W?w:W;
    for(u32 y=0;y<mh;y++){ u64 o=d+(u64)y*bpl; u32* row=&SCENE[y*SW]; u32 x=0;
        while(x<mw){ u64 oo=o+(u64)x*4; u64 pg=oo>>12; if((int)pg>=g_xfb_n)break; u32 run=(u32)((4096-(oo&0xFFF))/4); if(run>mw-x)run=mw-x;
            const u32* src=(const u32*)((const u8*)g_xfb_pg[pg]+(oo&0xFFF)); for(u32 k=0;k<run;k++){ u32 c=src[k]&0xFFFFFFu; if(c!=XKEY)row[x+k]=c; } x+=run; } } }
u32 g_xo_fast, g_xo_fullc;
void xover_invalidate(void){ g_xo_full=1; }
static int xover_tick2(void);
int xover_tick(void){ u64 t0=hr_ns(); int r=xover_tick2(); u64 t1=hr_ns(); g_xd_ns+=t1-t0; g_xd_calls++; return r; }
static int xover_tick2(void){
    static int idle; if(!g_xover||!g_xfb_live)return 0; if(g_ticks-g_xo_last<(u64)(g_xd_ok&&!g_xd_off?1:idle>25?8:2))return 0; g_xo_last=g_ticks;
    u32 w,h,bpl; u64 d; if(!xo_geom(&w,&h,&bpl,&d))return 0;
    u32 mw=w<W?w:W, mh=h<(u32)g_tby?h:(u32)g_tby; if(mh>H)mh=H;
    if(!g_xshadow||g_xsh_w!=mw||g_xsh_h!=mh){ if(g_xshadow)kfree(g_xshadow); g_xshadow=(u32*)kmalloc((u64)mw*mh*4); if(!g_xshadow)return 0; g_xsh_w=mw; g_xsh_h=mh; g_xo_full=1; }
    int x0=(int)mw, y0=(int)mh, x1=-1, y1=-1, tokey=0;
    u32 ya=0, yb=mh; int dv=g_xo_full?-1:xfb_collect();
    if(g_xo_full&&g_xd_ok&&g_xd_bits)xfb_collect();
    if(dv==0){ if(idle<1000)idle++; return 0; }
    if(dv>0){ int r0,r1; if(!xd_rows(d,bpl,h,&r0,&r1)){ xd_clear(); if(idle<1000)idle++; return 0; } ya=(u32)r0; yb=(u32)r1+1; if(yb>mh)yb=mh; }
    else if(g_xd_ok&&g_xd_bits)xd_clear();
    g_xd_scans++;
    for(u32 y=ya;y<yb;y++){ u64 o=d+(u64)y*bpl; u32* sh=&g_xshadow[y*mw]; u32 x=0;
        while(x<mw){ u64 oo=o+(u64)x*4; u64 pg=oo>>12; if((int)pg>=g_xfb_n)break; u32 run=(u32)((4096-(oo&0xFFF))/4); if(run>mw-x)run=mw-x;
            if(dv>0&&!xd_pg((int)pg)){ x+=run; continue; }
            const u32* src=(const u32*)((const u8*)g_xfb_pg[pg]+(oo&0xFFF));
            for(u32 k=0;k<run;k++){ u32 c=src[k]&0xFFFFFFu; if(c!=sh[x+k]){ if(c==XKEY)tokey=1; sh[x+k]=c; int xx=(int)(x+k); if(xx<x0)x0=xx; if(xx>x1)x1=xx; if((int)y<y0)y0=(int)y; if((int)y>y1)y1=(int)y; } }
            x+=run; } }
    if(dv>0)xd_clear();
    if(x1>=0)idle=0; else if(idle<1000)idle++;
    if(g_xo_full){ g_xo_full=0; g_xo_fullc++; return 1; }
    if(x1<0)return 0;
    extern int desk_popups_clear(void); extern int g_state;
    if(tokey||!desk_popups_clear()){ g_xo_fullc++; return 1; }
    for(int y=y0;y<=y1;y++){ u32* row=&SCENE[(u32)y*SW]; const u32* sh=&g_xshadow[(u32)y*mw]; for(int x=x0;x<=x1;x++){ u32 c=sh[x]; if(c!=XKEY)row[x]=c; } }
    present_rect(x0,y0,x1-x0+1,y1-y0+1); g_xo_fast++; return 0; }
int xover_mouse(int btn,int rbtn,int sc,int pressed_edge){
    if(!g_xover||!g_xfb_live)return 0;
    int over=xover_hit(mx,my);
    if(pressed_edge){ g_xkbd=over; g_xcap=over; }
    if(!over&&!g_xcap){ if(g_xb_prev||g_xr_prev){ if(g_xb_prev)xq_push(2,1,0,mx,my); if(g_xr_prev)xq_push(2,3,0,mx,my); g_xb_prev=0; g_xr_prev=0; } return 0; }
    if(mx!=g_xmx||my!=g_xmy){ xq_push(1,0,0,mx,my); g_xmx=mx; g_xmy=my; }
    if(btn!=g_xb_prev)xq_push(2,1,btn?1:0,mx,my);
    if(rbtn!=g_xr_prev)xq_push(2,3,rbtn?1:0,mx,my);
    if(sc){ int b=sc>0?4:5; xq_push(2,b,1,mx,my); xq_push(2,b,0,mx,my); }
    g_xb_prev=btn; g_xr_prev=rbtn; if(!btn&&!rbtn)g_xcap=0; return 1; }
void wm_close_xdisp(void){ extern void close_win(int); if(WN[W_XDISP].open)close_win(W_XDISP); g_xkbd=1; xover_invalidate(); }
void xstat_print(void (*out)(const char*)){
    char b[200],*p=b; p=scopy(p,"X damage tracking: "); p=scopy(p,g_xd_ok&&!g_xd_off?"on (page dirty bits)":"off (full-screen compare)"); *p++='\n'; *p=0; out(b);
    p=b; p=scopy(p,"  dirty pages seen "); p=udec(p,g_xd_pages); p=scopy(p,", scans "); p=udec(p,g_xd_scans); p=scopy(p,", safety rescans "); p=udec(p,g_xd_fulls); *p++='\n'; *p=0; out(b);
    p=b; p=scopy(p,"  partial updates "); p=udec(p,g_xo_fast); p=scopy(p,", full redraws "); p=udec(p,g_xo_fullc); *p++='\n'; *p=0; out(b);
    p=b; p=scopy(p,"  compositor time "); p=udec(p,(u32)(g_xd_ns/1000000ULL)); p=scopy(p," ms over "); p=udec(p,(u32)g_xd_calls); p=scopy(p," checks (avg "); p=udec(p,(u32)(g_xd_calls?g_xd_ns/g_xd_calls/1000ULL:0)); p=scopy(p," us), dirty-bit check "); p=udec(p,(u32)(g_xd_cns/1000000ULL)); p=scopy(p," ms\n"); *p=0; out(b);
    g_xd_ns=0; g_xd_calls=0; g_xd_cns=0; g_xd_pages=0; g_xd_scans=0; g_xd_fulls=0; g_xo_fast=0; g_xo_fullc=0; }
