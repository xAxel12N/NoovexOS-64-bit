#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/drivers/rtc/cmos.h"
#include "include/nvx/fs/fat/exfat.h"

typedef struct { u32 c, ncl; int nofat; u32 pc, pncl; int pnofat, pidx; } xf_dir;
typedef struct { xf_dir in; u32 idx; int cnt, nofat; u32 attr, clus; u64 len, vdl; u8 set[19*32]; } xf_ent;
static struct { nvxblk_rd rd; nvxblk_wr wr; u64 base, nsect, secno; int ro, mounted, unclean, bss, csh, fsd, bad, mv, fixed; u32 avoid, mvdir, mvidx, mvat, r, bs, cb, fatoff, heap, cnt, root, rootncl, bmc, bmsecs, nfree, fsno; u8* bm; u8* bmd; u16* up; u8 sec[4096], fs[4096], z[4096]; } g_xf;

static u16 xf_r16(const u8* p){ return (u16)(p[0]|(p[1]<<8)); }
static u32 xf_r32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static u64 xf_r64(const u8* p){ return (u64)xf_r32(p)|((u64)xf_r32(p+4)<<32); }
static void xf_w16(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void xf_w32(u8* p,u32 v){ xf_w16(p,v); xf_w16(p+2,v>>16); }
static void xf_w64(u8* p,u64 v){ xf_w32(p,(u32)v); xf_w32(p+4,(u32)(v>>32)); }
static void xf_cpy(void* d,const void* s,u64 n){ u8* a=(u8*)d; const u8* b=(const u8*)s; while(n--)*a++=*b++; }
static void xf_zero(void* d,u64 n){ u8* a=(u8*)d; while(n--)*a++=0; }
static int xf_ok(u32 c){ return c>=2&&c<g_xf.cnt+2; }
static u64 xf_c2s(u32 c){ return (u64)g_xf.heap+((u64)(c-2)<<g_xf.csh); }
static u32 xf_ncl(u64 len){ return (u32)((len+g_xf.cb-1)/g_xf.cb); }
static int xf_rs(u64 s,u32 n,void* b){ u64 l=s*g_xf.r, k=(u64)n*g_xf.r; if(!n||l+k>g_xf.nsect)return -1; return g_xf.rd(g_xf.base+l,(int)k,b)?-1:0; }
static int xf_ws(u64 s,u32 n,const void* b){ u64 l=s*g_xf.r, k=(u64)n*g_xf.r; if(g_xf.ro||!g_xf.wr||!n||l+k>g_xf.nsect)return -1; if(g_xf.secno>=s&&g_xf.secno<s+n)g_xf.secno=~0ULL; if(g_xf.wr(g_xf.base+l,(int)k,b)){ g_xf.bad=1; return -1; } return 0; }
static int xf_fload(u32 s){ if(g_xf.fsno==s)return 0; if(g_xf.fsd){ if(xf_ws(g_xf.fatoff+g_xf.fsno,1,g_xf.fs))return -1; g_xf.fsd=0; } if(xf_rs(g_xf.fatoff+s,1,g_xf.fs)){ g_xf.fsno=~0u; return -1; } g_xf.fsno=s; return 0; }
static u32 xf_fget(u32 c){ u64 o=(u64)c*4; if(xf_fload((u32)(o>>g_xf.bss)))return 0xFFFFFFF7u; return xf_r32(g_xf.fs+(o&(g_xf.bs-1))); }
static int xf_fset(u32 c,u32 v){ u64 o=(u64)c*4; if(xf_fload((u32)(o>>g_xf.bss)))return -1; xf_w32(g_xf.fs+(o&(g_xf.bs-1)),v); g_xf.fsd=1; return 0; }
static u32 xf_at(u32 c,int nofat,u32 k){ if(nofat)return (u64)c+k<(u64)g_xf.cnt+2?c+k:0; while(k--&&xf_ok(c))c=xf_fget(c); return xf_ok(c)?c:0; }
static int xf_bget(u32 c){ u32 b=c-2; return (g_xf.bm[b>>3]>>(b&7))&1; }
static void xf_bset(u32 c,int v){ u32 b=c-2; if(v)g_xf.bm[b>>3]|=(u8)(1u<<(b&7)); else g_xf.bm[b>>3]&=(u8)~(1u<<(b&7)); g_xf.bmd[(b>>3)>>g_xf.bss]=1; }
static int xf_rbytes(u64 s,u8* p,u64 k){
    while(k>=g_xf.bs){ u32 n=(u32)(k>>g_xf.bss); if(n>128)n=128; if(xf_rs(s,n,p))return -1; s+=n; p+=(u64)n<<g_xf.bss; k-=(u64)n<<g_xf.bss; }
    if(k){ if(xf_rs(s,1,g_xf.sec)){ g_xf.secno=~0ULL; return -1; } g_xf.secno=s; xf_cpy(p,g_xf.sec,k); } return 0; }
static int xf_wbytes(u64 s,const u8* p,u64 k){
    while(k>=g_xf.bs){ u32 n=(u32)(k>>g_xf.bss); if(n>128)n=128; if(xf_ws(s,n,p))return -1; s+=n; p+=(u64)n<<g_xf.bss; k-=(u64)n<<g_xf.bss; }
    if(k){ g_xf.secno=~0ULL; xf_zero(g_xf.sec,g_xf.bs); xf_cpy(g_xf.sec,p,k); if(xf_ws(s,1,g_xf.sec))return -1; g_xf.secno=s; } return 0; }
static int xf_chain_io(u32 c,int nofat,u64 len,u8* buf,int wr){
    u64 done=0; while(done<len){ if(!xf_ok(c))return -1; u64 k=len-done; if(k>g_xf.cb)k=g_xf.cb;
        if(wr?xf_wbytes(xf_c2s(c),buf+done,k):xf_rbytes(xf_c2s(c),buf+done,k))return -1; done+=k; c=nofat?c+1:xf_fget(c); }
    return 0; }
static int xf_zclus(u32 c){ u64 s=xf_c2s(c); for(u32 i=0;i<(1u<<g_xf.csh);i++) if(xf_ws(s+i,1,g_xf.z))return -1; return 0; }
static int xf_sync(void){
    if(g_xf.ro||!g_xf.mounted)return 0; int r=0;
    if(g_xf.fsd){ if(xf_ws(g_xf.fatoff+g_xf.fsno,1,g_xf.fs))r=-1; else g_xf.fsd=0; }
    for(u32 j=0;j<g_xf.bmsecs;j++) if(g_xf.bmd[j]){ u32 c=xf_at(g_xf.bmc,0,j>>g_xf.csh); if(!c||xf_ws(xf_c2s(c)+(j&((1u<<g_xf.csh)-1)),1,g_xf.bm+((u64)j<<g_xf.bss))){ r=-1; continue; } g_xf.bmd[j]=0; }
    return r; }
static int xf_eloc(const xf_dir* d,u32 i,u64* sn,u32* o){
    u64 off=(u64)i*32; u32 k=(u32)(off>>(g_xf.bss+g_xf.csh)); if(k>=d->ncl)return 1; u32 c=xf_at(d->c,d->nofat,k); if(!c)return -1;
    *sn=xf_c2s(c)+((off>>g_xf.bss)&((1u<<g_xf.csh)-1)); *o=(u32)(off&(g_xf.bs-1)); return 0; }
static int xf_eload(u64 s){ if(g_xf.secno!=s){ if(xf_rs(s,1,g_xf.sec)){ g_xf.secno=~0ULL; return -1; } g_xf.secno=s; } return 0; }
static int xf_eflush(u64 s){ if(xf_ws(s,1,g_xf.sec)){ g_xf.secno=~0ULL; return -1; } g_xf.secno=s; return 0; }
static int xf_eio(const xf_dir* d,u32 i,u8* e,int wr){
    u64 s; u32 o; int r=xf_eloc(d,i,&s,&o); if(r)return r; if(xf_eload(s))return -1;
    if(!wr){ xf_cpy(e,g_xf.sec+o,32); return 0; }
    xf_cpy(g_xf.sec+o,e,32); return xf_eflush(s); }
static int xf_eput(const xf_dir* d,u32 at,const u8* s,int cnt,int down){
    u64 ps=~0ULL; int dirty=0;
    for(int j=0;j<cnt;j++){ int k=down?cnt-1-j:j; u64 sn; u32 o; if(xf_eloc(d,at+(u32)k,&sn,&o))return -1;
        if(sn!=ps){ if(dirty&&xf_eflush(ps))return -1; dirty=0; if(xf_eload(sn))return -1; ps=sn; }
        const u8* q=s+k*32; int same=1; for(int b=0;b<32;b++) if(g_xf.sec[o+b]!=q[b]){ same=0; break; }
        if(!same){ xf_cpy(g_xf.sec+o,q,32); dirty=1; } }
    return dirty?xf_eflush(ps):0; }
static int xf_1sec(u32 at,int cnt){ return ((u64)at*32>>g_xf.bss)==((u64)(at+(u32)cnt-1)*32>>g_xf.bss); }
static u16 xf_up(u16 c){ if(g_xf.up)return g_xf.up[c]; return (u16)(c>='a'&&c<='z'?c-32:c); }
static u16 xf_hash(const u16* u,int n){ u16 h=0; for(int i=0;i<n;i++){ u16 c=xf_up(u[i]); h=(u16)(((h&1)?0x8000:0)+(h>>1)+(c&0xFF)); h=(u16)(((h&1)?0x8000:0)+(h>>1)+(c>>8)); } return h; }
static u16 xf_csum(const u8* s,int n){ u16 h=0; for(int i=0;i<n;i++){ if(i==2||i==3)continue; h=(u16)(((h&1)?0x8000:0)+(h>>1)+s[i]); } return h; }
static int xf_u16(const char* s,u16* o){
    const u8* u=(const u8*)s; int n=0;
    while(*u){ u32 c; if(*u<0x80)c=*u++; else if((*u&0xE0)==0xC0&&u[1]){ c=((u32)(u[0]&0x1F)<<6)|(u[1]&0x3F); u+=2; }
        else if((*u&0xF0)==0xE0&&u[1]&&u[2]){ c=((u32)(u[0]&0x0F)<<12)|((u32)(u[1]&0x3F)<<6)|(u[2]&0x3F); u+=3; }
        else if((*u&0xF8)==0xF0&&u[1]&&u[2]&&u[3]){ c=((u32)(u[0]&0x07)<<18)|((u32)(u[1]&0x3F)<<12)|((u32)(u[2]&0x3F)<<6)|(u[3]&0x3F); u+=4; }
        else return -1;
        if(c<0x20||(c>=0xD800&&c<0xE000)||c>0x10FFFF||c=='"'||c=='*'||c=='/'||c==':'||c=='<'||c=='>'||c=='?'||c=='\\'||c=='|')return -1;
        if(c>=0x10000){ if(n+2>255)return -1; c-=0x10000; o[n++]=(u16)(0xD800|(c>>10)); o[n++]=(u16)(0xDC00|(c&0x3FF)); } else { if(n+1>255)return -1; o[n++]=(u16)c; } }
    if(n==1&&o[0]=='.')return -1; if(n==2&&o[0]=='.'&&o[1]=='.')return -1; return n; }
static void xf_u8(const u16* u,int n,char* o,int cap){
    int k=0; for(int i=0;i<n;i++){ u32 c=u[i]; if(c>=0xD800&&c<0xDC00&&i+1<n&&u[i+1]>=0xDC00&&u[i+1]<0xE000){ c=0x10000+((c-0xD800)<<10)+(u[i+1]-0xDC00); i++; }
        if(k+4>=cap)break; if(c<0x80)o[k++]=(char)c; else if(c<0x800){ o[k++]=(char)(0xC0|(c>>6)); o[k++]=(char)(0x80|(c&0x3F)); }
        else if(c<0x10000){ o[k++]=(char)(0xE0|(c>>12)); o[k++]=(char)(0x80|((c>>6)&0x3F)); o[k++]=(char)(0x80|(c&0x3F)); }
        else { o[k++]=(char)(0xF0|(c>>18)); o[k++]=(char)(0x80|((c>>12)&0x3F)); o[k++]=(char)(0x80|((c>>6)&0x3F)); o[k++]=(char)(0x80|(c&0x3F)); } }
    o[k]=0; }
static int xf_set(const xf_dir* d,u32* i,xf_ent* e,u16* nm,int* nl){
    u8 b[32];
    for(;;){ int r=xf_eio(d,*i,b,0); if(r>0)return 0; if(r<0)return -1; if(!b[0])return 0; u32 me=(*i)++;
        if(b[0]!=0x85||b[1]<2||b[1]>18)continue; int sc=b[1]; xf_cpy(e->set,b,32); int bad=0;
        for(int k=1;k<=sc&&!bad;k++) if(xf_eio(d,me+(u32)k,e->set+k*32,0))bad=1;
        if(bad||e->set[32]!=0xC0||xf_csum(e->set,(sc+1)*32)!=xf_r16(e->set+2))continue;
        e->in=*d; e->idx=me; e->cnt=sc+1; e->attr=xf_r16(e->set+4); e->nofat=(e->set[33]>>1)&1; e->vdl=xf_r64(e->set+40); e->clus=xf_r32(e->set+52); e->len=xf_r64(e->set+56);
        int want=e->set[35], n=0; for(int k=2;k<=sc&&n<want;k++){ const u8* q=e->set+k*32; if(q[0]!=0xC1)break; for(int j=0;j<15&&n<want;j++)nm[n++]=xf_r16(q+2+2*j); }
        *nl=n; *i=me+(u32)sc+1; return 1; } }
static int xf_eq(const u16* a,int an,const u16* b,int bn){ if(an!=bn)return 0; for(int i=0;i<an;i++) if(xf_up(a[i])!=xf_up(b[i]))return 0; return 1; }
static int xf_find(const xf_dir* d,const u16* nm,int nl,xf_ent* e){ u32 i=0; u16 t[256]; int tl; for(;;){ int r=xf_set(d,&i,e,t,&tl); if(r<=0)return -1; if(xf_eq(t,tl,nm,nl))return 0; } }
static void xf_rootd(xf_dir* d){ d->c=g_xf.root; d->ncl=g_xf.rootncl; d->nofat=0; d->pidx=-1; d->pc=0; d->pncl=0; d->pnofat=0; }
static void xf_sub(const xf_ent* e,xf_dir* d){ d->c=e->clus; d->ncl=xf_ncl(e->len); d->nofat=e->nofat; d->pc=e->in.c; d->pncl=e->in.ncl; d->pnofat=e->in.nofat; d->pidx=(int)e->idx; }
static int xf_walk(const char* path,xf_dir* par,xf_ent* e,char* leaf){
    xf_dir d; xf_rootd(&d); const char* p=path; while(*p=='/')p++; if(!*p){ if(par)*par=d; return 1; }
    for(;;){ char comp[256]; int n=0; while(*p&&*p!='/'){ if(n>=255)return -2; comp[n++]=*p++; } comp[n]=0; while(*p=='/')p++;
        u16 nm[256]; int nl=xf_u16(comp,nm); if(par)*par=d; if(leaf){ for(int i=0;i<=n;i++)leaf[i]=comp[i]; }
        if(nl<1)return -2; if(xf_find(&d,nm,nl,e))return *p?-2:-1; if(!*p)return 0; if(!(e->attr&0x10))return -2; xf_sub(e,&d); if(!xf_ok(d.c)||(g_xf.avoid&&d.c==g_xf.avoid))return -2; } }
static void xf_now(u8* p){ int hh=12,mm=0,ss=0,yr=2026,mo=1,dy=1; read_clock(&hh,&mm,&ss,&yr,&mo,&dy); if(yr<1980)yr=1980; if(yr>2107)yr=2107;
    u32 t=((u32)(yr-1980)<<25)|((u32)mo<<21)|((u32)dy<<16)|((u32)hh<<11)|((u32)mm<<5)|((u32)ss/2); xf_w32(p,t); }
static int xf_wset(const xf_dir* d,u32 at,u8* s,int cnt){ xf_w16(s+2,xf_csum(s,cnt*32)); return xf_eput(d,at,s,cnt,1); }
static u32 xf_free1(u32 from){ for(u32 i=0;i<g_xf.cnt;i++){ u32 c=2+(from-2+i)%g_xf.cnt; if(!xf_bget(c))return c; } return 0; }
static int xf_grow(xf_dir* d){
    if(!g_xf.nfree||!d->ncl)return -1; u32 last=xf_at(d->c,d->nofat,d->ncl-1); if(!last)return -1; u32 nc;
    if(d->nofat&&xf_ok(last+1)&&!xf_bget(last+1))nc=last+1;
    else { nc=xf_free1(last); if(!nc)return -1; if(d->nofat){ for(u32 k=0;k+1<d->ncl;k++) if(xf_fset(d->c+k,d->c+k+1))return -1; d->nofat=0; } if(xf_fset(last,nc)||xf_fset(nc,0xFFFFFFFFu))return -1; }
    xf_bset(nc,1); g_xf.nfree--; if(xf_zclus(nc))return -1; d->ncl++;
    if(d->pidx<0){ g_xf.rootncl=d->ncl; return xf_sync(); }
    xf_dir p; p.c=d->pc; p.ncl=d->pncl; p.nofat=d->pnofat; p.pidx=-1; p.pc=0; p.pncl=0; p.pnofat=0;
    u8 s[19*32]; if(xf_eio(&p,(u32)d->pidx,s,0)||s[0]!=0x85||s[1]<2||s[1]>18)return -1; int cnt=s[1]+1;
    for(int k=1;k<cnt;k++) if(xf_eio(&p,(u32)d->pidx+(u32)k,s+k*32,0))return -1;
    s[33]=(u8)((s[33]&~2)|(d->nofat?2:0)); xf_w64(s+40,(u64)d->ncl*g_xf.cb); xf_w64(s+56,(u64)d->ncl*g_xf.cb);
    if(xf_sync())return -1; return xf_wset(&p,(u32)d->pidx,s,cnt); }
static int xf_build(u8* s,const char* name,u32 attr,u32 clus,int nofat,u64 len,u64 vdl,const u8* times){
    u16 nm[256]; int nl=xf_u16(name,nm); if(nl<1)return -1; int cnt=2+(nl+14)/15;
    xf_zero(s,19*32); s[0]=0x85; s[1]=(u8)(cnt-1); xf_w16(s+4,attr);
    if(times)xf_cpy(s+8,times,17); else { xf_now(s+8); xf_cpy(s+12,s+8,4); xf_cpy(s+16,s+8,4); }
    s[32]=0xC0; s[33]=(u8)(1|(nofat?2:0)); s[35]=(u8)nl; xf_w16(s+36,xf_hash(nm,nl)); xf_w64(s+40,vdl); xf_w32(s+52,clus); xf_w64(s+56,len);
    if(g_xf.mv){ s[33]|=0x80; xf_w32(s+48,g_xf.mvdir); xf_w32(s+25,g_xf.mvidx); }
    for(int k=0;k<cnt-2;k++){ u8* q=s+(2+k)*32; q[0]=0xC1; for(int j=0;j<15;j++){ int ci=k*15+j; xf_w16(q+2+2*j,ci<nl?nm[ci]:0); } }
    return cnt; }
static int xf_mkset(xf_dir* d,const char* name,u32 attr,u32 clus,int nofat,u64 len,u64 vdl,const u8* times){
    u8 s[19*32]; int cnt=xf_build(s,name,attr,clus,nofat,len,vdl,times); if(cnt<0)return -1;
    u32 i=0, st=0, eps=g_xf.bs/32, zf=~0u; int run=0, al=(u32)cnt<=eps; u8 b[32];
    for(int g=0;run<cnt;g++){ if(g>0x200000)return -1; int r=xf_eio(d,i,b,0); if(r<0)return -1; if(r>0){ if(xf_grow(d))return -1; continue; }
        if(!b[0]&&zf==~0u)zf=i;
        if(b[0]<0x80){ if(!run||(al&&i/eps!=st/eps)){ st=i; run=0; } run++; } else run=0; i++; }
    if(zf<st){ static u8 f[128*32]; u32 n=st-zf; if(n>128)return -1; xf_zero(f,(u64)n*32); for(u32 k=0;k<n;k++)f[k*32]=0x05; if(xf_eput(d,zf,f,(int)n,0))return -1; }
    g_xf.mvat=st; return xf_wset(d,st,s,cnt); }
static int xf_kill(const xf_ent* e){ u8 s[19*32]; for(int k=0;k<e->cnt;k++){ if(xf_eio(&e->in,e->idx+(u32)k,s+k*32,0))return -1; s[k*32]&=0x7F; } return xf_eput(&e->in,e->idx,s,e->cnt,0); }
static int xf_unmv(const xf_dir* d,u32 at){
    u8 s[19*32]; if(xf_eio(d,at,s,0)||s[0]!=0x85||s[1]<2||s[1]>18)return -1; int cnt=s[1]+1; for(int k=1;k<cnt;k++) if(xf_eio(d,at+(u32)k,s+k*32,0))return -1;
    if(s[32]!=0xC0)return -1; s[33]&=0x7F; xf_zero(s+48,4); xf_zero(s+25,4); return xf_wset(d,at,s,cnt); }
static void xf_release(u32 c,int nofat,u64 len){ u32 n=xf_ncl(len); for(u32 i=0;i<n&&xf_ok(c);i++){ u32 nx=nofat?c+1:xf_fget(c); if(xf_bget(c)){ xf_bset(c,0); g_xf.nfree++; } c=nx; } }
static int xf_alloc(u32 n,u32* first,int* nofat){
    *first=0; *nofat=0; if(!n)return 0; if(n>g_xf.nfree)return -1; u32 run=0, st=0;
    for(u32 i=0;i<g_xf.cnt;i++){ u32 c=2+i; if(!run&&!(i&7)&&g_xf.bm[i>>3]==0xFF&&i+8<=g_xf.cnt){ i+=7; continue; } if(!xf_bget(c)){ if(!run)st=c; if(++run==n)break; } else run=0; }
    if(run==n){ for(u32 k=0;k<n;k++)xf_bset(st+k,1); g_xf.nfree-=n; *first=st; *nofat=1; return 0; }
    u32 prev=0, got=0; for(u32 c=2;got<n&&c<g_xf.cnt+2;c++) if(!xf_bget(c)){ if(prev&&xf_fset(prev,c))break; xf_bset(c,1); g_xf.nfree--; if(!prev)*first=c; prev=c; got++; }
    if(got==n&&!xf_fset(prev,0xFFFFFFFFu))return 0;
    for(u32 c=*first,k=0;k<got&&xf_ok(c);k++){ u32 nx=k+1<got?xf_fget(c):0; xf_bset(c,0); g_xf.nfree++; c=nx; } *first=0; return -1; }
static void xf_pin(u32 c,int nofat,u64 len){ u32 n=xf_ncl(len); for(u32 i=0;i<n&&xf_ok(c);i++){ if(!xf_bget(c)){ xf_bset(c,1); g_xf.nfree--; } c=nofat?c+1:xf_fget(c); } }
static int xf_allocd(u32 c,int nofat,u64 len){ u32 n=xf_ncl(len); for(u32 i=0;i<n;i++){ if(!xf_ok(c)||!xf_bget(c))return 0; c=nofat?c+1:xf_fget(c); } return 1; }
typedef struct { xf_dir in; u32 idx, clus, attr, sd, si; u64 len; u8 cnt, nofat, mv, walk, gone; } xf_fe;
static int xf_rmark(u8* ref,u32 c,int nofat,u64 len){ u32 n=xf_ncl(len); for(u32 i=0;i<n;i++){ if(!xf_ok(c))return -1; u32 b=c-2; if(ref[b>>3]&(1u<<(b&7)))return -1; ref[b>>3]|=(u8)(1u<<(b&7)); c=nofat?c+1:xf_fget(c); } return 0; }
static int xf_fix(u32 upc,u64 upl,u64 bml){
    u32 cap=32768, n=0, q=0; u64 rb=(u64)g_xf.bmsecs<<g_xf.bss, steps=0; xf_fe* L=(xf_fe*)kmalloc((u64)cap*sizeof(xf_fe)); u8* ref=(u8*)kmalloc(rb); int bad=0, fx=0;
    if(!L||!ref){ if(L)kfree(L); if(ref)kfree(ref); return -1; } xf_zero(ref,rb);
    xf_dir d; xf_rootd(&d); u32 rb0=g_xf.root-2; ref[rb0>>3]|=(u8)(1u<<(rb0&7));
    for(int top=1;!bad;top=0){
        if(!top){ while(q<n&&!L[q].walk)q++; if(q>=n)break; d.c=L[q].clus; d.ncl=xf_ncl(L[q].len); d.nofat=L[q].nofat; d.pidx=-1; d.pc=0; d.pncl=0; d.pnofat=0; q++; }
        u8 s[19*32];
        for(u32 i=0;!bad;){ if(++steps>(8u<<20)){ bad=1; break; } int r=xf_eio(&d,i,s,0); if(r>0)break; if(r<0){ bad=1; break; } if(!s[0])break;
            if(s[0]!=0x85){ i++; continue; } int sc=s[1]; if(sc<2||sc>18){ bad=1; break; }
            for(int k=1;k<=sc&&!bad;k++) if(xf_eio(&d,i+(u32)k,s+k*32,0))bad=1;
            if(bad||s[32]!=0xC0||xf_csum(s,(sc+1)*32)!=xf_r16(s+2)||n>=cap){ bad=1; break; }
            xf_fe* f=&L[n++]; f->in=d; f->idx=i; f->cnt=(u8)(sc+1); f->attr=xf_r16(s+4); f->nofat=(s[33]>>1)&1; f->clus=xf_r32(s+52); f->len=xf_r64(s+56);
            f->sd=xf_r32(s+48); f->si=xf_r32(s+25); f->mv=(s[33]&0x80)&&f->sd; f->walk=0; f->gone=0;
            if((f->attr&0x10)&&xf_ok(f->clus)){ u32 b=f->clus-2; if(!(ref[b>>3]&(1u<<(b&7)))){ ref[b>>3]|=(u8)(1u<<(b&7)); f->walk=1; } }
            i+=(u32)sc+1; } }
    for(u32 k=0;k<n&&!bad;k++){ if(!L[k].mv)continue; fx++;
        for(u32 m=0;m<n;m++){ xf_fe* o=&L[m]; if(m==k||o->mv||o->gone||o->in.c!=L[k].sd||o->idx!=L[k].si||o->clus!=L[k].clus||o->len!=L[k].len||o->attr!=L[k].attr)continue; o->gone=1; fx++; break; } }
    if(!bad){ xf_zero(ref,rb); bad=xf_rmark(ref,g_xf.bmc,0,bml)||xf_rmark(ref,upc,0,upl)||xf_rmark(ref,g_xf.root,0,(u64)g_xf.rootncl*g_xf.cb);
        for(u32 k=0;k<n&&!bad;k++) if(!L[k].gone&&(L[k].clus||L[k].len)&&xf_rmark(ref,L[k].clus,L[k].nofat,L[k].len))bad=1; }
    if(!bad){ for(u32 k=0;k<n&&!bad;k++) if(L[k].gone){ xf_ent x; x.in=L[k].in; x.idx=L[k].idx; x.cnt=L[k].cnt; if(xf_kill(&x))bad=1; }
        for(u32 k=0;k<n&&!bad;k++) if(L[k].mv&&xf_unmv(&L[k].in,L[k].idx))bad=1;
        for(u32 c=2;c<g_xf.cnt+2&&!bad;c++){ u32 b=c-2; int u=(ref[b>>3]>>(b&7))&1; if(u!=xf_bget(c)){ xf_bset(c,u); if(u)g_xf.nfree--; else g_xf.nfree++; fx++; } }
        if(!bad&&xf_sync())bad=1; }
    kfree(L); kfree(ref); if(bad)return -1; g_xf.fixed=fx; return 0; }
int xf_repaired(void){ return g_xf.mounted?g_xf.fixed:0; }
int xf_mounted(void){ return g_xf.mounted; }
int xf_ro(void){ return g_xf.ro; }
int xf_unclean(void){ return g_xf.mounted&&g_xf.unclean; }
unsigned long long xf_free_bytes(void){ return g_xf.mounted?(u64)g_xf.nfree*g_xf.cb:0; }
static int xf_flags(int dirty){
    u8* b=g_xf.fs; if(xf_sync())return -1; if(!dirty&&g_xf.bad)return -1; g_xf.fsno=~0u; if(xf_rs(0,1,b))return -1; u16 f=xf_r16(b+106); f=(u16)(dirty?(f|2):(f&~2)); xf_w16(b+106,f);
    if(g_xf.cnt)b[112]=(u8)(((u64)(g_xf.cnt-g_xf.nfree)*100)/g_xf.cnt); return xf_ws(0,1,b); }
static void xf_drop(void){ if(g_xf.bm)kfree(g_xf.bm); if(g_xf.bmd)kfree(g_xf.bmd); if(g_xf.up)kfree(g_xf.up); g_xf.bm=0; g_xf.bmd=0; g_xf.up=0; g_xf.mounted=0; g_xf.secno=~0ULL; g_xf.fsno=~0u; g_xf.fsd=0; }
int xf_umount(void){ if(!g_xf.mounted)return 0; int r=xf_sync(); xf_drop(); return r; }
int xf_mark(int dirty){ if(!g_xf.mounted||g_xf.ro)return -1; return xf_flags(dirty); }
int xf_mount(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,int ro){
    xf_umount(); xf_zero(&g_xf,sizeof g_xf); g_xf.rd=rd; g_xf.wr=ro?0:wr; g_xf.base=base; g_xf.nsect=nsect; g_xf.ro=ro||!wr; g_xf.secno=~0ULL; g_xf.fsno=~0u; g_xf.r=1; g_xf.bs=512; g_xf.bss=9;
    u8* b=g_xf.fs; if(rd(base,1,b))return -1;
    if(b[3]!='E'||b[4]!='X'||b[5]!='F'||b[6]!='A'||b[7]!='T'||b[510]!=0x55||b[511]!=0xAA)return -1;
    if(b[108]<9||b[108]>12||b[109]>25-b[108]||b[105]!=1||(b[110]!=1&&b[110]!=2))return -1;
    g_xf.bss=b[108]; g_xf.bs=1u<<g_xf.bss; g_xf.r=g_xf.bs/512; g_xf.csh=b[109]; g_xf.cb=g_xf.bs<<g_xf.csh; g_xf.fatoff=xf_r32(b+80); g_xf.heap=xf_r32(b+88); g_xf.cnt=xf_r32(b+92); g_xf.root=xf_r32(b+96);
    u32 fatlen=xf_r32(b+84); if(xf_r64(b+72)>nsect||(u64)g_xf.fatoff+fatlen>xf_r64(b+72)||!g_xf.cnt||g_xf.cnt>0xFFFFFFF5u||(u64)fatlen*g_xf.bs<((u64)g_xf.cnt+2)*4||g_xf.fatoff<24||(u64)g_xf.heap<(u64)g_xf.fatoff+fatlen||!xf_ok(g_xf.root))return -1;
    if(((u64)g_xf.heap+((u64)g_xf.cnt<<g_xf.csh))*g_xf.r>nsect)return -1;
    { u32 sum=0; for(u32 s=0;s<12;s++){ if(xf_rs(s,1,g_xf.sec))return -1; if(s<11){ for(u32 i=0;i<g_xf.bs;i++){ if(!s&&(i==106||i==107||i==112))continue; sum=((sum&1)?0x80000000u:0)+(sum>>1)+g_xf.sec[i]; } } else if(xf_r32(g_xf.sec)!=sum)return -2; } g_xf.secno=~0ULL; }
    if(xf_rs(0,1,b))return -1; u16 vf=xf_r16(b+106); g_xf.unclean=(vf&6)?1:0; int fixok=b[110]==1&&(vf&6)==2&&!ro&&wr; if(b[110]!=1||g_xf.unclean)g_xf.ro=1, g_xf.wr=0;
    { u32 c=g_xf.root, n=0; while(xf_ok(c)&&n<=g_xf.cnt){ n++; c=xf_fget(c); } if(n>g_xf.cnt)return -3; g_xf.rootncl=n; }
    xf_dir rd0; xf_rootd(&rd0); u8 e[32]; u64 bml=0, upl=0; u32 upc=0, upck=0;
    for(u32 i=0;;i++){ int r=xf_eio(&rd0,i,e,0); if(r<0)return -1; if(r>0||!e[0])break; if(e[0]==0x81&&!(e[1]&1)){ g_xf.bmc=xf_r32(e+20); bml=xf_r64(e+24); } else if(e[0]==0x82){ upck=xf_r32(e+4); upc=xf_r32(e+20); upl=xf_r64(e+24); } }
    if(!xf_ok(g_xf.bmc)||bml<((u64)g_xf.cnt+7)/8||bml>(256ull<<20))return -3;
    g_xf.bmsecs=(u32)((((u64)g_xf.cnt+7)/8+g_xf.bs-1)>>g_xf.bss); g_xf.bm=(u8*)kmalloc((u64)g_xf.bmsecs<<g_xf.bss); g_xf.bmd=(u8*)kmalloc(g_xf.bmsecs);
    if(!g_xf.bm||!g_xf.bmd){ xf_drop(); return -4; } xf_zero(g_xf.bmd,g_xf.bmsecs);
    for(u32 j=0;j<g_xf.bmsecs;j++){ u32 c=xf_at(g_xf.bmc,0,j>>g_xf.csh); if(!c||xf_rs(xf_c2s(c)+(j&((1u<<g_xf.csh)-1)),1,g_xf.bm+((u64)j<<g_xf.bss))){ xf_drop(); return -1; } }
    g_xf.nfree=0; for(u32 c=2;c<g_xf.cnt+2;c++) if(!xf_bget(c))g_xf.nfree++;
    if(xf_ok(upc)&&upl>=2&&upl<=(256u<<10)){ u8* t=(u8*)kmalloc(upl+g_xf.bs); g_xf.up=(u16*)kmalloc(65536*2);
        u32 ck=0; if(t&&g_xf.up&&!xf_chain_io(upc,0,upl,t,0)){ for(u64 q=0;q<upl;q++)ck=((ck&1)?0x80000000u:0)+(ck>>1)+t[q]; }
        if(t&&g_xf.up&&ck==upck&&ck){ for(u32 k=0;k<65536;k++)g_xf.up[k]=(u16)k; u32 at=0; for(u64 q=0;q+1<upl&&at<65536;q+=2){ u16 v=xf_r16(t+q); if(v==0xFFFF&&q+3<upl){ at+=xf_r16(t+q+2); q+=2; } else g_xf.up[at++]=v; } }
        else if(g_xf.up){ kfree(g_xf.up); g_xf.up=0; } if(t)kfree(t); }
    if(!g_xf.up||!xf_allocd(g_xf.bmc,0,bml)||!xf_allocd(upc,0,upl)||!xf_allocd(g_xf.root,0,(u64)g_xf.rootncl*g_xf.cb)){ g_xf.ro=1; g_xf.wr=0; fixok=0; }
    g_xf.mounted=1;
    if(fixok){ g_xf.ro=0; g_xf.wr=wr; if(xf_fix(upc,upl,bml)||xf_flags(0)){ g_xf.ro=1; g_xf.wr=0; g_xf.fixed=0; } else g_xf.unclean=0; }
    return 0; }
int xf_ls(const char* path,nvxntfs_lscb cb,void* ctx){
    if(!g_xf.mounted||!cb)return -1; xf_dir d; xf_ent e; int r=xf_walk(path,&d,&e,0); if(r==0){ if(!(e.attr&0x10))return -1; xf_sub(&e,&d); } else if(r!=1)return -1;
    u32 i=0; u16 nm[256]; int nl; char o[1024]; for(;;){ int q=xf_set(&d,&i,&e,nm,&nl); if(q<0)return -1; if(!q)break; xf_u8(nm,nl,o,sizeof o); cb(o,(e.attr&0x10)?1:0,(e.attr&0x10)?0:e.len,ctx); }
    return 0; }
long xf_size(const char* path){ xf_ent e; if(!g_xf.mounted||xf_walk(path,0,&e,0)!=0||(e.attr&0x10))return -1; return (long)e.len; }
long xf_read(const char* path,char* buf,long cap){
    xf_ent e; if(!g_xf.mounted||!buf||cap<0||xf_walk(path,0,&e,0)!=0||(e.attr&0x10))return -1;
    u64 n=e.len<(u64)cap?e.len:(u64)cap, v=e.vdl<n?e.vdl:n; if(v&&xf_chain_io(e.clus,e.nofat,v,(u8*)buf,0))return -1; xf_zero(buf+v,n-v); return (long)n; }
long xf_write(const char* path,const char* buf,long len){
    if(!g_xf.mounted||g_xf.ro||len<0||(len&&!buf))return -1; xf_dir p; xf_ent e; char leaf[256]; int r=xf_walk(path,&p,&e,leaf);
    if(r==1||r==-2||(r==0&&(e.attr&0x10)))return -1;
    if(r==0)xf_pin(e.clus,e.nofat,e.len);
    u32 n=xf_ncl((u64)len), c; int nf; if(xf_alloc(n,&c,&nf)){ xf_sync(); return -1; }
    if(n&&xf_chain_io(c,nf,(u64)len,(u8*)buf,1)){ xf_release(c,nf,(u64)len); xf_sync(); return -1; }
    if(xf_sync())return -1;
    if(r==0){ u8* s=e.set; s[33]=(u8)(1|(nf?2:0)); xf_w64(s+40,(u64)len); xf_w32(s+52,c); xf_w64(s+56,(u64)len); xf_now(s+12); xf_now(s+16);
        if(xf_wset(&e.in,e.idx,s,e.cnt)){ xf_sync(); return -1; } xf_release(e.clus,e.nofat,e.len); }
    else if(xf_mkset(&p,leaf,0x20,c,nf,(u64)len,(u64)len,0)){ xf_release(c,nf,(u64)len); xf_sync(); return -1; }
    return xf_sync()?-1:len; }
int xf_mkdir(const char* path){
    if(!g_xf.mounted||g_xf.ro)return -1; xf_dir p; xf_ent e; char leaf[256]; if(xf_walk(path,&p,&e,leaf)!=-1)return -1;
    u32 c; int nf; if(xf_alloc(1,&c,&nf))return -1; if(xf_zclus(c)||xf_sync()||xf_mkset(&p,leaf,0x10,c,nf,g_xf.cb,g_xf.cb,0)){ xf_release(c,nf,g_xf.cb); xf_sync(); return -1; }
    return xf_sync(); }
int xf_delete(const char* path){
    if(!g_xf.mounted||g_xf.ro)return -1; xf_dir p; xf_ent e; if(xf_walk(path,&p,&e,0)!=0)return -1;
    if(e.attr&0x10){ xf_dir s; xf_sub(&e,&s); u32 i=0; xf_ent x; u16 nm[256]; int nl; int q=xf_set(&s,&i,&x,nm,&nl); if(q)return -1; }
    if(xf_kill(&e)){ xf_sync(); return -1; } xf_release(e.clus,e.nofat,e.len); return xf_sync(); }
static int xf_under(const char* a,const char* b){ while(*a=='/')a++; while(*b=='/')b++; while(*a&&*b){ char x=*a,y=*b; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y)return 0; a++; b++; } return !*a&&(*b=='/'||!*b); }
int xf_rename(const char* from,const char* to){
    if(!g_xf.mounted||g_xf.ro||!from||!to)return -1; xf_dir p1,p2; xf_ent e,x; char l2[256];
    if(xf_walk(from,&p1,&e,0)!=0)return -1; g_xf.avoid=(e.attr&0x10)?e.clus:0; int w=xf_walk(to,&p2,&x,l2); g_xf.avoid=0;
    if(w!=-1||!l2[0]||((e.attr&0x10)&&(xf_under(from,to)||p2.c==e.clus)))return -1;
    u8 t[17]; xf_cpy(t,e.set+8,17);
    if(p1.c==p2.c&&xf_1sec(e.idx,e.cnt)){ u8 ns[19*32]; int nc=xf_build(ns,l2,e.attr,e.clus,e.nofat,e.len,e.vdl,t); if(nc==e.cnt){ if(xf_wset(&e.in,e.idx,ns,nc)){ xf_sync(); return -1; } return xf_sync(); } }
    g_xf.mv=1; g_xf.mvdir=e.in.c; g_xf.mvidx=e.idx; int r=xf_mkset(&p2,l2,e.attr,e.clus,e.nofat,e.len,e.vdl,t); g_xf.mv=0; if(r){ xf_sync(); return -1; }
    if(xf_kill(&e)||xf_unmv(&p2,g_xf.mvat)){ xf_sync(); return -1; } return xf_sync(); }
