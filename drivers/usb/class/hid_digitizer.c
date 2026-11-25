#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/drivers/input/input.h"
#include "include/nvx/drivers/input/touchpad/tp_gesture.h"
#include "include/nvx/drivers/usb/hid_digitizer.h"

extern volatile u64 g_ticks;
extern u32 W, H;
int g_dg_on=1, g_dg_map=DGM_FULL, g_dg_curve=2, g_dg_lefty=0, g_dg_tap=1, g_dg_touch=1, g_dg_W=0, g_dg_H=0;
dgpen g_dg_pen;
u32 g_dg_reports, g_dg_frames, g_dg_modeset;
static struct featguard dg_fg={"Pen tablets and touchscreens","feat-pentouch.cfg",1,0,3,0,0};
int dg_enabled(void){ fg_boot(&dg_fg); return dg_fg.on&&!dg_fg.dead; }
void dg_set_enabled(int on){ fg_enable(&dg_fg,on); }
const char* dg_why(void){ fg_boot(&dg_fg); return !dg_fg.on?"Turned off":dg_fg.dead?"Turned itself off after a USB set-up did not finish":""; }
static dgdev dg_dev[DG_DEV];
static dgdev dg_pend[6];
static int dg_of[64];
static dgev dg_q[DG_EVQ]; static u32 dg_qw, dg_qr;

static u32 dg_rd_u(const u8* p,int n){ u32 v=0; for(int i=0;i<n;i++)v|=(u32)p[i]<<(8*i); return v; }
static int dg_rd_s(const u8* p,int n){ u32 v=dg_rd_u(p,n); if(n==1)return (int)(int8_t)v; if(n==2)return (int)(int16_t)v; return (int)v; }
static u32 dg_equiv(u32 u){ if((u>>16)==0xFF0Du){ u32 sp=(u>>8)&0xFF, id=u&0xFF; if(sp==0)return 0x000D0000u|id; if(sp==1)return 0x00010000u|id; } return u; }
int dg_bits(const u8* p,int plen,dgfld f){ if(!f.ok||f.size<=0||f.size>32)return 0; u32 v=0; for(int b=0;b<f.size;b++){ int bit=f.off+b; if(bit<0||(bit>>3)>=plen)return 0; if(p[bit>>3]&(1u<<(bit&7)))v|=1u<<b; }
    if(f.sgn&&f.size<32&&(v&(1u<<(f.size-1))))v|=~0u<<f.size; return (int)v; }
void dg_setbits(u8* p,int plen,dgfld f,u32 val){ if(!f.ok)return; for(int b=0;b<f.size&&b<32;b++){ int bit=f.off+b; if(bit<0||(bit>>3)>=plen)return; if(val&(1u<<b))p[bit>>3]|=(u8)(1u<<(bit&7)); else p[bit>>3]&=(u8)~(1u<<(bit&7)); } }

int dg_parse(const u8* d,int n,dglay* L){
    for(u32 i=0;i<sizeof(*L);i++)((u8*)L)[i]=0;
    if(!d||n<=0)return 0;
    struct { u32 page; int lmin,lmax,rsize,rid,rcount; } G, GS[8]; int gsp=0;
    for(u32 i=0;i<sizeof(G);i++)((u8*)&G)[i]=0;
    u32 usage[32]; int nu=0; u32 umin=0, umax=0; int hasmin=0, hasmax=0;
    u32 coll[16]; int csp=0, cov=0, finger=-1;
    static int inbits[256], fbits[256]; for(int i=0;i<256;i++){ inbits[i]=0; fbits[i]=0; }
    int i=0;
    while(i<n){
        u8 pre=d[i]; if(pre==0xFE){ if(i+2>=n||i+3+d[i+1]>n)break; i+=3+d[i+1]; continue; }
        int sz=pre&3; if(sz==3)sz=4; if(i+1+sz>n)break;
        int type=(pre>>2)&3, tag=(pre>>4)&0xF; const u8* v=d+i+1; u32 uv=dg_rd_u(v,sz); int sv=sz?dg_rd_s(v,sz):0; i+=1+sz;
        if(type==1){
            switch(tag){ case 0: G.page=uv; break; case 1: G.lmin=sv; break; case 2: G.lmax=(G.lmin<0)?sv:(int)uv; break; case 7: G.rsize=(int)uv; break;
                case 8: G.rid=(int)(uv&0xFF); L->uses_ids=1; break; case 9: G.rcount=(int)uv; break;
                case 10: if(gsp<8)GS[gsp++]=G; break; case 11: if(gsp>0)G=GS[--gsp]; break; }
            continue; }
        if(type==2){
            u32 fu=dg_equiv((sz==4)?uv:((G.page<<16)|uv));
            if(tag==0){ if(nu<32)usage[nu++]=fu; } else if(tag==1){ umin=fu; hasmin=1; } else if(tag==2){ umax=fu; hasmax=1; }
            continue; }
        if(type!=0)continue;
        if(tag==10){ u32 cu=nu?usage[0]:(hasmin?umin:0);
            if(csp<16)coll[csp++]=cu; else cov++;
            if(cu==0x000D0022u&&csp>=2&&coll[0]==0x000D0004u){ finger=L->nf<DG_MAXC?L->nf++:-1; }
            nu=0; hasmin=hasmax=0; continue; }
        if(tag==12){ if(cov>0)cov--; else if(csp>0){ if(coll[csp-1]==0x000D0022u)finger=-1; csp--; } nu=0; hasmin=hasmax=0; continue; }
        if(tag==8||tag==11){
            int feat=(tag==11), rid=G.rid; u32 app=csp?coll[0]:0; int* bits=feat?fbits:inbits;
            if(G.rsize<=0||G.rsize>32||G.rcount<0||G.rcount>4096){
                if(G.rsize>0&&G.rsize<=4096&&G.rcount>0&&G.rcount<=65536){ long long nb=(long long)bits[rid]+(long long)G.rsize*G.rcount; if(nb>(1<<20))nb=1<<20; bits[rid]=(int)nb; }
                if(!feat)L->in_len[rid]=(bits[rid]+7)/8; else L->feat_len[rid]=(bits[rid]+7)/8;
                nu=0; hasmin=hasmax=0; continue; }
            for(int k=0;k<G.rcount&&!(uv&1);k++){
                u32 u=0; if(nu)u=usage[k<nu?k:nu-1]; else if(hasmin){ u=umin+(u32)k; if(hasmax&&u>umax)u=umax; }
                dgfld f; f.off=bits[rid]+k*G.rsize; f.size=G.rsize; f.sgn=G.lmin<0; f.ok=1;
                if(feat){ if(u==0x000D0052u){ L->mode=f; L->mode_rid=rid; } else if(u==0x000D0053u){ L->didx=f; L->didx_rid=rid; } else if(u==0x000D0055u){ L->ccmax=f; L->ccmax_rid=rid; } continue; }
                if(app==0x000D0001u||app==0x000D0002u){ L->pen=1; L->pen_rid=rid;
                    switch(u){ case 0x000D0042u: L->tip=f; break; case 0x000D0044u: L->bar1=f; break; case 0x000D005Au: L->bar2=f; break; case 0x000D0045u: L->eras=f; break;
                        case 0x000D003Cu: L->inv=f; break; case 0x000D0032u: L->rng=f; break; case 0x000D0030u: L->pres=f; L->pmin=G.lmin; L->pmax=G.lmax; break;
                        case 0x000D003Du: L->tx=f; L->tmin=G.lmin; L->tmax=G.lmax; break; case 0x000D003Eu: L->ty=f; break;
                        case 0x00010030u: L->x=f; L->xmin=G.lmin; L->xmax=G.lmax; break; case 0x00010031u: L->y=f; L->ymin=G.lmin; L->ymax=G.lmax; break; } }
                else if(app==0x000D0004u){ L->touch=1; L->touch_rid=rid;
                    if(finger>=0){ switch(u){ case 0x000D0042u: L->f[finger].tip=f; break; case 0x000D0047u: L->f[finger].conf=f; break; case 0x000D0051u: L->f[finger].cid=f; break;
                        case 0x000D0048u: L->f[finger].w=f; break; case 0x000D0049u: L->f[finger].h=f; break;
                        case 0x00010030u: L->f[finger].x=f; if(!finger){ L->fxmin=G.lmin; L->fxmax=G.lmax; } break;
                        case 0x00010031u: L->f[finger].y=f; if(!finger){ L->fymin=G.lmin; L->fymax=G.lmax; } break; } }
                    else if(u==0x000D0054u)L->cc=f; } }
            bits[rid]+=G.rsize*G.rcount; if(bits[rid]>(1<<20))bits[rid]=1<<20;
            if(!feat)L->in_len[rid]=(bits[rid]+7)/8; else L->feat_len[rid]=(bits[rid]+7)/8;
            nu=0; hasmin=hasmax=0; continue; }
        if(tag==9){ nu=0; hasmin=hasmax=0; } }
    if(L->pen&&(!L->x.ok||!L->y.ok||L->xmax<=L->xmin||L->ymax<=L->ymin||(!L->tip.ok&&!L->rng.ok)))L->pen=0;
    if(L->touch){ int good=0; for(int k=0;k<L->nf;k++) if(L->f[k].tip.ok&&L->f[k].x.ok&&L->f[k].y.ok)good++; if(good!=L->nf||!good||L->fxmax<=L->fxmin||L->fymax<=L->fymin)L->touch=0; }
    if(L->pen&&L->pmax<=L->pmin)L->pres.ok=0;
    int m=0; for(int r=0;r<256;r++) if(L->in_len[r]>m)m=L->in_len[r]; L->in_max=m+(L->uses_ids?1:0);
    return L->pen+L->touch*2; }

int dg_mode_report(const dglay* L,u8* buf,int max,int* rid){
    if(!L->touch||!L->mode.ok)return 0; int id=L->mode_rid, h=L->uses_ids?1:0, len=L->feat_len[id]+h; if(len<1+h||len>max)return 0;
    for(int i=0;i<len;i++)buf[i]=0; if(h)buf[0]=(u8)id;
    dg_setbits(buf+h,len-h,L->mode,2); if(L->didx.ok&&L->didx_rid==id)dg_setbits(buf+h,len-h,L->didx,0);
    *rid=id; return len; }

static const dgwacom dg_wtab[]={
    {"CTL-480 Intuos Pen S",0x030e,DGP_BAMBOO,1023,152,102}, {"CTH-480 Intuos Pen & Touch S",0x0302,DGP_BAMBOO,1023,152,102},
    {"CTL-680 Intuos Pen M",0x0323,DGP_BAMBOO,1023,229,127}, {"CTH-680 Intuos Pen & Touch M",0x0303,DGP_BAMBOO,1023,229,127},
    {"CTL-490 Intuos Draw S",0x033b,DGP_BAMBOO,2047,152,102}, {"CTH-490 Intuos Art S",0x033c,DGP_BAMBOO,2047,152,102},
    {"CTL-690 Intuos M",0x033d,DGP_BAMBOO,2047,229,127}, {"CTH-690 Intuos Art M",0x033e,DGP_BAMBOO,2047,229,127},
    {"CTL-4100 Intuos S",0x0374,DGP_HID,4095,152,102}, {"CTL-6100 Intuos M",0x0375,DGP_HID,4095,229,127},
    {"CTL-4100WL Intuos S Bluetooth",0x0376,DGP_HID,4095,152,102}, {"CTL-4100WL Intuos S Bluetooth",0x03c5,DGP_HID,4095,152,102},
    {"CTL-6100WL Intuos M Bluetooth",0x0378,DGP_HID,4095,229,127}, {"CTL-6100WL Intuos M Bluetooth",0x03c7,DGP_HID,4095,229,127},
    {"CTL-472 One by Wacom S",0x037a,DGP_HID,2047,152,102}, {"CTL-672 One by Wacom M",0x037b,DGP_HID,2047,229,127} };
const dgwacom* dg_wacom(u16 vid,u16 pid){ if(vid!=0x056A)return 0; for(u32 i=0;i<sizeof(dg_wtab)/sizeof(dg_wtab[0]);i++) if(dg_wtab[i].pid==pid)return &dg_wtab[i]; return 0; }
int dg_wacom_mode_report(const dgwacom* w,u8* buf,int max,int* rid){ if(!w||w->proto!=DGP_BAMBOO||max<2)return 0; buf[0]=2; buf[1]=2; *rid=2; return 2; }
int dg_wacom_bamboo(const u8* b,int n,int pmax,dgpen* o){
    if(n<8||b[0]!=2)return 0; int in=(b[1]&0x20)!=0;
    o->in=in; o->xr=0; o->yr=0; o->tx=o->ty=0;
    if(!in){ o->tip=o->b1=o->b2=0; o->pres=0; return 1; }
    o->eraser=(b[1]&0x08)!=0; o->tip=b[1]&1; o->b1=(b[1]>>1)&1; o->b2=(b[1]>>2)&1;
    o->x=b[2]|(b[3]<<8); o->y=b[4]|(b[5]<<8); int p=b[6]|(b[7]<<8); o->pres=p>pmax?pmax:p; return 1; }

int dg_curve(int p,int pmax,int preset){
    static const short cp[5][4]={{0,75,25,100},{0,50,50,100},{0,0,100,100},{50,0,100,50},{75,0,100,25}};
    if(pmax<=0)return 0; if(p<=0)return 0; if(p>=pmax)return 65535; if(preset<0||preset>4)preset=2;
    int x1=cp[preset][0]*655, y1=cp[preset][1]*655, x2=cp[preset][2]*655, y2=cp[preset][3]*655;
    long long X=(long long)p*65535/pmax; int lo=0, hi=1024;
    for(int it=0;it<14&&lo<hi;it++){ int t=(lo+hi)/2; long long T=t, U=1024-t;
        long long bx=(3*U*U*T*x1+3*U*T*T*x2+T*T*T*65535)/(1024LL*1024*1024); if(bx<X)lo=t+1; else hi=t; }
    long long T=lo, U=1024-lo; long long by=(3*U*U*T*y1+3*U*T*T*y2+T*T*T*65535)/(1024LL*1024*1024);
    return by<0?0:by>65535?65535:(int)by; }

void dg_map_xy(int x,int xmin,int xmax,int y,int ymin,int ymax,int* ox,int* oy){
    long long fx=xmax>xmin?(long long)(x-xmin)*65535/(xmax-xmin):0, fy=ymax>ymin?(long long)(y-ymin)*65535/(ymax-ymin):0;
    if(fx<0)fx=0; if(fx>65535)fx=65535; if(fy<0)fy=0; if(fy>65535)fy=65535;
    if(g_dg_lefty){ fx=65535-fx; fy=65535-fy; }
    if(g_dg_map==DGM_ASPECT&&g_dg_W>0&&g_dg_H>0){ long long tw=xmax-xmin, th=ymax-ymin, sw=g_dg_W, sh=g_dg_H;
        if(tw*sh>th*sw){ long long use=th*sw*65535/(sh*tw); if(use>0){ fx=fx*65535/use; if(fx>65535)fx=65535; } }
        else if(tw*sh<th*sw){ long long use=tw*sh*65535/(sw*th); if(use>0){ fy=fy*65535/use; if(fy>65535)fy=65535; } } }
    else if(g_dg_map==DGM_LEFT)fx=fx/2; else if(g_dg_map==DGM_RIGHT)fx=32768+fx/2;
    *ox=(int)fx; *oy=(int)fy; }

int dg_frame(dgdev* D,const u8* b,int n){
    dglay* L=&D->L; const u8* p=b; int pl=n;
    if(L->uses_ids){ if(n<1||b[0]!=L->touch_rid)return 0; p++; pl--; }
    int cc=L->cc.ok?dg_bits(p,pl,L->cc):-1; if(cc>255)cc=255;
    if(cc!=0||D->ncur>=D->expect){ D->ncur=0; D->expect=cc<0?L->nf:cc; }
    int take=cc<0?L->nf:D->expect-D->ncur; if(take>L->nf)take=L->nf;
    for(int i=0;i<take;i++){ int tip=dg_bits(p,pl,L->f[i].tip)!=0; if(L->f[i].conf.ok&&!dg_bits(p,pl,L->f[i].conf))tip=0;
        if(D->ncur<DG_MAXC){ dgcon* c=&D->cur[D->ncur]; c->id=L->f[i].cid.ok?dg_bits(p,pl,L->f[i].cid):i; c->x=dg_bits(p,pl,L->f[i].x); c->y=dg_bits(p,pl,L->f[i].y); c->tip=tip; }
        D->ncur++; }
    if(cc>=0&&D->ncur<D->expect)return 0;
    int m=0, lim=D->ncur<DG_MAXC?D->ncur:DG_MAXC; for(int i=0;i<lim;i++) if(D->cur[i].tip)D->cur[m++]=D->cur[i];
    D->ncur=0; D->expect=0; return m+1; }

static int dg_isqrt(long long v){ if(v<=0)return 0; long long r=0, b=1LL<<40; while(b>v)b>>=2; while(b){ if(v>=r+b){ v-=r+b; r=(r>>1)+b; } else r>>=1; b>>=2; } return (int)r; }
static void dg_abs(const dglay* L,int x,int y){ in_abs(x-L->fxmin,L->fxmax-L->fxmin,y-L->fymin,L->fymax-L->fymin); }
void dg_gesture(dgges* G,const dgcon* c,int n,u32 ms,const dglay* L){
    int rx=L->fxmax-L->fxmin, ry=L->fymax-L->fymin, thr=(rx>ry?rx:ry)/50; if(thr<1)thr=1;
    if(n==0){ if(G->act==1){ if(G->drag||G->held)in_btn_raw(0,0,0); else if(!G->moved&&!G->lp&&g_dg_tap&&ms-G->t0<400){ in_btn_raw(1,0,0); in_btn_raw(0,0,0); } }
        G->act=0; G->drag=G->held=G->lp=G->moved=0; G->n=0; return; }
    if(n==1&&G->act!=2){
        if(!G->act){ G->act=1; G->x0=c[0].x; G->y0=c[0].y; G->t0=ms; G->moved=G->drag=G->held=G->lp=0; dg_abs(L,c[0].x,c[0].y); G->n=1; return; }
        int ddx=c[0].x-G->x0, ddy=c[0].y-G->y0; if(ddx<0)ddx=-ddx; if(ddy<0)ddy=-ddy;
        if(ddx>thr||ddy>thr)G->moved=1;
        if(G->moved&&!G->drag&&!G->lp){ dg_abs(L,G->x0,G->y0); in_btn_raw(1,0,0); G->drag=1; }
        if(!G->moved&&!G->lp&&ms-G->t0>=700){ in_btn_raw(0,1,0); in_btn_raw(0,0,0); G->lp=1; }
        dg_abs(L,c[0].x,c[0].y); G->n=1; return; }
    if(n<2)return;
    int cx=(c[0].x+c[1].x)/2, cy=(c[0].y+c[1].y)/2; long long ddx=c[0].x-c[1].x, ddy=c[0].y-c[1].y; int d=dg_isqrt(ddx*ddx+ddy*ddy);
    if(G->act!=2){ if(G->drag)in_btn_raw(0,0,0); G->act=2; G->drag=G->held=0; G->cx=cx; G->cy=cy; G->d0=d; G->sv=G->sh=0; G->n=n; return; }
    int notch=ry/24; if(notch<1)notch=1; int nh=rx/24; if(nh<1)nh=1;
    G->sv+=cy-G->cy; G->sh+=cx-G->cx; G->cx=cx; G->cy=cy;
    int v=0, h=0; while(G->sv>=notch){ v++; G->sv-=notch; } while(G->sv<=-notch){ v--; G->sv+=notch; } while(G->sh>=nh){ h--; G->sh-=nh; } while(G->sh<=-nh){ h++; G->sh+=nh; }
    if(G->d0>thr){ if((long long)d*100>(long long)G->d0*125){ in_gesture(GES_ZOOM_IN); G->d0=d; G->sv=G->sh=0; v=h=0; } else if((long long)d*100<(long long)G->d0*80){ in_gesture(GES_ZOOM_OUT); G->d0=d; G->sv=G->sh=0; v=h=0; } } else G->d0=d;
    if(v||h)in_scroll(v,h); G->n=n; }

static int dg_put(dgev* o,int max,int* k,int t,int c,int v){ if(*k>=max)return 0; o[*k].type=(u16)t; o[*k].code=(u16)c; o[*k].value=v; (*k)++; return 1; }
int dg_ev_pen(const dgpen* w,const dgpen* p,dgev* o,int max){ int k=0;
    if(p->in&&!w->in)dg_put(o,max,&k,1,p->eraser?0x141:0x140,1);
    if(p->in){ if(p->x!=w->x||!w->in)dg_put(o,max,&k,3,0x00,p->x); if(p->y!=w->y||!w->in)dg_put(o,max,&k,3,0x01,p->y); if(p->pres!=w->pres)dg_put(o,max,&k,3,0x18,p->pres);
        if(p->tx!=w->tx)dg_put(o,max,&k,3,0x1a,p->tx); if(p->ty!=w->ty)dg_put(o,max,&k,3,0x1b,p->ty); }
    int tip=p->in&&p->tip, wt=w->in&&w->tip; if(tip!=wt)dg_put(o,max,&k,1,0x14a,tip);
    int b1=p->in&&p->b1, b2=p->in&&p->b2; if(b1!=(w->in&&w->b1))dg_put(o,max,&k,1,0x14b,b1); if(b2!=(w->in&&w->b2))dg_put(o,max,&k,1,0x14c,b2);
    if(!p->in&&w->in){ if(w->pres)dg_put(o,max,&k,3,0x18,0); dg_put(o,max,&k,1,w->eraser?0x141:0x140,0); }
    if(k)dg_put(o,max,&k,0,0,0); return k; }
int dg_ev_touch(const dgcon* w,int nw,const dgcon* p,int nn,int* slots,dgev* o,int max){ int k=0, any=0;
    for(int s=0;s<DG_MAXC;s++){ if(slots[s]<0)continue; int still=0; for(int i=0;i<nn;i++) if(p[i].id==slots[s])still=1;
        if(!still){ dg_put(o,max,&k,3,0x2f,s); dg_put(o,max,&k,3,0x39,-1); slots[s]=-1; any=1; } }
    for(int i=0;i<nn;i++){ int s=-1, isnew=0; for(int q=0;q<DG_MAXC;q++) if(slots[q]==p[i].id)s=q;
        if(s<0){ for(int q=0;q<DG_MAXC&&s<0;q++) if(slots[q]<0)s=q; if(s<0)continue; slots[s]=p[i].id; isnew=1; }
        int ox=-1, oy=-1; for(int j=0;j<nw;j++) if(w[j].id==p[i].id){ ox=w[j].x; oy=w[j].y; }
        if(!isnew&&ox==p[i].x&&oy==p[i].y)continue;
        dg_put(o,max,&k,3,0x2f,s); if(isnew)dg_put(o,max,&k,3,0x39,p[i].id&0xFFFF); if(isnew||ox!=p[i].x)dg_put(o,max,&k,3,0x35,p[i].x); if(isnew||oy!=p[i].y)dg_put(o,max,&k,3,0x36,p[i].y); any=1; }
    if(nn>0&&nw==0)dg_put(o,max,&k,1,0x14a,1); if(nn==0&&nw>0)dg_put(o,max,&k,1,0x14a,0);
    if(nn&&any){ dg_put(o,max,&k,3,0x00,p[0].x); dg_put(o,max,&k,3,0x01,p[0].y); }
    if(any||k)dg_put(o,max,&k,0,0,0); return k; }
static void dg_ev_push(const dgev* e,int n){ for(int i=0;i<n;i++){ if(dg_qw-dg_qr>=DG_EVQ)dg_qr++; dg_q[dg_qw%DG_EVQ]=e[i]; dg_qw++; } }
int dg_ev_take(dgev* o,int max){ int k=0; while(k<max&&dg_qr!=dg_qw){ o[k++]=dg_q[dg_qr%DG_EVQ]; dg_qr++; } return k; }

int dg_probe(int k,u16 vid,u16 pid,const u8* rd,int n,u8* feat,int* flen,int* frid){
    *flen=0; *frid=0; if(k<0||k>=6)return 0; dgdev* D=&dg_pend[k]; for(u32 i=0;i<sizeof(*D);i++)((u8*)D)[i]=0;
    const dgwacom* w=dg_wacom(vid,pid); int cand=w!=0;
    for(int i=0;i+1<n&&!cand;i++) if((rd[i]==0x05&&rd[i+1]==0x0D)||(rd[i]==0x06&&i+2<n&&rd[i+1]==0x0D&&rd[i+2]==0xFF))cand=1;
    if(!cand||!g_dg_on||!fg_begin(&dg_fg))return 0;
    int got=dg_parse(rd,n,&D->L); fg_ok(&dg_fg);
    if(got){ D->used=1; D->wacom=w?1:0; D->proto=DGP_HID; D->pmax=D->L.pmax; *flen=dg_mode_report(&D->L,feat,64,frid);
        klog(got&2?"usb: touchscreen found (HID digitizer, multitouch)":"usb: pen tablet found (HID digitizer, pressure)"); return D->L.in_max>0?D->L.in_max:8; }
    if(w&&w->proto==DGP_BAMBOO){ D->used=1; D->wacom=1; D->proto=DGP_BAMBOO; D->pmax=w->pmax; D->L.pen=1; D->L.xmin=0; D->L.ymin=0; D->L.xmax=w->wmm*100; D->L.ymax=w->hmm*100;
        *flen=dg_wacom_mode_report(w,feat,64,frid); klog("usb: Wacom tablet found (Wacom pen protocol)"); return 9; }
    return 0; }
void dg_bind(int k,int e){ if(e<0||e>=64)return; dg_of[e]=0;
    if(k<0||k>=6||!dg_pend[k].used)return;
    for(int s=0;s<DG_DEV;s++) if(!dg_dev[s].used){ dg_dev[s]=dg_pend[k]; for(int q=0;q<DG_MAXC;q++)dg_dev[s].slots[q]=-1; dg_of[e]=s+1; dg_pend[k].used=0; return; } }
int dg_count(int* pens,int* touches){ int p=0, t=0, a=0; for(int s=0;s<DG_DEV;s++) if(dg_dev[s].used){ a++; if(dg_dev[s].L.pen)p++; if(dg_dev[s].L.touch)t++; } if(pens)*pens=p; if(touches)*touches=t; return a; }

static void dg_pen_out(dgdev* D,dgpen* np){
    dgev ev[24]; int ne=dg_ev_pen(&D->pen,np,ev,24); dg_ev_push(ev,ne);
    if(np->in){ int ox, oy; dg_map_xy(np->x,D->L.xmin,D->L.xmax,np->y,D->L.ymin,D->L.ymax,&ox,&oy); in_abs(ox,65535,oy,65535);
        in_btn_raw(np->tip,np->b1,np->b2); }
    else if(D->pen.in)in_btn_raw(0,0,0);
    D->pen=*np; g_dg_pen=*np; if(np->in)g_dg_pen.pres=dg_curve(np->pres,D->pmax,g_dg_curve); }
int dg_report(int e,const u8* b,int n,u32 ms){
    if(e<0||e>=64||!dg_of[e]||n<1)return 0; dgdev* D=&dg_dev[dg_of[e]-1]; dglay* L=&D->L; g_dg_reports++;
    if(D->proto==DGP_BAMBOO){ dgpen np=D->pen; if(!dg_wacom_bamboo(b,n,D->pmax,&np))return 1; np.xr=L->xmax; np.yr=L->ymax; dg_pen_out(D,&np); return 1; }
    int id=L->uses_ids?b[0]:0;
    if(L->pen&&id==L->pen_rid){ const u8* p=b+(L->uses_ids?1:0); int pl=n-(L->uses_ids?1:0); dgpen np;
        np.in=L->rng.ok?dg_bits(p,pl,L->rng)!=0:1; np.x=dg_bits(p,pl,L->x); np.y=dg_bits(p,pl,L->y); np.xr=L->xmax; np.yr=L->ymax;
        np.tip=dg_bits(p,pl,L->tip)!=0; np.b1=dg_bits(p,pl,L->bar1)!=0; np.b2=dg_bits(p,pl,L->bar2)!=0;
        np.eraser=dg_bits(p,pl,L->inv)!=0||dg_bits(p,pl,L->eras)!=0; if(dg_bits(p,pl,L->eras))np.tip=1;
        np.pres=L->pres.ok?dg_bits(p,pl,L->pres)-L->pmin:(np.tip?1:0); if(np.pres<0)np.pres=0; np.tx=dg_bits(p,pl,L->tx); np.ty=dg_bits(p,pl,L->ty);
        if(!L->pres.ok)D->pmax=1;
        if(np.x<L->xmin||np.x>L->xmax||np.y<L->ymin||np.y>L->ymax)np.in=0;
        dg_pen_out(D,&np); return 1; }
    if(L->touch&&id==L->touch_rid){ int r=dg_frame(D,b,n); if(!r)return 1; int m=r-1; g_dg_frames++;
        dgev ev[96]; int ne=dg_ev_touch(D->prev,D->nprev,D->cur,m,D->slots,ev,96); dg_ev_push(ev,ne);
        if(g_dg_touch)dg_gesture(&D->g,D->cur,m,ms,L);
        for(int i=0;i<m;i++)D->prev[i]=D->cur[i]; D->nprev=m; return 1; }
    return 0; }
int dg_xfer(int e,const u8* b,int n){ g_dg_W=(int)W; g_dg_H=(int)H; return dg_report(e,b,n,(u32)(g_ticks*10ULL)); }
