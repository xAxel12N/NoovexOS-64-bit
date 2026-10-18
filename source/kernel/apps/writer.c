#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/core/syscall.h"
#include "include/nvx/apps/browser.h"
#include "include/nvx/apps/files_classic.h"
#include "include/nvx/arch/x86_64/hrtimer.h"

extern void toast(const char*); extern volatile unsigned char kdown[128]; extern int kshift;
extern const u8 wpf_npt; extern const u8 wpf_pt[]; extern const u8 wpf_px[]; extern const short wpf_met[][2]; extern const int wpf_ncp;
extern const struct { u16 adv16; signed char x0, y0; u8 w, h; u32 off; } wpf_g[]; extern const u8 _binary_wpf_data_bin_start[];
#define wpf_data _binary_wpf_data_bin_start

typedef struct { u16 ch; u8 fam, sz, fl, al; u16 img; u32 col; } wc_t;
typedef struct { u8* raw; int rawlen, png; u32* px; int pw, ph, w, h; } wimg_t;
typedef struct { int s, e, y, h, asc, pg, w, x0; } wline_t;
#define WP_MAXI 32
#define WP_MAXL 20000
#define PGW 794
#define PGH 1123
#define PGM 96
#define PGGAP 24
#define CW (PGW-2*PGM)
#define CH (PGH-2*PGM)
static wc_t* D=0; static int N=0, CAP=0; static wimg_t IM[WP_MAXI]; static int NIM=0;
static wline_t* LN=0; static int NL=0, NPG=1, dirty=1;
static int cur=0, sa=0, scrolly=0, tfam=0, tsz=4, tfl=0; static u32 tcol=0x000000; static int tal=0;
static char wp_name[64]="Document1.docx"; static int wp_mod=0, wp_slot=-1;
static int px0=0, py0=0, pvx=0, pvy=0, pvw=1, pvh=1;
static int modal=0, mlist[64], mn=0; static char mtext[64]; static int mtl=0;
static int bx_[48], by_[48], bw_[48], bh_[48], bid_[48], bn=0;
static wc_t* U[24]; static int UN[24], ucnt=0; static u64 ulast=0; static int ukind=0;
static const char* FAMN[3]={"Sans (Arial)","Serif (Times)","Mono (Courier)"};
static const char* FAMX[3]={"Arial","Times New Roman","Courier New"};
static const u32 PAL[8]={0x000000,0x404040,0xC0392B,0xE67E22,0x27AE60,0x2E86DE,0x8E44AD,0xFFFFFF};

static int st_of(int fam,int fl){ return fam*4+((fl&1)?1:0)+((fl&2)?2:0); }
static int mi_of(const wc_t* c){ return st_of(c->fam,c->fl)*wpf_npt+c->sz; }
static int cpi(int ch){ if(ch>=32&&ch<127)return ch-32; if(ch>=160&&ch<=255)return 95+ch-160; return '?'-32; }
static int adv16(const wc_t* c){ if(c->ch==0xFFFC){ int i=c->img-1; return (i>=0&&i<NIM?IM[i].w:10)*16; } if(c->ch=='\t')return 48*16; if(c->ch=='\n')return 0; return wpf_g[mi_of(c)*wpf_ncp+cpi(c->ch)].adv16; }
static int c_asc(const wc_t* c){ if(c->ch==0xFFFC){ int i=c->img-1; return i>=0&&i<NIM?IM[i].h:10; } return wpf_met[mi_of(c)][0]; }
static int c_desc(const wc_t* c){ if(c->ch==0xFFFC)return 4; return wpf_met[mi_of(c)][1]; }
static int grow(int need){ if(need<=CAP)return 1; int nc=CAP?CAP*2:4096; while(nc<need)nc*=2; wc_t* n=(wc_t*)kmalloc((unsigned long)nc*sizeof(wc_t)); if(!n)return 0; for(int i=0;i<N;i++)n[i]=D[i]; if(D)kfree(D); D=n; CAP=nc; return 1; }
static void doc_reset(void){ N=0; grow(16); D[0].ch='\n'; D[0].fam=0; D[0].sz=4; D[0].fl=0; D[0].al=0; D[0].img=0; D[0].col=0; N=1; cur=sa=0; scrolly=0; for(int i=0;i<NIM;i++){ if(IM[i].raw)kfree(IM[i].raw); if(IM[i].px)kfree(IM[i].px); IM[i].raw=0; IM[i].px=0; } NIM=0; dirty=1; tfam=0; tsz=4; tfl=0; tcol=0; tal=0; }
static void snap(int kind){ u64 now=hr_ns(); if(kind==ukind&&kind==1&&now-ulast<1500000000ULL){ ulast=now; return; } ukind=kind; ulast=now;
    if(ucnt==24){ kfree(U[0]); for(int i=1;i<24;i++){ U[i-1]=U[i]; UN[i-1]=UN[i]; } ucnt--; }
    wc_t* c=(wc_t*)kmalloc((unsigned long)N*sizeof(wc_t)+16); if(!c)return; for(int i=0;i<N;i++)c[i]=D[i]; U[ucnt]=c; UN[ucnt]=N; ucnt++; }
static void undo(void){ if(!ucnt){ toast("Nothing to undo"); return; } ucnt--; if(grow(UN[ucnt])){ for(int i=0;i<UN[ucnt];i++)D[i]=U[ucnt][i]; N=UN[ucnt]; } kfree(U[ucnt]); if(cur>=N)cur=N-1; sa=cur; dirty=1; wp_mod=1; ukind=0; }
static int selmin(void){ return sa<cur?sa:cur; } static int selmax(void){ return sa<cur?cur:sa; }
static void del_range(int a,int b){ if(b>N-1)b=N-1; if(a>=b)return; for(int i=b;i<N;i++)D[a+i-b]=D[i]; N-=b-a; dirty=1; wp_mod=1; }
static void del_sel(void){ int a=selmin(), b=selmax(); if(a<b){ del_range(a,b); cur=sa=a; } }
static void ins(u16 ch,u16 img){ if(!grow(N+1))return; for(int i=N;i>cur;i--)D[i]=D[i-1]; wc_t* c=&D[cur]; c->ch=ch; c->fam=(u8)tfam; c->sz=(u8)tsz; c->fl=(u8)tfl; c->col=tcol; c->img=img; c->al=D[cur+1].al;
    if(ch=='\n')c->al=D[cur+1].al; N++; cur++; sa=cur; dirty=1; wp_mod=1; }
static void pick_style(void){ int k=cur>0?cur-1:cur; if(k<N&&D[k].ch!='\n'&&D[k].ch!=0xFFFC){ tfam=D[k].fam; tsz=D[k].sz; tfl=D[k].fl&15; tcol=D[k].col; } else if(cur<N&&D[cur].ch!=0xFFFC){ tfam=D[cur].fam; tsz=D[cur].sz; tfl=D[cur].fl&15; tcol=D[cur].col; } }
static int para_end(int i){ while(i<N-1&&D[i].ch!='\n')i++; return i; }
static int para_start(int i){ while(i>0&&D[i-1].ch!='\n')i--; return i; }
static void layout(void){
    if(!dirty)return; dirty=0; if(!LN)LN=(wline_t*)kmalloc(sizeof(wline_t)*WP_MAXL); if(!LN)return;
    NL=0; int pg=0, y=0; int i=0;
    while(i<N&&NL<WP_MAXL){
        int pe=para_end(i); int al=D[pe].al;
        int s=i;
        do { int w16=0, lastbrk=-1, k=s; int asc=0, desc=0;
            while(k<pe){ int a=adv16(&D[k]); if(w16+a>CW*16&&k>s){ if(lastbrk>s){ k=lastbrk; } break; } w16+=a; if(D[k].ch==' ')lastbrk=k+1; k++; }
            int e=k; if(e==pe)e=pe+1;
            int ww=0; for(int q=s;q<e&&q<=pe;q++){ if(q<pe)ww+=adv16(&D[q]); int A=c_asc(&D[q]), Dd=c_desc(&D[q]); if(A>asc)asc=A; if(Dd>desc)desc=Dd; }
            int lh=(asc+desc)*115/100; if(lh<8)lh=8;
            if(y+lh>CH&&y>0){ pg++; y=0; }
            wline_t* L=&LN[NL++]; L->s=s; L->e=e; L->y=y; L->h=lh; L->asc=asc+(lh-asc-desc)/2; L->pg=pg; L->w=ww/16;
            { int ew=ww; int tw=ew; int q=e-1; while(q>s&&q<pe&&D[q].ch==' '){ tw-=adv16(&D[q]); q--; } tw/=16; L->x0=al==1?(CW-tw)/2:al==2?(CW-tw):0; if(L->x0<0)L->x0=0; }
            y+=lh; s=e; if(NL>=WP_MAXL)break;
        } while(s<=pe);
        i=pe+1; }
    NPG=pg+1; }
static int line_of(int pos){ for(int l=0;l<NL;l++){ if(pos>=LN[l].s&&pos<LN[l].e)return l; } return NL-1; }
static int x_of(int l,int pos){ wline_t* L=&LN[l]; int pe=para_end(L->s); int al=D[pe].al; int x16=L->x0*16; int nsp=0, ex=0;
    if(al==3&&L->e<=pe){ for(int q=L->s;q<L->e;q++)if(D[q].ch==' ')nsp++; if(nsp)ex=(CW-L->w)*16/nsp; }
    for(int q=L->s;q<pos&&q<L->e;q++){ x16+=adv16(&D[q]); if(nsp&&D[q].ch==' ')x16+=ex; } return x16/16; }
static int pos_at(int l,int x){ wline_t* L=&LN[l]; int best=L->s; int bd=1<<30; int last=L->e; if(last>N-1)last=N-1; int pe=para_end(L->s); if(L->e>pe)last=pe;
    for(int q=L->s;q<=last;q++){ int d=x_of(l,q)-x; if(d<0)d=-d; if(d<bd){ bd=d; best=q; } } return best; }
static int page_y(int pg){ return pg*(PGH+PGGAP); }
static void hit(int x,int y,int w,int h,int id){ if(bn<48){ bx_[bn]=x; by_[bn]=y; bw_[bn]=w; bh_[bn]=h; bid_[bn]=id; bn++; } }
static void blend(u32* p,u32 c,int a){ if(a>=255){ *p=c; return; } u32 d=*p; u32 rb=((c&0xFF00FF)*(u32)a+(d&0xFF00FF)*(u32)(255-a))>>8; u32 g=((c&0xFF00)*(u32)a+(d&0xFF00)*(u32)(255-a))>>8; *p=(rb&0xFF00FF)|(g&0xFF00); }
static void glyph(u32* sc,int x,int y,const wc_t* c,int cx0,int cy0,int cx1,int cy1){
    int gi=mi_of(c)*wpf_ncp+cpi(c->ch); int w=wpf_g[gi].w, h=wpf_g[gi].h; if(!w||!h)return; const u8* d=wpf_data+wpf_g[gi].off; int bw=(w+1)/2;
    int gx=x+wpf_g[gi].x0, gy=y+wpf_g[gi].y0;
    for(int r=0;r<h;r++){ int yy=gy+r; if(yy<cy0||yy>=cy1||(u32)yy>=H)continue; u32* row=sc+(u64)yy*SW;
        for(int k=0;k<w;k++){ int xx=gx+k; if(xx<cx0||xx>=cx1||(u32)xx>=W)continue; u8 b=d[r*bw+k/2]; int v=(k&1)?(b&15):(b>>4); if(v)blend(row+xx,c->col,v*17); } } }
static void draw_doc(int vx,int vy,int vw,int vh){
    layout(); u32* sc=SCENE; int totalh=NPG*(PGH+PGGAP);
    if(scrolly>totalh-vh+40)scrolly=totalh-vh+40; if(scrolly<0)scrolly=0;
    int pxl=vx+(vw-PGW)/2; if(pxl<vx+8)pxl=vx+8; px0=pxl; py0=vy+16-scrolly;
    for(int p=0;p<NPG;p++){ int ty=py0+page_y(p); if(ty>vy+vh||ty+PGH<vy)continue;
        int y0=ty<vy?vy:ty, y1=ty+PGH>vy+vh?vy+vh:ty+PGH; fill_rect(pxl+3,y0+3>vy+vh?vy+vh:y0+3,PGW,(y1-y0)>3?y1-y0-3:0,rgb(170,174,184)); fill_rect(pxl,y0,PGW,y1-y0,rgb(255,255,255));
        char t[24],*q=t; q=scopy(q,"Page "); q=udec(q,(u32)(p+1)); *q=0; if(ty+PGH-30>vy&&ty+PGH-30<vy+vh-14) dstr_ctr(pxl+PGW/2,ty+PGH-40,t,rgb(170,176,190),1); }
    int a=selmin(), b=selmax(); int cx0=vx, cy0=vy, cx1=vx+vw, cy1=vy+vh;
    for(int l=0;l<NL;l++){ wline_t* L=&LN[l]; int ly=py0+page_y(L->pg)+PGM+L->y; if(ly+L->h<vy||ly>vy+vh)continue; int lx=pxl+PGM;
        int pe=para_end(L->s); int al=D[pe].al; int nsp=0, ex16=0; if(al==3&&L->e<=pe){ for(int q=L->s;q<L->e;q++)if(D[q].ch==' ')nsp++; if(nsp)ex16=(CW-L->w)*16/nsp; }
        int x16=L->x0*16; int base=ly+L->asc;
        for(int q=L->s;q<L->e&&q<N;q++){ const wc_t* c=&D[q]; int aw=adv16(c); if(nsp&&c->ch==' ')aw+=ex16; int gx=lx+x16/16, nx=lx+(x16+aw)/16;
            if(q>=a&&q<b&&c->ch!='\n'){ int yy0=ly<cy0?cy0:ly, yy1=ly+L->h>cy1?cy1:ly+L->h; for(int yy=yy0;yy<yy1;yy++){ u32* row=sc+(u64)yy*SW; for(int xx=gx;xx<nx;xx++) if(xx>=cx0&&xx<cx1)blend(row+xx,0x7AA7F0,110); } }
            if(c->ch==0xFFFC){ int ii=c->img-1; if(ii>=0&&ii<NIM&&IM[ii].px){ wimg_t* im=&IM[ii]; int ix=gx, iy=base-im->h; for(int yy=0;yy<im->h;yy++){ int sy=iy+yy; if(sy<cy0||sy>=cy1)continue; u32* row=sc+(u64)sy*SW; int srcy=yy*im->ph/im->h; for(int xx=0;xx<im->w;xx++){ int sx=ix+xx; if(sx<cx0||sx>=cx1)continue; row[sx]=im->px[(u64)srcy*im->pw+xx*im->pw/im->w]; } } } }
            else if(c->ch>32&&c->ch!=0xA0) glyph(sc,gx,base,c,cx0,cy0,cx1,cy1);
            if(c->fl&4){ int uy=base+2+wpf_px[c->sz]/14; if(uy>=cy0&&uy<cy1){ u32* row=sc+(u64)uy*SW; for(int xx=gx;xx<nx;xx++)if(xx>=cx0&&xx<cx1)row[xx]=c->col; } }
            if(c->fl&8){ int uy=base-wpf_px[c->sz]*3/10; if(uy>=cy0&&uy<cy1){ u32* row=sc+(u64)uy*SW; for(int xx=gx;xx<nx;xx++)if(xx>=cx0&&xx<cx1)row[xx]=c->col; } }
            x16+=aw; } }
    { int l=line_of(cur); if(l>=0){ wline_t* L=&LN[l]; int x=px0+PGM+x_of(l,cur), y=py0+page_y(L->pg)+PGM+L->y; if((hr_ns()/530000000ULL)%2==0||modal) for(int yy=y+2;yy<y+L->h-2;yy++) if(yy>=cy0&&yy<cy1&&x>=cx0&&x<cx1){ sc[(u64)yy*SW+x]=0x101010; if(x+1<cx1)sc[(u64)yy*SW+x+1]=0x101010; } } }
    { int sbx=vx+vw-10; int th=vh*vh/(totalh>vh?totalh:vh); if(th<30)th=30; int ty=vy+(totalh>vh?scrolly*(vh-th)/(totalh-vh+40>1?totalh-vh+40:1):0); fill_round(sbx,vy+2,6,vh-4,3,15,rgb(214,218,226)); fill_round(sbx,ty,6,th,3,15,rgb(150,158,176)); } }
static void ensure_visible(void){ layout(); int l=line_of(cur); if(l<0)return; int y=page_y(LN[l].pg)+PGM+LN[l].y+16; if(y-scrolly<20)scrolly=y-40; if(y+LN[l].h-scrolly>pvh-10)scrolly=y+LN[l].h-pvh+40; if(scrolly<0)scrolly=0; }
static void tbtn(int x,int y,int w,const char* t,int on,int id){ int hov=mx>=x&&mx<x+w&&my>=y&&my<y+28; fill_round(x,y,w,28,6,15,on?mix(rgb(255,255,255),ACC,60):hov?rgb(228,232,240):rgb(255,255,255)); dstr_ctr(x+w/2,y+7,t,on?ACC:rgb(30,34,46),1); hit(x,y,w,28,id); }
static int sel_has(int bit){ int a=selmin(), b=selmax(); if(a==b)return (tfl&bit)!=0; for(int q=a;q<b;q++) if(D[q].ch!='\n'&&!(D[q].fl&bit))return 0; return 1; }
static void apply(int what,int v){
    int a=selmin(), b=selmax();
    if(a==b){ if(what==0)tfam=v; else if(what==1)tsz=v; else if(what==2)tfl^=v; else if(what==3)tcol=(u32)v; return; }
    snap(2); int on=0; if(what==2)on=!sel_has(v);
    for(int q=a;q<b;q++){ if(D[q].ch=='\n'&&what!=1)continue; if(what==0)D[q].fam=(u8)v; else if(what==1)D[q].sz=(u8)v; else if(what==2){ if(on)D[q].fl|=(u8)v; else D[q].fl&=(u8)~v; } else if(what==3)D[q].col=(u32)v; }
    if(what==2){ if(on)tfl|=v; else tfl&=~v; } else if(what==0)tfam=v; else if(what==1)tsz=v; else tcol=(u32)v;
    dirty=1; wp_mod=1; }
static void set_align(int al){ snap(2); int a=para_start(selmin()), b=para_end(selmax()); for(int q=a;q<=b&&q<N;q++)D[q].al=(u8)al; dirty=1; wp_mod=1; }
static void heading(int lv){ snap(2); int a=para_start(selmin()), b=para_end(selmax()); int sz=lv==1?9:lv==2?7:4; for(int q=a;q<=b&&q<N;q++){ D[q].sz=(u8)sz; if(lv)D[q].fl|=1; else D[q].fl&=(u8)~1; } tsz=sz; tfl=lv?(tfl|1):(tfl&~1); dirty=1; wp_mod=1; }
static int sz_idx(int pt){ int b=0; for(int i=0;i<wpf_npt;i++){ int d=wpf_pt[i]-pt; int db=wpf_pt[b]-pt; if(d<0)d=-d; if(db<0)db=-db; if(d<db)b=i; } return b; }
static int add_image(const u8* data,int len){
    if(NIM>=WP_MAXI)return -1; int w=0,h=0; u8* rgba=(u8*)kmalloc(4096u*4096u); if(!rgba)return -1; int r=-1;
    if(len>8&&data[0]==0x89&&data[1]==0x50) r=png_decode(data,len,PNG_RAW,PNG_RAW_SZ,rgba,4096*4096,&w,&h);
    else if(len>3&&data[0]==0xFF&&data[1]==0xD8) r=jpeg_decode(data,len,rgba,4096*4096,&w,&h);
    if(r!=0||w<=0||h<=0){ kfree(rgba); return -1; }
    int dw=w, dh=h; if(dw>CW){ dh=dh*CW/dw; dw=CW; } if(dh>CH-40){ dw=dw*(CH-40)/dh; dh=CH-40; } if(dw<1)dw=1; if(dh<1)dh=1;
    int pw=dw, ph=dh; u32* px=(u32*)kmalloc((unsigned long)pw*ph*4); u8* raw=(u8*)kmalloc((unsigned long)len); if(!px||!raw){ if(px)kfree(px); if(raw)kfree(raw); kfree(rgba); return -1; }
    for(int y=0;y<ph;y++) for(int x=0;x<pw;x++){ const u8* s=rgba+((u64)(y*h/ph)*w+(x*w/pw))*4; u32 al=s[3]; u32 rr=(s[0]*al+255*(255-al))/255, gg=(s[1]*al+255*(255-al))/255, bb=(s[2]*al+255*(255-al))/255; px[(u64)y*pw+x]=(rr<<16)|(gg<<8)|bb; }
    for(int i=0;i<len;i++)raw[i]=data[i]; kfree(rgba);
    wimg_t* im=&IM[NIM]; im->raw=raw; im->rawlen=len; im->png=data[0]==0x89; im->px=px; im->pw=pw; im->ph=ph; im->w=dw; im->h=dh; return NIM++; }
typedef struct { u8* p; u32 n, cap; } bufw;
static void bput(bufw* b,const void* d,u32 n){ if(b->n+n>b->cap){ u32 nc=b->cap?b->cap*2:65536; while(nc<b->n+n)nc*=2; u8* q=(u8*)kmalloc(nc); if(!q)return; for(u32 i=0;i<b->n;i++)q[i]=b->p[i]; if(b->p)kfree(b->p); b->p=q; b->cap=nc; } for(u32 i=0;i<n;i++)b->p[b->n+i]=((const u8*)d)[i]; b->n+=n; }
static void bs(bufw* b,const char* s){ u32 n=0; while(s[n])n++; bput(b,s,n); }
static void bu(bufw* b,u32 v){ char t[16],*q=t; q=udec(q,v); *q=0; bs(b,t); }
static void bhex(bufw* b,u32 v){ const char* H="0123456789ABCDEF"; char t[7]; for(int i=0;i<6;i++)t[i]=H[(v>>(20-4*i))&15]; t[6]=0; bs(b,t); }
static void b8(bufw* b,u8 v){ bput(b,&v,1); } static void b16(bufw* b,u32 v){ b8(b,(u8)v); b8(b,(u8)(v>>8)); } static void b32(bufw* b,u32 v){ b16(b,v&0xFFFF); b16(b,v>>16); }
static u32 crc_t[256]; static int crc_ok=0;
static u32 crc32(const u8* p,u32 n){ if(!crc_ok){ for(u32 i=0;i<256;i++){ u32 v=i; for(int k=0;k<8;k++)v=(v&1)?0xEDB88320u^(v>>1):v>>1; crc_t[i]=v; } crc_ok=1; } u32 c=0xFFFFFFFFu; for(u32 i=0;i<n;i++)c=crc_t[(c^p[i])&255]^(c>>8); return ~c; }
typedef struct { const char* name; u32 off, crc, len; } zfile;
static void zadd(bufw* z,zfile* f,const char* name,const u8* d,u32 n){ f->name=name; f->off=z->n; f->crc=crc32(d,n); f->len=n; u32 nl=0; while(name[nl])nl++;
    b32(z,0x04034B50u); b16(z,20); b16(z,0); b16(z,0); b16(z,0); b16(z,0x5821); b32(z,f->crc); b32(z,n); b32(z,n); b16(z,nl); b16(z,0); bput(z,name,nl); bput(z,d,n); }
static void zend(bufw* z,zfile* f,int nf){ u32 cd=z->n; for(int i=0;i<nf;i++){ u32 nl=0; while(f[i].name[nl])nl++; b32(z,0x02014B50u); b16(z,20); b16(z,20); b16(z,0); b16(z,0); b16(z,0); b16(z,0x5821); b32(z,f[i].crc); b32(z,f[i].len); b32(z,f[i].len); b16(z,nl); b16(z,0); b16(z,0); b16(z,0); b16(z,0); b32(z,0); b32(z,f[i].off); bput(z,f[i].name,nl); }
    u32 cs=z->n-cd; b32(z,0x06054B50u); b16(z,0); b16(z,0); b16(z,(u32)nf); b16(z,(u32)nf); b32(z,cs); b32(z,cd); b16(z,0); }
static void xml_text(bufw* b,u16 c){ if(c=='&')bs(b,"&amp;"); else if(c=='<')bs(b,"&lt;"); else if(c=='>')bs(b,"&gt;"); else if(c<128)b8(b,(u8)c); else { b8(b,(u8)(0xC0|(c>>6))); b8(b,(u8)(0x80|(c&63))); } }
static void run_props(bufw* b,const wc_t* c){
    bs(b,"<w:rPr><w:rFonts w:ascii=\""); bs(b,FAMX[c->fam]); bs(b,"\" w:hAnsi=\""); bs(b,FAMX[c->fam]); bs(b,"\" w:cs=\""); bs(b,FAMX[c->fam]); bs(b,"\"/>");
    if(c->fl&1)bs(b,"<w:b/>"); if(c->fl&2)bs(b,"<w:i/>"); if(c->fl&8)bs(b,"<w:strike/>"); if(c->col){ bs(b,"<w:color w:val=\""); bhex(b,c->col); bs(b,"\"/>"); }
    bs(b,"<w:sz w:val=\""); bu(b,(u32)wpf_pt[c->sz]*2); bs(b,"\"/><w:szCs w:val=\""); bu(b,(u32)wpf_pt[c->sz]*2); bs(b,"\"/>");
    if(c->fl&4)bs(b,"<w:u w:val=\"single\"/>"); bs(b,"</w:rPr>"); }
static int same_run(const wc_t* a,const wc_t* b){ return a->fam==b->fam&&a->sz==b->sz&&a->fl==b->fl&&a->col==b->col; }
static int save_docx(const char* name){
    bufw doc={0,0,0}; bs(&doc,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><w:body>");
    int i=0; while(i<N){ int pe=para_end(i); static const char* JC[4]={"left","center","right","both"};
        bs(&doc,"<w:p><w:pPr>"); if(D[pe].al){ bs(&doc,"<w:jc w:val=\""); bs(&doc,JC[D[pe].al&3]); bs(&doc,"\"/>"); } bs(&doc,"<w:spacing w:after=\"0\" w:line=\"276\" w:lineRule=\"auto\"/>"); run_props(&doc,&D[pe]); bs(&doc,"</w:pPr>");
        int q=i; while(q<pe){ if(D[q].ch==0xFFFC){ int ii=D[q].img-1; if(ii>=0&&ii<NIM){ u32 ex=(u32)IM[ii].w*9525u, ey=(u32)IM[ii].h*9525u;
                    bs(&doc,"<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\""); bu(&doc,ex); bs(&doc,"\" cy=\""); bu(&doc,ey); bs(&doc,"\"/><wp:docPr id=\""); bu(&doc,(u32)ii+1); bs(&doc,"\" name=\"Picture "); bu(&doc,(u32)ii+1);
                    bs(&doc,"\"/><a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><pic:pic><pic:nvPicPr><pic:cNvPr id=\""); bu(&doc,(u32)ii+1); bs(&doc,"\" name=\"image"); bu(&doc,(u32)ii+1); bs(&doc,IM[ii].png?".png":".jpeg");
                    bs(&doc,"\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdImg"); bu(&doc,(u32)ii+1); bs(&doc,"\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\""); bu(&doc,ex); bs(&doc,"\" cy=\""); bu(&doc,ey);
                    bs(&doc,"\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr></pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>"); } q++; continue; }
            int r=q; while(r<pe&&D[r].ch!=0xFFFC&&same_run(&D[r],&D[q]))r++;
            bs(&doc,"<w:r>"); run_props(&doc,&D[q]);
            int t=q; while(t<r){ if(D[t].ch=='\t'){ bs(&doc,"<w:tab/>"); t++; continue; } bs(&doc,"<w:t xml:space=\"preserve\">"); while(t<r&&D[t].ch!='\t'){ xml_text(&doc,D[t].ch); t++; } bs(&doc,"</w:t>"); }
            bs(&doc,"</w:r>"); q=r; }
        bs(&doc,"</w:p>"); i=pe+1; }
    bs(&doc,"<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>");
    bufw ct={0,0,0}; bs(&ct,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Default Extension=\"png\" ContentType=\"image/png\"/><Default Extension=\"jpeg\" ContentType=\"image/jpeg\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>");
    bufw rl={0,0,0}; bs(&rl,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
    bufw dr={0,0,0}; bs(&dr,"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for(int k=0;k<NIM;k++){ bs(&dr,"<Relationship Id=\"rIdImg"); bu(&dr,(u32)k+1); bs(&dr,"\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"media/image"); bu(&dr,(u32)k+1); bs(&dr,IM[k].png?".png\"/>":".jpeg\"/>"); }
    bs(&dr,"</Relationships>");
    bufw z={0,0,0}; zfile f[40]; int nf=0; static char mnames[WP_MAXI][32];
    zadd(&z,&f[nf++],"[Content_Types].xml",ct.p,ct.n); zadd(&z,&f[nf++],"_rels/.rels",rl.p,rl.n); zadd(&z,&f[nf++],"word/document.xml",doc.p,doc.n); zadd(&z,&f[nf++],"word/_rels/document.xml.rels",dr.p,dr.n);
    for(int k=0;k<NIM&&nf<40;k++){ char* q=mnames[k]; q=scopy(q,"word/media/image"); q=udec(q,(u32)k+1); q=scopy(q,IM[k].png?".png":".jpeg"); *q=0; zadd(&z,&f[nf++],mnames[k],IM[k].raw,(u32)IM[k].rawlen); }
    zend(&z,f,nf);
    int r=z.p?fs_save_bin(name,z.p,(int)z.n,0):-1;
    if(doc.p)kfree(doc.p); if(ct.p)kfree(ct.p); if(rl.p)kfree(rl.p); if(dr.p)kfree(dr.p); if(z.p)kfree(z.p);
    if(r>=0){ wp_mod=0; wp_slot=r; } return r; }
static int xs(const char* p,const char* e,const char* s){ while(*s){ if(p>=e||*p!=*s)return 0; p++; s++; } return 1; }
static int attr(const char* p,const char* e,const char* name,char* out,int cap){
    const char* q=p; while(q<e&&*q!='>'){ if(xs(q,e,name)){ q+=slen(name); if(q<e&&*q=='='){ q++; char qt=*q; if(qt!='"'&&qt!='\'')return 0; q++; int n=0; while(q<e&&*q!=qt&&n<cap-1)out[n++]=*q++; out[n]=0; return 1; } } q++; } return 0; }
static int is_on(const char* p,const char* e){ char v[16]; if(!attr(p,e," w:val",v,16))return 1; return !(v[0]=='0'||v[0]=='f'||(v[0]=='n'&&v[1]=='o')); }
static int fam_of(const char* n){ for(int i=0;n[i];i++){ char c=n[i]; if(c=='C'&&n[i+1]=='o'&&n[i+2]=='u')return 2; if(c=='M'&&n[i+1]=='o'&&n[i+2]=='n')return 2; if(c=='C'&&n[i+1]=='o'&&n[i+2]=='n')return 2; if(c=='T'&&n[i+1]=='i'&&n[i+2]=='m')return 1; if(c=='S'&&n[i+1]=='e'&&n[i+2]=='r')return 1; if(c=='G'&&n[i+1]=='e'&&n[i+2]=='o')return 1; if(c=='C'&&n[i+1]=='a'&&n[i+2]=='m')return 1; if(c=='G'&&n[i+1]=='a'&&n[i+2]=='r')return 1; } return 0; }
static void push_ch(u16 ch,int fam,int sz,int fl,u32 col,int img){ if(!grow(N+1))return; D[N]=D[N-1]; wc_t* d=&D[N-1]; d->ch=ch; d->fam=(u8)fam; d->sz=(u8)sz; d->fl=(u8)fl; d->col=col; d->img=(u16)img; d->al=0; N++; }
static int open_docx(int slot){
    zip_slot=-1; zip_open(slot); int di=-1, ri=-1; for(int i=0;i<znent;i++){ if(seq(zent[i].name,"word/document.xml"))di=i; if(seq(zent[i].name,"word/_rels/document.xml.rels"))ri=i; }
    if(di<0){ toast("Not a Word document (no word/document.xml)"); return -1; }
    u32 cap=zent[di].usize+16; if(cap<64)cap=64; char* x=(char*)kmalloc(cap); if(!x)return -1; int L=zip_extract(slot,di,(u8*)x,(int)cap); if(L<=0){ kfree(x); toast("Could not unpack the document"); return -1; }
    char* rels=0; int RL=0; if(ri>=0){ u32 rc=zent[ri].usize+16; rels=(char*)kmalloc(rc); if(rels){ RL=zip_extract(slot,ri,(u8*)rels,(int)rc); if(RL<0)RL=0; } }
    doc_reset(); N=1; int paral=0, fam=0, sz=sz_idx(11), fl=0; u32 col=0; int inr=0, inppr=0, dsz=sz_idx(11), psz=sz_idx(11), pfl=0;
    const char* p=x; const char* e=x+L;
    while(p<e){ if(*p!='<'){ p++; continue; }
        if(xs(p,e,"<w:p>")||xs(p,e,"<w:p ")){ paral=0; inppr=0; psz=dsz; pfl=0; }
        else if(xs(p,e,"<w:pStyle ")){ char v[32]; if(attr(p,e," w:val",v,32)){ if(xs(v,v+32,"Title")){ psz=sz_idx(26); pfl=0; } else if(xs(v,v+32,"Heading1")||xs(v,v+32,"heading1")){ psz=sz_idx(16); pfl=1; } else if(xs(v,v+32,"Heading2")||xs(v,v+32,"heading2")){ psz=sz_idx(14); pfl=1; } else if(xs(v,v+32,"Heading")){ psz=sz_idx(12); pfl=1; } else if(xs(v,v+32,"Subtitle")){ psz=sz_idx(14); pfl=2; } } }
        else if(xs(p,e,"<w:pPr"))inppr=1; else if(xs(p,e,"</w:pPr"))inppr=0;
        else if(xs(p,e,"<w:jc ")){ char v[16]; if(attr(p,e," w:val",v,16)){ paral=v[0]=='c'?1:(v[0]=='r'||(v[0]=='e'&&v[1]=='n'))?2:v[0]=='b'?3:0; } }
        else if(xs(p,e,"<w:r>")||xs(p,e,"<w:r ")){ inr=1; fam=0; sz=psz; fl=pfl; col=0; }
        else if(xs(p,e,"</w:r>"))inr=0;
        else if(!inppr&&(xs(p,e,"<w:b/>")||xs(p,e,"<w:b "))){ if(is_on(p,e))fl|=1; else fl&=~1; }
        else if(!inppr&&(xs(p,e,"<w:i/>")||xs(p,e,"<w:i "))){ if(is_on(p,e))fl|=2; else fl&=~2; }
        else if(!inppr&&(xs(p,e,"<w:strike/>")||xs(p,e,"<w:strike "))){ if(is_on(p,e))fl|=8; }
        else if(!inppr&&xs(p,e,"<w:u ")){ char v[16]; if(attr(p,e," w:val",v,16)&&!(v[0]=='n'&&v[1]=='o'))fl|=4; }
        else if(!inppr&&xs(p,e,"<w:sz ")){ char v[16]; if(attr(p,e," w:val",v,16)){ int n=0; for(int k=0;v[k]>='0'&&v[k]<='9';k++)n=n*10+v[k]-'0'; if(n>0)sz=sz_idx((n+1)/2); } }
        else if(!inppr&&xs(p,e,"<w:color ")){ char v[16]; if(attr(p,e," w:val",v,16)&&v[0]!='a'){ u32 c=0; for(int k=0;k<6&&v[k];k++){ char h=v[k]; c=c*16+(u32)(h>='a'?h-'a'+10:h>='A'?h-'A'+10:h-'0'); } col=c; } }
        else if(!inppr&&xs(p,e,"<w:rFonts ")){ char v[48]; if(attr(p,e," w:ascii",v,48))fam=fam_of(v); }
        else if(xs(p,e,"<w:tab/>")&&inr){ push_ch('\t',fam,sz,fl,col,0); }
        else if(xs(p,e,"<w:br/>")&&inr){ push_ch('\n',fam,sz,fl,col,0); D[N-2].al=(u8)paral; }
        else if(xs(p,e,"<a:blip ")&&rels){ char id[32]; if(attr(p,e," r:embed",id,32)){ const char* r=rels; const char* re=rels+RL; char tgt[96]; tgt[0]=0;
                while(r<re){ if(xs(r,re,"<Relationship ")){ char rid[32]; if(attr(r,re," Id",rid,32)&&seq(rid,id)){ attr(r,re," Target",tgt,96); break; } } r++; }
                if(tgt[0]){ char full[128],*q=full; if(!xs(tgt,tgt+96,"word/"))q=scopy(q,"word/"); q=scopy(q,tgt[0]=='/'?tgt+1:tgt); *q=0;
                    for(int k=0;k<znent;k++) if(seq(zent[k].name,full)){ u32 mc=zent[k].usize+16; u8* m=(u8*)kmalloc(mc); if(m){ int ml=zip_extract(slot,k,m,(int)mc); if(ml>0){ int ii=add_image(m,ml); if(ii>=0)push_ch(0xFFFC,fam,sz,fl,col,ii+1); } kfree(m); } break; } } } }
        else if((xs(p,e,"<w:t>")||xs(p,e,"<w:t "))&&inr){ while(p<e&&*p!='>')p++; p++;
            while(p<e&&*p!='<'){ u32 c=(u8)*p;
                if(c=='&'){ if(xs(p,e,"&amp;")){ c='&'; p+=5; } else if(xs(p,e,"&lt;")){ c='<'; p+=4; } else if(xs(p,e,"&gt;")){ c='>'; p+=4; } else if(xs(p,e,"&quot;")){ c='"'; p+=6; } else if(xs(p,e,"&apos;")){ c='\''; p+=6; } else p++; }
                else if(c>=0xC0&&c<0xE0&&p+1<e){ c=((c&31)<<6)|((u8)p[1]&63); p+=2; } else if(c>=0xE0&&c<0xF0&&p+2<e){ u32 u=((c&15)<<12)|(((u8)p[1]&63)<<6)|((u8)p[2]&63); p+=3; c=u==0x2019||u==0x2018?'\'':u==0x201C||u==0x201D?'"':u==0x2013||u==0x2014?'-':u==0x2026?'.':u==0xA0?' ':'?'; }
                else if(c>=0xF0){ p+=4; c='?'; } else p++;
                if(c>255)c='?'; if(c<32&&c!='\t')continue; push_ch((u16)c,fam,sz,fl,col,0); }
            continue; }
        else if(xs(p,e,"</w:p>")){ push_ch('\n',fam,dsz,0,0,0); D[N-2].al=(u8)paral; for(int k=N-3;k>=0&&D[k].ch!='\n';k--)D[k].al=(u8)paral; }
        p++; }
    if(N>1){ N--; D[N-1].ch='\n'; }
    kfree(x); if(rels)kfree(rels); cur=sa=0; dirty=1; wp_mod=0; ucnt=0; return 0; }
static int open_txt(int slot){ doc_reset(); const char* t=fsd(slot); int n=FS[slot].len; for(int i=0;i<n;i++){ u8 c=(u8)t[i]; if(c=='\r')continue; if(c=='\n'){ push_ch('\n',0,4,0,0,0); continue; } if(c=='\t'||c>=32)push_ch(c<128||c>=160?c:'?',0,4,0,0,0); } if(N>1){ N--; D[N-1].ch='\n'; } cur=sa=0; dirty=1; wp_mod=0; ucnt=0; return 0; }
int writer_open_slot(int k){
    if(k<0||!FS[k].used)return -1; if(!D)doc_reset();
    int r=fext_eq(FS[k].name,"docx")?open_docx(k):open_txt(k);
    if(r==0){ int i=0; for(;FS[k].name[i]&&i<63;i++)wp_name[i]=FS[k].name[i]; wp_name[i]=0; if(!fext_eq(wp_name,"docx")){ int d=-1; for(int j=0;wp_name[j];j++)if(wp_name[j]=='.')d=j; if(d<0)d=i; if(d<58){ scopy(wp_name+d,".docx"); } } wp_slot=k; }
    open_win(W_WRITER); return r; }
static void do_save(void){ int r=save_docx(wp_name); char t[96],*q=t; q=scopy(q,r>=0?"Saved ":"Could not save "); q=scopy(q,wp_name); *q=0; toast(t); }
static void mlist_build(int images){ mn=0; for(int k=0;k<FS_N&&mn<64;k++){ if(!FS[k].used)continue; const char* nm=FS[k].name; int ok=images?(fext_eq(nm,"png")||fext_eq(nm,"jpg")||fext_eq(nm,"jpeg")):(fext_eq(nm,"docx")||fext_eq(nm,"txt")||fext_eq(nm,"md")); if(ok)mlist[mn++]=k; } }
static void cmd(int id){
    if(id==1){ snap(3); doc_reset(); scopy(wp_name,"Document1.docx"); wp_mod=0; wp_slot=-1; }
    else if(id==2){ modal=1; mlist_build(0); }
    else if(id==3){ do_save(); }
    else if(id==4){ modal=3; int i=0; for(;wp_name[i]&&i<63;i++)mtext[i]=wp_name[i]; mtext[i]=0; mtl=i; }
    else if(id==5){ tfam=(tfam+1)%3; apply(0,tfam); }
    else if(id==6){ int s=tsz>0?tsz-1:0; apply(1,s); } else if(id==7){ int s=tsz<wpf_npt-1?tsz+1:tsz; apply(1,s); }
    else if(id==8)apply(2,1); else if(id==9)apply(2,2); else if(id==10)apply(2,4); else if(id==11)apply(2,8);
    else if(id>=12&&id<=15)set_align(id-12);
    else if(id==16){ modal=2; mlist_build(1); }
    else if(id>=20&&id<=27)apply(3,(int)PAL[id-20]);
    else if(id==30)heading(1); else if(id==31)heading(2); else if(id==32)heading(0);
    else if(id==33)undo(); }
void writer_draw(int i){
    if(!D)doc_reset(); win_frame(i,wp_mod?"Writer - unsaved changes":"Writer"); bn=0;
    int wx=WN[i].x+1, wy=WN[i].y+TITLEH, ww=WN[i].w-2, wh=WN[i].h-TITLEH-1;
    fill_rect(wx,wy,ww,84,rgb(246,247,250)); fill_rect(wx,wy+84,ww,1,rgb(222,225,232));
    int x=wx+8, y=wy+8;
    tbtn(x,y,48,"New",0,1); x+=52; tbtn(x,y,52,"Open",0,2); x+=56; tbtn(x,y,52,"Save",0,3); x+=56; tbtn(x,y,70,"Save as",0,4); x+=78; tbtn(x,y,50,"Undo",0,33); x+=58;
    tbtn(x,y,118,FAMN[tfam],0,5); x+=122; tbtn(x,y,26,"-",0,6); x+=28; { char t[8],*q=t; q=udec(q,wpf_pt[tsz]); *q=0; fill_round(x,y,34,28,6,15,rgb(255,255,255)); dstr_ctr(x+17,y+7,t,rgb(30,34,46),1); } x+=36; tbtn(x,y,26,"+",0,7); x+=34;
    tbtn(x,y,28,"B",sel_has(1),8); x+=30; tbtn(x,y,28,"I",sel_has(2),9); x+=30; tbtn(x,y,28,"U",sel_has(4),10); x+=30; tbtn(x,y,28,"S",sel_has(8),11); x+=36;
    x=wx+8; y=wy+44; int pal=D[para_end(cur)].al;
    tbtn(x,y,46,"Left",pal==0,12); x+=48; tbtn(x,y,62,"Center",pal==1,13); x+=64; tbtn(x,y,54,"Right",pal==2,14); x+=56; tbtn(x,y,68,"Justify",pal==3,15); x+=74;
    tbtn(x,y,86,"Heading 1",0,30); x+=88; tbtn(x,y,86,"Heading 2",0,31); x+=88; tbtn(x,y,62,"Normal",0,32); x+=70;
    for(int k=0;k<8;k++){ int on=tcol==PAL[k]; fill_round(x,y+4,20,20,4,15,on?ACC:rgb(200,204,214)); fill_round(x+2,y+6,16,16,3,15,PAL[k]); hit(x,y+4,20,20,20+k); x+=23; } x+=8;
    tbtn(x,y,90,"Insert image",0,16);
    pvx=wx; pvy=wy+85; pvw=ww; pvh=wh-85-24; fill_rect(pvx,pvy,pvw,pvh,rgb(228,230,236));
    draw_doc(pvx,pvy,pvw,pvh);
    fill_rect(wx,wy+wh-24,ww,24,rgb(246,247,250)); { int words=0, inw=0; for(int q=0;q<N;q++){ int sp=D[q].ch==' '||D[q].ch=='\n'||D[q].ch=='\t'; if(!sp&&!inw)words++; inw=!sp; }
        char t[160],*q=t; int l=line_of(cur); q=scopy(q,"Page "); q=udec(q,(u32)(l>=0?LN[l].pg+1:1)); q=scopy(q," of "); q=udec(q,(u32)NPG); q=scopy(q,"    "); q=udec(q,(u32)words); q=scopy(q," words    "); q=scopy(q,wp_name); q=scopy(q,"    Ctrl+B/I/U  Ctrl+S save  Ctrl+Z undo"); *q=0; dstr(wx+10,wy+wh-19,t,rgb(90,98,118),1); }
    if(modal){ int mw=420, mh=modal==3?150:360, mxx=wx+(ww-mw)/2, myy=wy+110; shadow_soft(mxx,myy,mw,mh,12); fill_round(mxx,myy,mw,mh,12,15,rgb(255,255,255));
        dstr(mxx+18,myy+16,modal==1?"Open a document":modal==2?"Insert a picture":"Save as",rgb(26,30,42),2);
        if(modal==3){ fill_round(mxx+18,myy+56,mw-36,32,6,15,rgb(240,242,247)); dstr(mxx+28,myy+65,mtext,rgb(26,30,42),1); dstr(mxx+18,myy+100,"Type a name and press Enter (.docx is added). Esc cancels.",rgb(120,128,146),1); }
        else { if(!mn) dstr(mxx+18,myy+60,modal==1?"No .docx or .txt files found in Files.":"No .png or .jpg pictures found in Files.",rgb(120,128,146),1);
            for(int k=0;k<mn&&k<11;k++){ int ry=myy+52+k*26; int hov=mx>=mxx+10&&mx<mxx+mw-10&&my>=ry&&my<ry+24; if(hov)fill_round(mxx+10,ry,mw-20,24,6,15,rgb(236,240,248)); dstr(mxx+20,ry+5,FS[mlist[k]].name,rgb(30,34,46),1); hit(mxx+10,ry,mw-20,24,100+k); }
            dstr(mxx+18,myy+mh-24,"Esc cancels",rgb(150,156,170),1); } } }
static void click_doc(int px,int py,int extend){ layout(); int best=-1, bd=1<<30;
    for(int l=0;l<NL;l++){ int ly=py0+page_y(LN[l].pg)+PGM+LN[l].y; int d=py<ly?ly-py:py>=ly+LN[l].h?py-(ly+LN[l].h-1):0; if(d<bd){ bd=d; best=l; } if(d==0)break; }
    if(best<0)return; cur=pos_at(best,px-px0-PGM); if(!extend)sa=cur; pick_style(); }
void writer_click(int px,int py){
    if(modal){ for(int k=bn-1;k>=0;k--) if(px>=bx_[k]&&px<bx_[k]+bw_[k]&&py>=by_[k]&&py<by_[k]+bh_[k]&&bid_[k]>=100){ int s=mlist[bid_[k]-100];
                if(modal==1){ modal=0; writer_open_slot(s); } else { modal=0; char* d=fsd(s); int ii=add_image((const u8*)d,FS[s].len); if(ii<0)toast("Could not read that picture"); else { snap(3); del_sel(); ins(0xFFFC,(u16)(ii+1)); } } return; }
        return; }
    for(int k=bn-1;k>=0;k--) if(px>=bx_[k]&&px<bx_[k]+bw_[k]&&py>=by_[k]&&py<by_[k]+bh_[k]){ cmd(bid_[k]); return; }
    if(px>=pvx&&px<pvx+pvw-12&&py>=pvy&&py<pvy+pvh) click_doc(px,py,kshift); }
void writer_drag(int px,int py){ if(modal)return; if(px>=pvx&&px<pvx+pvw&&py>=pvy-40&&py<pvy+pvh+40){ if(py<pvy)scrolly-=20; if(py>pvy+pvh)scrolly+=20; click_doc(px,py,1); } }
void writer_wheel(int d){ scrolly-=d*60; if(scrolly<0)scrolly=0; }
static void move(int np,int ext){ if(np<0)np=0; if(np>N-1)np=N-1; cur=np; if(!ext)sa=cur; pick_style(); ensure_visible(); }
void writer_key(int c){
    if(!D)doc_reset();
    if(modal){ if(c==27){ modal=0; return; } if(modal==3){ if(c=='\n'||c=='\r'){ if(mtl){ mtext[mtl]=0; if(!fext_eq(mtext,"docx")&&mtl<58){ scopy(mtext+mtl,".docx"); } int i=0; for(;mtext[i]&&i<63;i++)wp_name[i]=mtext[i]; wp_name[i]=0; modal=0; do_save(); } return; }
            if(c==8||c==127){ if(mtl)mtl--; mtext[mtl]=0; return; } if(c>=32&&c<127&&c!='/'&&mtl<50){ mtext[mtl++]=(char)c; mtext[mtl]=0; } } return; }
    extern int g_key_ctrl, g_key_shift; int ctrl=g_key_ctrl||kdown[0x1D]; int ext=g_key_shift||kshift;
    if(ctrl){ int l=c; if(l>=1&&l<=26)l+='a'-1; if(l>='A'&&l<='Z')l+=32;
        if(l=='b')cmd(8); else if(l=='i')cmd(9); else if(l=='u')cmd(10); else if(l=='s')cmd(3); else if(l=='z')cmd(33); else if(l=='o')cmd(2); else if(l=='n')cmd(1);
        else if(l=='a'){ sa=0; cur=N-1; } else if(l=='e')set_align(1); else if(l=='l')set_align(0); else if(l=='r')set_align(2); else if(l=='j')set_align(3);
        else if(c==17){ int p=cur; while(p>0&&D[p-1].ch==' ')p--; while(p>0&&D[p-1].ch!=' '&&D[p-1].ch!='\n')p--; move(p,ext); } else if(c==18){ int p=cur; while(p<N-1&&D[p].ch!=' '&&D[p].ch!='\n')p++; while(p<N-1&&D[p].ch==' ')p++; move(p,ext); }
        else if(c==2)move(0,ext); else if(c==5)move(N-1,ext);
        return; }
    layout();
    if(c==17){ if(!ext&&sa!=cur){ move(selmin(),0); return; } move(cur-1,ext); }
    else if(c==18){ if(!ext&&sa!=cur){ move(selmax(),0); return; } move(cur+1,ext); }
    else if(c==19||c==20||c==6||c==7){ int l=line_of(cur); int x=x_of(l,cur); int step=c==6||c==7?(pvh/24>1?pvh/24:1):1; int nl=l+((c==19||c==6)?-step:step); if(nl<0)nl=0; if(nl>=NL)nl=NL-1; move(pos_at(nl,x),ext); }
    else if(c==2){ int l=line_of(cur); move(LN[l].s,ext); } else if(c==5){ int l=line_of(cur); int e=LN[l].e-1; if(e<LN[l].s)e=LN[l].s; if(D[e].ch!='\n'&&e<N-1&&LN[l].e<=para_end(LN[l].s)&&D[e].ch==' ')e=e; if(LN[l].e>para_end(LN[l].s))e=para_end(LN[l].s); move(e,ext); }
    else if(c==8){ snap(1); if(sa!=cur)del_sel(); else if(cur>0){ del_range(cur-1,cur); cur--; sa=cur; } ensure_visible(); }
    else if(c==14){ snap(1); if(sa!=cur)del_sel(); else if(cur<N-1)del_range(cur,cur+1); ensure_visible(); }
    else if(c=='\n'||c=='\r'){ snap(1); del_sel(); int al=D[para_end(cur)].al; ins('\n',0); D[cur-1].al=(u8)al; ensure_visible(); }
    else if(c=='\t'||(c>=32&&c<127)){ snap(1); del_sel(); ins((u16)c,0); ensure_visible(); }
}
int writer_blink(void){ return WN[W_WRITER].open&&!WN[W_WRITER].min&&focus==W_WRITER; }
