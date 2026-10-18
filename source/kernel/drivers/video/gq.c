#include "include/nvx/drivers/video/gq.h"

int g_gq_serial=0; char g_gq_diag[160]; int g_gq_mode=GQM_OFF; gsurf g_gs[GQ_MAXS]; gqhw g_gqhw; int g_gq_wflush=0; volatile int g_gq_inb=0; int g_gq_tlb=0; static gq_prefn pre=0; static int in_pre=0;
void gq_set_pre(gq_prefn f){ pre=f; }
static char* wtxt(char* d,const char* s); static char* wdec(char* p,int v);
u32 g_sgen[GQ_MAXS]; u64 g_ophash=1469598103934665603ULL; int g_hash_d=0; u64 g_gq_wait_ns=0; u32 g_gq_submits=0; static u32 cur_ck=0;
void gq_set_key(u32 k){ cur_ck=k; }
static void gq_bump_r(int s);
void gq_bump(int s){ if(s>0&&s<GQ_MAXS){ g_sgen[s]++; gq_bump_r(s); } }
u32 g_gq_ops=0, g_gq_stalls=0, g_gq_syncs=0, g_gq_batches=0, g_gq_fail=0, g_gq_last_us=0, g_gq_frame_ops=0, g_gq_frame_stalls=0, g_gq_frame_syncs=0;
static u32 f_ops0=0, f_stalls0=0, f_syncs0=0;
typedef struct { u8 k, d, s, m; int x, y, w, h, sdx, sdy, mdx, mdy; u32 c0, c1, a, inv, key, goff, ck, sg, mg; } gop;
typedef struct { int x0, y0, x1, y1; } grc;
#define GQ_NR 8
static u32 rp=0, nops=0; static int inbatch=0; static u64 bpix=0;
#define GQ_MAXPIX (12ULL<<20)
static struct { grc w[GQ_NR]; grc r[GQ_NR]; int nw, nr; grc dirty; int hasd; grc touch; int hast; } dep[GQ_MAXS];
void gq_touch_reset(int s){ if(s>0&&s<GQ_MAXS)dep[s].hast=0; }
int gq_touch_get(int s,int* x0,int* y0,int* x1,int* y1){ if(s<=0||s>=GQ_MAXS||!dep[s].hast)return 0; *x0=dep[s].touch.x0; *y0=dep[s].touch.y0; *x1=dep[s].touch.x1; *y1=dep[s].touch.y1; return 1; }
static int sr_on, sr_n[2];
void gq_abort(void){ inbatch=0; nops=0; g_gq_inb=0; sr_on=0; sr_n[0]=sr_n[1]=-1; }

u32 gq_bl(u32 d,u32 s,u32 a){ u32 ia=256-a; u32 rb=(((s&0x00FF00FFu)*a+(d&0x00FF00FFu)*ia)>>8)&0x00FF00FFu; u32 ag=((((s>>8)&0x00FF00FFu)*a+((d>>8)&0x00FF00FFu)*ia))&0xFF00FF00u; return rb|ag; }
static u32 gq_over(u32 d,u32 s){ u32 t=s>>24; t+=t>>7; u32 rb=(((d&0x00FF00FFu)*t)>>8)&0x00FF00FFu; u32 ag=(((d>>8)&0x00FF00FFu)*t)&0xFF00FF00u; return s+rb+ag; }
static void ss_write(int i){ if(!g_gqhw.st)return; gsurf* g=&g_gs[i]; u32* s=g_gqhw.st+i*16; for(int k=0;k<16;k++)s[k]=0;
    u32 w=g->w?g->w:1, h=g->h?g->h:1, p=g->pitch?g->pitch:1;
    s[0]=(1u<<29)|(0x0C0u<<18)|(1u<<16)|(1u<<14)|(1u<<8); s[2]=((h-1)<<16)|(w-1); s[3]=p*4-1;
    s[7]=(4u<<25)|(5u<<22)|(6u<<19)|(7u<<16); s[8]=(u32)g->gfx; s[9]=(u32)(g->gfx>>32);
    if(g_gqhw.flush)g_gqhw.flush(s,64); }
int gq_surf(u32* px,u64 gfx,u32 w,u32 h,u32 pitch){ for(int i=1;i<GQ_MAXS;i++) if(!g_gs[i].used){ g_gs[i].px=px; g_gs[i].gfx=gfx; g_gs[i].w=w; g_gs[i].h=h; g_gs[i].pitch=pitch; g_gs[i].used=1; dep[i].nw=dep[i].nr=0; dep[i].hasd=0; ss_write(i); return i; } return -1; }
void gq_surf_update(int i,u32 w,u32 h){ if(i<=0||i>=GQ_MAXS)return; gq_sync(); g_gs[i].w=w; g_gs[i].h=h; ss_write(i); }
static int clampi(int v,int lo,int hi){ return v<lo?lo:v>hi?hi:v; }
static u32 rdpx(int s,int x,int y){ gsurf* g=&g_gs[s]; x=clampi(x,0,(int)g->w-1); y=clampi(y,0,(int)g->h-1); return g->px[(u64)y*g->pitch+(u32)x]; }
static void cpu_exec(const gop* o){
    gsurf* D=&g_gs[o->d];
    for(int y=o->y;y<o->y+o->h;y++){ u32* row=D->px+(u64)y*D->pitch;
        for(int x=o->x;x<o->x+o->w;x++){ u32 d=row[x], n=d;
            switch(o->k){
            case GK_FILL: n=o->c0; break;
            case GK_BLEND: n=gq_bl(d,o->c0,o->a); break;
            case GK_MASK: { u32 m=rdpx(o->m,x+o->mdx,y+o->mdy); n=gq_bl(d,o->c0,(m*o->a)>>8); } break;
            case GK_COPY: case GK_COPYM: { u32 s=rdpx(o->s,x+o->sdx,y+o->sdy); if(o->key&&(s>>24))break; u32 a=o->a; if(o->k==GK_COPYM){ u32 m=rdpx(o->m,x+o->mdx,y+o->mdy); a=(m*o->a)>>8; } n=gq_bl(d,s,a); } break;
            case GK_GRADH: case GK_GRADV: { u32 dv=(u32)((o->k==GK_GRADH?(x-o->x):(y-o->y))+(int)o->goff); u32 t=(dv*o->inv)>>8; n=gq_bl(o->c0,o->c1,t); } break;
            case GK_TINT: n=(d&0xFFFF0000u)|(((((d>>8)&255)*15)>>4)<<8)|(((d&255)*5)>>3); break;
            case GK_BLURH: case GK_BLURV: { u32 rb=0,g=0; for(int k=-4;k<4;k++){ u32 s=o->k==GK_BLURH?rdpx(o->s,x+o->sdx+k,y+o->sdy):rdpx(o->s,x+o->sdx,y+o->sdy+k); rb+=s&0x00FF00FFu; g+=(s>>8)&0x00FF00FFu; } n=((rb>>3)&0x00FF00FFu)|((g<<5)&0xFF00FF00u); } break;
            case GK_OVER: case GK_OVERM: { u32 s=rdpx(o->s,x+o->sdx,y+o->sdy); u32 a=o->a; if(o->k==GK_OVERM){ u32 m=rdpx(o->m,x+o->mdx,y+o->mdy); a=(m*o->a)>>8; } n=gq_bl(d,gq_over(d,s),a); } break; }
            row[x]=n; } } }
static void w32(u32 v){ g_gqhw.ring[rp++]=v; }
static void pc(u32 flags){ w32(0x7A000004u); w32(flags); w32(0); w32(0); w32(0); w32(0); }
static void dep_clear(void){ for(int i=0;i<GQ_MAXS;i++){ dep[i].nw=0; dep[i].nr=0; } }
static int ov(grc a,grc b){ return a.x0<b.x1&&b.x0<a.x1&&a.y0<b.y1&&b.y0<a.y1; }
static int hits(grc* l,int n,grc r){ for(int i=0;i<n;i++) if(ov(l[i],r))return 1; return 0; }
static void addr(grc* l,int* n,grc r){ if(*n<GQ_NR){ l[(*n)++]=r; return; } grc u=l[GQ_NR-1]; if(r.x0<u.x0)u.x0=r.x0; if(r.y0<u.y0)u.y0=r.y0; if(r.x1>u.x1)u.x1=r.x1; if(r.y1>u.y1)u.y1=r.y1; l[GQ_NR-1]=u; }
static void rclamp(grc* r,int s){ int w=(int)g_gs[s].w, h=(int)g_gs[s].h; if(w<1)w=1; if(h<1)h=1;
    r->x0=clampi(r->x0,0,w-1); r->y0=clampi(r->y0,0,h-1); r->x1=clampi(r->x1,1,w); r->y1=clampi(r->y1,1,h); if(r->x1<=r->x0)r->x1=r->x0+1; if(r->y1<=r->y0)r->y1=r->y0+1; }
static void batch_begin(void){
    rp=0; nops=0; bpix=0; dep_clear(); u64 S=g_gqhw.st_g, K=g_gqhw.kern_g;
    pc((1u<<20)|(1u<<12)|(1u<<5)); w32(0x69040302u);
    w32(0x61010011u); w32(1); w32(0); w32(0); w32((u32)S|1u); w32((u32)(S>>32)); w32((u32)S|1u); w32((u32)(S>>32)); w32(1); w32(0); w32((u32)K|1u); w32((u32)(K>>32));
    w32(0xFFFFF001u); w32(0xFFFFF001u); w32(0xFFFFF001u); w32(0xFFFFF001u); w32(1); w32(0); w32(0xFFFFF000u);
    pc((1u<<20)|(1u<<11)|(1u<<10)|(1u<<3)|(1u<<2));
    if(g_gq_tlb&&g_gqhw.scratch_g){ w32(0x7A000004u); w32((1u<<24)|(1u<<20)|(1u<<18)|(1u<<14)); w32((u32)g_gqhw.scratch_g); w32((u32)(g_gqhw.scratch_g>>32)); w32(0); w32(0); g_gq_tlb=0; }
    w32(0x70000007u); w32(0); w32(0); w32((63u<<16)|(2u<<8)); w32(0); w32((2u<<16)|2u); w32(0); w32(0); w32(0);
    inbatch=1; g_gq_inb=1; g_gq_batches++; }
static int bsubmit(void){
    if(!inbatch)return 0; inbatch=0; g_gq_inb=0; if(!nops)return 0;
    pc((1u<<20)|(1u<<12)|(1u<<5));
    if(g_gqhw.flush){ g_gqhw.flush(g_gqhw.st+0x1000/4,(u64)nops*32); g_gqhw.flush(g_gqhw.st+0x10000/4,(u64)nops*128); }
    g_gq_syncs++; g_gq_submits++; u64 tw0=g_gqhw.now?g_gqhw.now():0; int r=g_gqhw.submit(rp); if(g_gqhw.now)g_gq_wait_ns+=g_gqhw.now()-tw0;
    if(r){ g_gq_fail++; g_gq_mode=GQM_OFF; return -1; }
    for(int i=1;i<GQ_MAXS;i++)dep[i].hasd=0;
    return 0; }
static void gpu_rec(const gop* o){
    if(inbatch&&(nops>=GQ_MAXOPS||rp+80>g_gqhw.ring_cap-64||bpix>GQ_MAXPIX)) bsubmit();
    if(g_gq_mode!=GQM_GPU){ cpu_exec(o); return; }
    if(!inbatch)batch_begin();
    int bx=o->x&~7, by=o->y&~7, ex=(o->x+o->w+7)&~7, ey=(o->y+o->h+7)&~7;
    grc wr={bx,by,ex,ey}; grc sr={bx+o->sdx-4,by+o->sdy-4,ex+o->sdx+4,ey+o->sdy+4}; grc mr={bx+o->mdx,by+o->mdy,ex+o->mdx,ey+o->mdy}; bpix+=(u64)(ex-bx)*(u64)(ey-by);
    int uses=(o->k==GK_COPY||o->k==GK_COPYM||o->k==GK_BLURH||o->k==GK_BLURV||o->k==GK_OVER||o->k==GK_OVERM), usem=(o->k==GK_MASK||o->k==GK_COPYM||o->k==GK_OVERM);
    rclamp(&sr,o->s); rclamp(&mr,o->m);
    int conflict=hits(dep[o->d].w,dep[o->d].nw,wr)||hits(dep[o->d].r,dep[o->d].nr,wr)||(uses&&hits(dep[o->s].w,dep[o->s].nw,sr))||(usem&&hits(dep[o->m].w,dep[o->m].nw,mr));
    if(conflict||(g_gq_serial&&nops>0)){ pc((1u<<20)|(1u<<5)); dep_clear(); g_gq_stalls++; }
    addr(dep[o->d].w,&dep[o->d].nw,wr); if(uses)addr(dep[o->s].r,&dep[o->s].nr,sr); if(usem)addr(dep[o->m].r,&dep[o->m].nr,mr);
    { grc* dd=&dep[o->d].dirty; if(!dep[o->d].hasd){ *dd=wr; dep[o->d].hasd=1; } else { if(wr.x0<dd->x0)dd->x0=wr.x0; if(wr.y0<dd->y0)dd->y0=wr.y0; if(wr.x1>dd->x1)dd->x1=wr.x1; if(wr.y1>dd->y1)dd->y1=wr.y1; } }
    u32 n=nops++; u32 bt=0x1000u+n*32u, idd=0x10000u+n*128u, cb=idd+64u; u32* st=g_gqhw.st;
    st[bt/4]=(u32)o->d*64u; st[bt/4+1]=(u32)o->s*64u; st[bt/4+2]=(u32)o->m*64u;
    u32* I=st+idd/4; for(int k=0;k<8;k++)I[k]=0; I[0]=g_gqhw.koff[o->k]&~63u; I[2]=1u<<18; I[4]=bt&0xFFE0u; I[5]=2u<<16; I[6]=1;
    u32* c=st+cb/4; c[GC_BX]=(u32)bx; c[GC_BY]=(u32)by; c[GC_NX]=(u32)(-o->x); c[GC_NY]=(u32)(-o->y); c[GC_NW]=(u32)(-o->w); c[GC_NH]=(u32)(-o->h);
    c[GC_C0]=o->c0; c[GC_A]=o->a; c[GC_SDX]=(u32)o->sdx; c[GC_SDY]=(u32)o->sdy; c[GC_C1]=o->c1; c[GC_INV]=o->inv; c[GC_KEY]=o->key?0xFFFFFFFFu:0; c[GC_MDX]=(u32)o->mdx; c[GC_MDY]=(u32)o->mdy; c[GC_GOFF]=o->goff;
    w32(0x70040000u); w32(0);
    w32(0x70010002u); w32(0); w32(64); w32(cb);
    w32(0x70020002u); w32(0); w32(32); w32(idd);
    u32 gx=(u32)((ex-bx)>>3), gy=(u32)((ey-by)>>3);
    w32(0x7105000Du); w32(0); w32(0); w32(0); w32(0); w32(0); w32(0); w32(gx); w32(0); w32(0); w32(gy); w32(0); w32(1); w32(0xFFu); w32(0xFFFFFFFFu);
    w32(0x70040000u); w32(0); }
static int usesrc(const gop* o){ return o->k==GK_COPY||o->k==GK_COPYM||o->k==GK_BLURH||o->k==GK_BLURV||o->k==GK_OVER||o->k==GK_OVERM; }
static int usemask(const gop* o){ return o->k==GK_MASK||o->k==GK_COPYM||o->k==GK_OVERM; }
static int norm(gop* o){
    if(o->d<=0||o->d>=GQ_MAXS||!g_gs[o->d].used)return 0; gsurf* D=&g_gs[o->d];
    int x0=o->x, y0=o->y, x1=o->x+o->w, y1=o->y+o->h; if(x0<0)x0=0; if(y0<0)y0=0; if(x1>(int)D->w)x1=(int)D->w; if(y1>(int)D->h)y1=(int)D->h; if(x1<=x0||y1<=y0)return 0;
    int ox=x0-o->x, oy=y0-o->y; if(o->k==GK_GRADH)o->goff+=(u32)ox; if(o->k==GK_GRADV)o->goff+=(u32)oy;
    o->x=x0; o->y=y0; o->w=x1-x0; o->h=y1-y0; if(o->a>256)o->a=256;
    if(usesrc(o)&&(o->s<=0||o->s>=GQ_MAXS||!g_gs[o->s].used))return 0;
    if(usemask(o)&&(o->m<=0||o->m>=GQ_MAXS||!g_gs[o->m].used))return 0;
    if(o->s<=0)o->s=o->d; if(o->m<=0)o->m=o->d; return 1; }
static void hmix(u64 v){ g_ophash=(g_ophash^v)*0x100000001B3ULL; }
#define RGN 8
typedef struct { int n; grc r[RGN]; } rgn;
static grc grect(const gop* o){ grc t={o->x,o->y,o->x+o->w,o->y+o->h}; return t; }
static int rin(grc a,grc b){ return a.x0>=b.x0&&a.y0>=b.y0&&a.x1<=b.x1&&a.y1<=b.y1; }
static grc runi(grc a,grc b){ if(b.x0<a.x0)a.x0=b.x0; if(b.y0<a.y0)a.y0=b.y0; if(b.x1>a.x1)a.x1=b.x1; if(b.y1>a.y1)a.y1=b.y1; return a; }
static long rarea(grc a){ return (long)(a.x1-a.x0)*(long)(a.y1-a.y0); }
static void rg_add(rgn* g,grc r){ if(r.x1<=r.x0||r.y1<=r.y0)return;
    for(int i=0;i<g->n;i++){ if(rin(r,g->r[i]))return; if(rin(g->r[i],r)){ g->r[i]=g->r[--g->n]; i--; } }
    if(g->n<RGN){ g->r[g->n++]=r; return; }
    int bi=0; long bc=-1; for(int i=0;i<g->n;i++){ long c=rarea(runi(g->r[i],r))-rarea(g->r[i]); if(bc<0||c<bc){ bc=c; bi=i; } } g->r[bi]=runi(g->r[bi],r); }
static int rg_hit(const rgn* g,grc r){ for(int i=0;i<g->n;i++) if(ov(g->r[i],r))return 1; return 0; }
static void rg_full(rgn* g,int s){ grc f={0,0,(int)g_gs[s].w,(int)g_gs[s].h}; g->n=0; rg_add(g,f); }
int g_scene_quiet=0; u32 g_scene_wr=0;
static int sr_on=0, sr_exec=0, sr_d=0, sr_cur=0, sr_max=0, sr_n[2]={-1,-1}, sr_ab=0; static gop* sr_buf[2]; static u8 sr_def[GQ_MAXS], sr_rd[GQ_MAXS];
static rgn sr_chg[GQ_MAXS], sr_ext[GQ_MAXS];
int g_sr_why=0, g_sr_full=0, g_sr_part=0, g_sr_none=0, g_sr_ops=0, g_sr_run=0; static int lg_d=0, lg_n=0;
#define GQ_LOGMAX 4096
typedef struct { u8 k, s, m, d; int x, y, w, h; int i; } glog; static glog lgb[GQ_LOGMAX]; static int lg_i=0;
static void sr_abort(void);
static void op_hash(const gop* o){
    if(g_hash_d&&o->d==g_hash_d){ hmix((u64)o->k|((u64)o->a<<8)|((u64)o->key<<20)); hmix(((u64)(u32)o->x<<32)|(u32)o->y); hmix(((u64)(u32)o->w<<32)|(u32)o->h); hmix(((u64)o->c0<<32)|o->c1); hmix(((u64)o->inv<<32)|o->goff);
        if(o->ck)hmix(o->ck); else { hmix(((u64)o->s<<8)|o->m); hmix(((u64)(u32)o->sdx<<32)|(u32)o->sdy); hmix(((u64)(u32)o->mdx<<32)|(u32)o->mdy); if(usesrc(o)&&o->s!=o->d)hmix(g_sgen[o->s]); if(usemask(o)&&o->m!=o->d)hmix(g_sgen[o->m]); } } }
static void run_op(gop* o){
    if(sr_exec){ if(o->d!=sr_d)rg_add(&sr_ext[o->d],grect(o)); }
    else { grc t=grect(o); if(sr_on){ if(sr_def[o->d]||sr_rd[o->d]){ g_sr_why=5; sr_abort(); } else rg_add(&sr_chg[o->d],t); } else if(!g_scene_quiet) rg_add(&sr_ext[o->d],t); }
    g_gq_ops++; g_sgen[o->d]++;
    { grc t={o->x,o->y,o->x+o->w,o->y+o->h}; if(!dep[o->d].hast){ dep[o->d].touch=t; dep[o->d].hast=1; } else { grc* u=&dep[o->d].touch; if(t.x0<u->x0)u->x0=t.x0; if(t.y0<u->y0)u->y0=t.y0; if(t.x1>u->x1)u->x1=t.x1; if(t.y1>u->y1)u->y1=t.y1; } }
    if(lg_d&&o->d==lg_d){ if(lg_n<GQ_LOGMAX){ glog* L=&lgb[lg_n++]; L->k=o->k; L->s=o->s; L->m=o->m; L->d=o->d; L->x=o->x; L->y=o->y; L->w=o->w; L->h=o->h; L->i=lg_i; } lg_i++; }
    if(g_gq_mode==GQM_GPU&&g_gqhw.submit) gpu_rec(o); else cpu_exec(o); }
static void exec_op(gop* o){ op_hash(o); run_op(o); }
static gop Z(int k,int d,int x,int y,int w,int h);
static int cap_on=0, cap_s=0, cap_n=0, cap_max=0, cap_ab=0; static gop* cap_buf=0; static grc cap_clr;
int g_cap_why=0, g_cap_wk=0; volatile int g_gq_capon=0;
static void cap_abort(void){ if(!cap_on)return; cap_on=0; g_gq_capon=0; cap_ab=1;
    int ip=in_pre; in_pre=1; grc tt=dep[cap_s].touch; int th_=dep[cap_s].hast;
    gop c=Z(GK_FILL,cap_s,cap_clr.x0,cap_clr.y0,cap_clr.x1-cap_clr.x0,cap_clr.y1-cap_clr.y0); c.c0=GQ_EMPTY; if(norm(&c))exec_op(&c);
    dep[cap_s].touch=tt; dep[cap_s].hast=th_;
    for(int i=0;i<cap_n;i++)exec_op(&cap_buf[i]); cap_n=0; in_pre=ip; }
void gq_cap_begin(int s,void* buf,int max,int cx,int cy,int cw,int ch){ cap_on=1; g_gq_capon=1; cap_ab=0; cap_s=s; cap_n=0; cap_max=max; cap_buf=(gop*)buf; cap_clr.x0=cx; cap_clr.y0=cy; cap_clr.x1=cx+cw; cap_clr.y1=cy+ch; }
int gq_cap_end(void){ g_gq_capon=0; if(cap_ab||!cap_on){ cap_on=0; cap_ab=0; return -1; } cap_on=0; return cap_n; }
int gq_op_size(void){ return (int)sizeof(gop); }
int gq_op_same(const void* pa,const void* pb){ const gop* a=(const gop*)pa; const gop* b=(const gop*)pb;
    if(a->k!=b->k||a->d!=b->d||a->x!=b->x||a->y!=b->y||a->w!=b->w||a->h!=b->h||a->c0!=b->c0||a->c1!=b->c1||a->a!=b->a||a->inv!=b->inv||a->key!=b->key||a->goff!=b->goff||a->ck!=b->ck)return 0;
    if(a->ck)return 1; return a->s==b->s&&a->m==b->m&&a->sdx==b->sdx&&a->sdy==b->sdy&&a->mdx==b->mdx&&a->mdy==b->mdy&&a->sg==b->sg&&a->mg==b->mg; }
void gq_op_rect(const void* p,int* x,int* y,int* w,int* h){ const gop* o=(const gop*)p; *x=o->x; *y=o->y; *w=o->w; *h=o->h; }
void gq_replay(const void* buf,int n,int cx,int cy,int cw,int ch){ const gop* L=(const gop*)buf;
    for(int i=0;i<n;i++){ gop o=L[i]; int x0=o.x>cx?o.x:cx, y0=o.y>cy?o.y:cy, x1=o.x+o.w<cx+cw?o.x+o.w:cx+cw, y1=o.y+o.h<cy+ch?o.y+o.h:cy+ch; if(x1<=x0||y1<=y0)continue;
        if(o.k==GK_GRADH)o.goff+=(u32)(x0-o.x); if(o.k==GK_GRADV)o.goff+=(u32)(y0-o.y); o.x=x0; o.y=y0; o.w=x1-x0; o.h=y1-y0; if(!g_gs[o.d].used)continue; exec_op(&o); } }
static void rec(gop* o){
    if(!norm(o)){ cur_ck=0; return; } o->ck=cur_ck; cur_ck=0; o->sg=(usesrc(o)&&o->s!=o->d)?g_sgen[o->s]:0; o->mg=(usemask(o)&&o->m!=o->d)?g_sgen[o->m]:0;
    if(pre&&!in_pre){ in_pre=1; pre(o->d,o->x,o->y,o->w,o->h,usesrc(o)?o->s:-1,usemask(o)?o->m:-1); in_pre=0; }
    if(cap_on){ if(o->d==cap_s&&!(usesrc(o)&&o->s==cap_s)&&!(usemask(o)&&o->m==cap_s)&&cap_n<cap_max){ cap_buf[cap_n++]=*o; return; } g_cap_why=o->d!=cap_s?1:cap_n>=cap_max?3:2; g_cap_wk=o->k; cap_abort(); }
    if(sr_on&&(sr_def[o->d]||(usesrc(o)&&sr_def[o->s])||(usemask(o)&&sr_def[o->m]))){
        if(sr_n[sr_cur]<sr_max){ op_hash(o); sr_buf[sr_cur][sr_n[sr_cur]++]=*o; sr_def[o->d]=1; if(usesrc(o)&&!sr_def[o->s])sr_rd[o->s]=1; if(usemask(o)&&!sr_def[o->m])sr_rd[o->m]=1; return; }
        g_sr_why=3; sr_abort(); }
    exec_op(o); }
static gop Z(int k,int d,int x,int y,int w,int h){ gop o; u8* p=(u8*)&o; for(u32 i=0;i<sizeof(o);i++)p[i]=0; o.k=(u8)k; o.d=(u8)d; o.x=x; o.y=y; o.w=w; o.h=h; o.a=256; return o; }
void gq_fill(int d,int x,int y,int w,int h,u32 c){ gop o=Z(GK_FILL,d,x,y,w,h); o.c0=c; rec(&o); }
void gq_blend(int d,int x,int y,int w,int h,u32 c,int a){ if(a<=0)return; gop o=Z(a>=256?GK_FILL:GK_BLEND,d,x,y,w,h); o.c0=a>=256?(c&0x00FFFFFFu):c; o.a=(u32)a; rec(&o); }
void gq_mask(int d,int x,int y,int w,int h,u32 c,int a,int m,int mx,int my){ if(a<=0)return; gop o=Z(GK_MASK,d,x,y,w,h); o.c0=c; o.a=(u32)a; o.m=(u8)m; o.mdx=mx-x; o.mdy=my-y; rec(&o); }
void gq_copy(int d,int x,int y,int w,int h,int s,int sx,int sy,int a,int key){ if(a<=0)return; gop o=Z(GK_COPY,d,x,y,w,h); o.s=(u8)s; o.sdx=sx-x; o.sdy=sy-y; o.a=(u32)a; o.key=(u32)key; rec(&o); }
void gq_copym(int d,int x,int y,int w,int h,int s,int sx,int sy,int m,int mx,int my,int a,int key){ if(a<=0)return; gop o=Z(GK_COPYM,d,x,y,w,h); o.s=(u8)s; o.sdx=sx-x; o.sdy=sy-y; o.m=(u8)m; o.mdx=mx-x; o.mdy=my-y; o.a=(u32)a; o.key=(u32)key; rec(&o); }
void gq_grad(int d,int x,int y,int w,int h,u32 c0,u32 c1,int vert){ gop o=Z(vert?GK_GRADV:GK_GRADH,d,x,y,w,h); int len=vert?h:w; if(len<1)return; u32 inv=(256u<<8)/(u32)len; if(inv>65535u)inv=65535u; o.c0=c0; o.c1=c1; o.inv=inv; rec(&o); }
void gq_overc(int d,int x,int y,int w,int h,int s,int sx,int sy,int a){ if(a<=0)return; gop o=Z(GK_OVER,d,x,y,w,h); o.s=(u8)s; o.sdx=sx-x; o.sdy=sy-y; o.a=(u32)a; rec(&o); }
void gq_overm(int d,int x,int y,int w,int h,int s,int sx,int sy,int m,int mx,int my,int a){ if(a<=0)return; gop o=Z(GK_OVERM,d,x,y,w,h); o.s=(u8)s; o.sdx=sx-x; o.sdy=sy-y; o.m=(u8)m; o.mdx=mx-x; o.mdy=my-y; o.a=(u32)a; rec(&o); }
void gq_tint(int d,int x,int y,int w,int h){ gop o=Z(GK_TINT,d,x,y,w,h); rec(&o); }
void gq_blur1(int d,int x,int y,int w,int h,int s,int sx,int sy,int vert){ gop o=Z(vert?GK_BLURV:GK_BLURH,d,x,y,w,h); o.s=(u8)s; o.sdx=sx-x; o.sdy=sy-y; rec(&o); }
int gq_pending(void){ return inbatch&&nops>0; }
int gq_sync(void){
    if(pre&&!in_pre){ in_pre=1; pre(-1,0,0,0,0,-1,-1); in_pre=0; }
    if(cap_on){ g_cap_why=4; cap_abort(); }
    if(sr_on){ g_sr_why=1; sr_abort(); }
    return bsubmit(); }
static void gq_bump_r(int s){ if(sr_on)rg_full(&sr_chg[s],s); else rg_full(&sr_ext[s],s); }
static void sr_abort(void){ if(!sr_on)return; int n=sr_n[sr_cur]; gop* L=sr_buf[sr_cur]; sr_on=0; sr_ab=1; sr_n[sr_cur]=-1; sr_exec=1; for(int i=0;i<n;i++)run_op(&L[i]); sr_exec=0; }
void gq_srec_invalidate(void){ sr_n[0]=sr_n[1]=-1; }
void gq_srec_begin(int scene,void* a,void* b,int max){
    if(sr_on||!scene){ return; } sr_buf[0]=(gop*)a; sr_buf[1]=(gop*)b; sr_max=max; if(sr_d!=scene){ sr_n[0]=sr_n[1]=-1; sr_d=scene; }
    sr_cur^=1; sr_n[sr_cur]=0; sr_ab=0; g_sr_why=0;
    for(int i=0;i<GQ_MAXS;i++){ sr_def[i]=0; sr_rd[i]=0; sr_chg[i]=sr_ext[i]; sr_ext[i].n=0; } sr_def[scene]=1; sr_on=1; }
static int sr_same(const gop* a,const gop* b){
    if(a->k!=b->k||a->d!=b->d||a->x!=b->x||a->y!=b->y||a->w!=b->w||a->h!=b->h||a->c0!=b->c0||a->c1!=b->c1||a->a!=b->a||a->inv!=b->inv||a->key!=b->key||a->goff!=b->goff||a->ck!=b->ck)return 0;
    if(a->ck)return 1; return a->s==b->s&&a->m==b->m&&a->sdx==b->sdx&&a->sdy==b->sdy&&a->mdx==b->mdx&&a->mdy==b->mdy; }
static int blurm(const gop* o){ return o->k==GK_BLURH?1:o->k==GK_BLURV?2:0; }
static grc rsh(grc r,int dx,int dy,int m){ int mx=(m&1)?4:0, my=(m&2)?4:0; grc t={r.x0+dx-mx,r.y0+dy-my,r.x1+dx+mx,r.y1+dy+my}; return t; }
static grc rcap(grc a,grc b){ grc t={a.x0>b.x0?a.x0:b.x0,a.y0>b.y0?a.y0:b.y0,a.x1<b.x1?a.x1:b.x1,a.y1<b.y1?a.y1:b.y1}; return t; }
static int rok(grc r){ return r.x1>r.x0&&r.y1>r.y0; }
static void spread(rgn* dst,const rgn* src,const gop* o,int in,int dx,int dy,int m){ grc r=grect(o); int sw=(int)g_gs[in].w, sh=(int)g_gs[in].h;
    for(int i=0;i<src->n;i++){ grc e=src->r[i]; if(e.x0<=0)e.x0=-(1<<20); if(e.y0<=0)e.y0=-(1<<20); if(e.x1>=sw)e.x1=1<<20; if(e.y1>=sh)e.y1=1<<20; grc q=rcap(rsh(e,-dx,-dy,m),r); if(rok(q))rg_add(dst,q); } }
static rgn sr_cd[GQ_MAXS], sr_need[GQ_MAXS], sr_w[GQ_MAXS]; static u8 sr_x[8192]; static u32 sr_hl[4096], sr_hp[4096]; static short sr_ml[4096]; static u8 sr_mp[4096], sr_f[4096]; static rgn sr_rw[GQ_MAXS];
static u32 hh(u32 h,u32 v){ h^=v; h*=16777619u; return h; }
static u32 sr_hash(const gop* o){ u32 h=2166136261u; h=hh(h,(u32)o->k|((u32)o->d<<8)); h=hh(h,(u32)o->x); h=hh(h,(u32)o->y); h=hh(h,(u32)o->w); h=hh(h,(u32)o->h); h=hh(h,o->c0); h=hh(h,o->c1); h=hh(h,o->a); h=hh(h,o->inv); h=hh(h,o->key); h=hh(h,o->goff); h=hh(h,o->ck);
    if(!o->ck){ h=hh(h,(u32)o->s|((u32)o->m<<8)); h=hh(h,(u32)o->sdx); h=hh(h,(u32)o->sdy); h=hh(h,(u32)o->mdx); h=hh(h,(u32)o->mdy); } return h; }
#define SR_LOOK 96
static void sr_align(gop* L,int n,gop* P,int np){
    for(int k=0;k<n;k++){ sr_hl[k]=sr_hash(&L[k]); sr_ml[k]=-1; } for(int j=0;j<np;j++){ sr_hp[j]=sr_hash(&P[j]); sr_mp[j]=0; }
    int i=0, j=0;
    while(i<n&&j<np){
        if(sr_hl[i]==sr_hp[j]&&sr_same(&L[i],&P[j])){ sr_ml[i]=(short)j; sr_mp[j]=1; i++; j++; continue; }
        int best=-1, bi=0, bj=0;
        for(int d=1;d<SR_LOOK&&best<0;d++){
            if(i+d<n&&sr_hl[i+d]==sr_hp[j]&&sr_same(&L[i+d],&P[j])){ best=d; bi=d; bj=0; }
            else if(j+d<np&&sr_hl[i]==sr_hp[j+d]&&sr_same(&L[i],&P[j+d])){ best=d; bi=0; bj=d; } }
        if(best<0){ i++; j++; continue; }
        i+=bi; j+=bj; } }
static int rg_cover(const rgn* g,grc r){ for(int i=0;i<g->n;i++) if(rin(r,g->r[i]))return 1; return 0; }
static void spread2(gop* o,int in,int dx,int dy,int m){ rgn* dst=&sr_cd[o->d]; spread(dst,&sr_cd[in],o,in,dx,dy,m); grc rr=rsh(grect(o),dx,dy,m); rclamp(&rr,in); if(!rg_cover(&sr_w[in],rr)) spread(dst,&sr_chg[in],o,in,dx,dy,m); }
int gq_srec_end(int* ox,int* oy,int* ow,int* oh){
    int full=0; grc F={0,0,(int)g_gs[sr_d].w,(int)g_gs[sr_d].h};
    if(!sr_on){ g_sr_full++; *ox=0; *oy=0; *ow=F.x1; *oh=F.y1; return 0; }
    gop* L=sr_buf[sr_cur]; gop* P=sr_buf[sr_cur^1]; int n=sr_n[sr_cur], np=sr_n[sr_cur^1]; sr_on=0;
    if(np<0||n>4096||np>4096){ full=1; if(!g_sr_why)g_sr_why=np<0?6:3; }
    grc D={0,0,0,0}; int hasD=0;
    if(!full){
        for(int i=0;i<GQ_MAXS;i++){ sr_cd[i].n=0; sr_w[i].n=0; }
        sr_align(L,n,P,np);
        for(int j=0;j<np;j++) if(!sr_mp[j]&&P[j].d<GQ_MAXS) rg_add(&sr_cd[P[j].d],grect(&P[j]));
        for(int k=0;k<n;k++){ gop* o=&L[k]; int same=(sr_ml[k]>=0);
            if(!same){ rg_add(&sr_cd[o->d],grect(o)); }
            else if(!o->ck){
                if(usesrc(o)&&o->s!=o->d) spread2(o,o->s,o->sdx,o->sdy,blurm(o));
                if(usemask(o)&&o->m!=o->d) spread2(o,o->m,o->mdx,o->mdy,0); }
            rg_add(&sr_w[o->d],grect(o)); }
        rgn* C=&sr_cd[sr_d]; for(int i=0;i<C->n;i++){ D=hasD?runi(D,C->r[i]):C->r[i]; hasD=1; }
        rgn* E=&sr_chg[sr_d]; for(int i=0;i<E->n;i++){ D=hasD?runi(D,E->r[i]):E->r[i]; hasD=1; }
        if(hasD){ D=rcap(D,F); if(!rok(D))hasD=0; }
        for(int k=0;k<n;k++)sr_f[k]=0;
        for(int it=0;it<12;it++){ int grow=0;
            for(int i=0;i<GQ_MAXS;i++){ sr_need[i].n=0; sr_rw[i].n=0; }
            for(int k=n-1;k>=0;k--){ gop* o=&L[k]; grc r=grect(o); int ex;
                if(o->d==sr_d){ ex=hasD&&ov(r,D); if(ex)r=rcap(r,D); }
                else ex=sr_f[k]||rg_hit(&sr_need[o->d],r)||rg_hit(&sr_cd[o->d],r);
                sr_x[k]=(u8)ex; if(!ex)continue;
                for(int q=0;q<2;q++){ int in=q?(usemask(o)?o->m:-1):(usesrc(o)?o->s:-1); if(in<0||in==o->d||!sr_def[in])continue;
                    grc R2=q?rsh(r,o->mdx,o->mdy,0):rsh(r,o->sdx,o->sdy,blurm(o)); rclamp(&R2,in);
                    if(in==sr_d){ if(!hasD||!rin(R2,D)){ D=hasD?runi(D,R2):R2; hasD=1; grow=1; } }
                    else rg_add(&sr_need[in],R2); } }
            for(int k=0;k<n;k++){ gop* o=&L[k]; if(o->d==sr_d)continue; grc r=grect(o); if(sr_x[k]) rg_add(&sr_rw[o->d],r); else if(rg_hit(&sr_rw[o->d],r)){ sr_f[k]=1; sr_x[k]=1; rg_add(&sr_rw[o->d],r); grow=1; } }
            if(!grow)break; if(it==11){ full=1; g_sr_why=7; } }
    }
    if(full){ D=F; hasD=1; for(int k=0;k<n&&k<(int)sizeof(sr_x);k++)sr_x[k]=1; }
    int ran=0; sr_exec=1;
    if(full&&n>(int)sizeof(sr_x)){ for(int k=0;k<n;k++)run_op(&L[k]); ran=n; }
    else for(int k=0;k<n;k++){ if(!sr_x[k])continue; gop o=L[k]; if(o.d==sr_d&&!full){ grc r=rcap(grect(&o),D); if(!rok(r))continue; if(o.k==GK_GRADH)o.goff+=(u32)(r.x0-o.x); if(o.k==GK_GRADV)o.goff+=(u32)(r.y0-o.y); o.x=r.x0; o.y=r.y0; o.w=r.x1-r.x0; o.h=r.y1-r.y0; } run_op(&o); ran++; }
    sr_exec=0;
    g_sr_ops=n; g_sr_run=ran;
    if(full){ g_sr_full++; *ox=0; *oy=0; *ow=F.x1; *oh=F.y1; return 0; }
    if(!hasD){ g_sr_none++; *ox=0; *oy=0; *ow=0; *oh=0; return 1; }
    g_sr_part++; *ox=D.x0; *oy=D.y0; *ow=D.x1-D.x0; *oh=D.y1-D.y0; return 1; }
void gq_ext_mark(int s,int x,int y,int w,int h){ if(s<=0||s>=GQ_MAXS)return; grc r={x,y,x+w,y+h}; if(sr_on)rg_add(&sr_chg[s],r); else rg_add(&sr_ext[s],r); }
void gq_log_begin(int d){ lg_d=d; lg_n=0; lg_i=0; }
void gq_log_end(void){ lg_d=0; }
int gq_log_find(int x,int y,char* out,int max){ int i=lg_n-1; for(;i>=0;i--){ glog* L=&lgb[i]; if(x>=L->x&&y>=L->y&&x<L->x+L->w&&y<L->y+L->h)break; }
    char* p=out; char* e=out+max-1; const char* t; if(i<0){ t="no operation"; while(*t&&p<e)*p++=*t++; *p=0; return -1; }
    glog* L=&lgb[i]; char b[120]; char* q=b; q=wtxt(q,gk_name(L->k)); q=wtxt(q," #"); q=wdec(q,L->i); *q++=' '; q=wdec(q,L->x); *q++=','; q=wdec(q,L->y); *q++=' '; q=wdec(q,L->w); *q++='x'; q=wdec(q,L->h); q=wtxt(q," from "); q=wdec(q,L->s); *q++='/'; q=wdec(q,L->m); *q=0;
    t=b; while(*t&&p<e)*p++=*t++; *p=0; return L->i; }
void gq_surf_free(int s){ if(s<=0||s>=GQ_MAXS)return; g_gs[s].used=0; g_gs[s].px=0; g_gs[s].gfx=0; }
void gq_frame_mark(void){ g_gq_frame_ops=g_gq_ops-f_ops0; g_gq_frame_stalls=g_gq_stalls-f_stalls0; g_gq_frame_syncs=g_gq_syncs-f_syncs0; f_ops0=g_gq_ops; f_stalls0=g_gq_stalls; f_syncs0=g_gq_syncs; }
static u32 st_rng;
static u32 RNDv(void){ st_rng^=st_rng<<13; st_rng^=st_rng>>17; st_rng^=st_rng<<5; return st_rng; }
static int RNDi(int lo,int hi){ return lo+(int)(RNDv()%(u32)(hi-lo+1)); }
static char* wtxt(char* d,const char* s){ int i=0; while(s[i]&&i<40){ d[i]=s[i]; i++; } d[i]=0; return d+i; }
static char* whex(char* p,u32 v){ static const char Hx[]="0123456789ABCDEF"; for(int i=28;i>=0;i-=4)*p++=Hx[(v>>i)&15]; return p; }
static char* wdec(char* p,int v){ char t[12]; int n=0; if(v<0){ *p++='-'; v=-v; } do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
typedef struct { int k, d, s, x, y, w, h, sx, sy, mx, my, a, key; u32 c0, c1; } tparm;
static tparm gen(int nd){ tparm p; p.k=RNDi(0,GK_N-1); p.d=RNDi(0,nd-1); p.s=RNDi(0,nd-1); if(p.s==p.d)p.s=(p.d+1)%nd; return p; }
static void gen2(tparm* p,const int* sid){ gsurf* D=&g_gs[sid[p->d]]; gsurf* S=&g_gs[sid[p->s]];
    p->x=RNDi(-12,(int)D->w+4); p->y=RNDi(-12,(int)D->h+4); p->w=RNDi(1,70); p->h=RNDi(1,50); p->sx=RNDi(-8,(int)S->w); p->sy=RNDi(-8,(int)S->h); p->mx=RNDi(-4,40); p->my=RNDi(-4,40);
    p->c0=RNDv()&0xFFFFFFu; p->c1=RNDv()&0xFFFFFFu; p->a=RNDi(0,300); if(RNDi(0,3)==0)p->a=256; p->key=RNDi(0,1); }
static void texec(const tparm* p,const int* sid,int msid){ int d=sid[p->d], s=sid[p->s];
    switch(p->k){ case GK_FILL: gq_fill(d,p->x,p->y,p->w,p->h,p->c0); break; case GK_BLEND: gq_blend(d,p->x,p->y,p->w,p->h,p->c0,p->a); break; case GK_MASK: gq_mask(d,p->x,p->y,p->w,p->h,p->c0,p->a,msid,p->mx,p->my); break;
        case GK_COPY: gq_copy(d,p->x,p->y,p->w,p->h,s,p->sx,p->sy,p->a,p->key); break; case GK_COPYM: gq_copym(d,p->x,p->y,p->w,p->h,s,p->sx,p->sy,msid,p->mx,p->my,p->a,p->key); break;
        case GK_GRADH: case GK_GRADV: gq_grad(d,p->x,p->y,p->w,p->h,p->c0,p->c1,p->k==GK_GRADV); break; case GK_TINT: gq_tint(d,p->x,p->y,p->w,p->h); break;
        case GK_OVER: gq_overc(d,p->x,p->y,p->w,p->h,s,p->sx,p->sy,p->a); break; case GK_OVERM: gq_overm(d,p->x,p->y,p->w,p->h,s,p->sx,p->sy,msid,p->mx,p->my,p->a); break;
        default: gq_blur1(d,p->x,p->y,p->w,p->h,s,p->sx,p->sy,p->k==GK_BLURV); break; } }
static void both(const tparm* p,const int* sid,u32** cpu,int nd,int msid){ texec(p,sid,msid); int m0=g_gq_mode; u32* keep[8]; g_gq_mode=GQM_CPU;
    for(int q=0;q<nd;q++){ keep[q]=g_gs[sid[q]].px; g_gs[sid[q]].px=cpu[q]; } texec(p,sid,msid); for(int q=0;q<nd;q++)g_gs[sid[q]].px=keep[q]; g_gq_mode=m0; }
static int cmpall(const int* sid,u32** cpu,int nd,int* fq,u32* fx,u32* fy,u32* gv,u32* cv){
    for(int q=0;q<nd;q++){ gsurf* G=&g_gs[sid[q]]; for(u32 yy=0;yy<G->h;yy++) for(u32 xx=0;xx<G->w;xx++){ u32 a=G->px[(u64)yy*G->pitch+xx], b=cpu[q][(u64)yy*G->pitch+xx]; if(a!=b){ *fq=q; *fx=xx; *fy=yy; *gv=a; *cv=b; return 1; } } } return 0; }
#define SNAPMAX 49152
static u32 snapg[4][SNAPMAX], snapc[4][SNAPMAX];
static int snap(const int* sid,u32** cpu,int nd,int save){ for(int q=0;q<nd;q++){ gsurf* G=&g_gs[sid[q]]; u64 n=(u64)G->pitch*G->h; if(n>SNAPMAX)return -1;
        for(u64 i=0;i<n;i++){ if(save){ snapg[q][i]=G->px[i]; snapc[q][i]=cpu[q][i]; } else { G->px[i]=snapg[q][i]; cpu[q][i]=snapc[q][i]; } } } return 0; }
static char* dline(char* p,const char* pre,int t,const tparm* o,int q,u32 x,u32 y,u32 gv,u32 cv){ p=wtxt(p,pre); p=wtxt(p,gk_name(o->k)); p=wtxt(p," op "); p=wdec(p,t); p=wtxt(p," image "); p=wdec(p,q); *p++=' '; p=wdec(p,(int)x); *p++=','; p=wdec(p,(int)y);
    p=wtxt(p," got "); p=whex(p,gv); p=wtxt(p," want "); p=whex(p,cv); *p=0; return p; }
int gq_selftest(const int* sid,u32** cpu,int nd,int msid,u32 seed,int n,char* why){
    st_rng=seed?seed:1; int ser=0; tparm P[16]; int t0=0; g_gq_diag[0]=0; int cansnap=(snap(sid,cpu,nd,1)==0);
    for(int t=0;t<n;t++){
        tparm p=gen(nd); gen2(&p,sid); P[t-t0]=p; both(&p,sid,cpu,nd,msid);
        if((t&15)==15||t==n-1){
            if(gq_sync()){ wtxt(why,"a test batch did not finish"); return -2; }
            int fq; u32 fx,fy,gv,cv;
            if(cmpall(sid,cpu,nd,&fq,&fx,&fy,&gv,&cv)){
                char* d=g_gq_diag; d=dline(d,g_gq_serial?"with waits: ":"batched: ",t,&P[t-t0],fq,fx,fy,gv,cv);
                if(!cansnap||g_gq_serial){ dline(why,"",t,&P[t-t0],fq,fx,fy,gv,cv); g_gq_serial=0; return -1; }
                snap(sid,cpu,nd,0); g_gq_serial=1; int bad=-1;
                for(int j=t0;j<=t;j++){ both(&P[j-t0],sid,cpu,nd,msid); if(gq_sync()){ wtxt(why,"a test batch did not finish"); return -2; }
                    if(cmpall(sid,cpu,nd,&fq,&fx,&fy,&gv,&cv)){ bad=j; break; } }
                if(bad>=0){ d=wtxt(d,"; "); dline(d,"alone: ",bad,&P[bad-t0],fq,fx,fy,gv,cv); dline(why,"",bad,&P[bad-t0],fq,fx,fy,gv,cv); g_gq_serial=0; return -1; }
                d=wtxt(d,"; one at a time: all ok"); ser=1; }
            t0=t+1; if(cansnap)snap(sid,cpu,nd,1); } }
    return ser; }
