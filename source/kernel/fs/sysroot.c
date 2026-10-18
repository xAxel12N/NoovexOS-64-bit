#include "include/nvx/base.h"
#include "include/nvx/fs/sysroot.h"
#include "include/nvx/fs/vfs.h"
#include "include/nvx/mm/heap.h"
extern int pz_inflate_raw(const unsigned char* src,int slen,unsigned char* out,int outcap);
extern long xzk_decode_into(const u8* in,long n,u8* out,long cap);
extern const u8 _binary_sysroot_bin_start[], _binary_sysroot_bin_end[];

typedef struct { char name[120]; char tgt[120]; u32 mode, off, clen, ulen; } sr_v1;
typedef struct { u32 type, off, clen, ulen; } sr_blk;
typedef struct { u32 name, tgt, mode, blk, boff, ulen; } sr_v2;
typedef struct { const char* name; const char* tgt; u32 mode, ulen, boff; int blk, layer; const u8* raw; u32 clen; u8* cache; } sr_ent;
typedef struct { char tag[64]; const u8* blob; u32 size; int owned, used, v2, nblk; const sr_blk* blk; const u8* data; u8** bcache; } sr_layer;
typedef struct { char* path; int kind, ent, parent, child, next; } sr_node;

#define SR_MAXL 256
static sr_layer g_ly[SR_MAXL]; static int g_nly;
static sr_ent* g_e; static int g_ne, g_ce;
static sr_node* g_nd; static int g_nn, g_cn;
static int* g_ht; static int g_hcap;

static int sr_len(const char* s){ int n=0; while(s[n])n++; return n; }
static int sr_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static u32 sr_hash(const char* s){ u32 h=2166136261u; while(*s){ h=(h^(u8)*s++)*16777619u; } return h; }
static char* sr_dup(const char* s){ int n=sr_len(s); char* d=(char*)kmalloc((unsigned long)n+1); if(!d)return 0; for(int i=0;i<=n;i++)d[i]=s[i]; return d; }

static void sr_norm(const char* in,char* out,int cap){
    int o=0; const char* p=in; out[o++]='/';
    while(*p){ while(*p=='/')p++; if(!*p)break; const char* s=p; int l=0; while(p[l]&&p[l]!='/')l++; p+=l;
        if(l==1&&s[0]=='.')continue;
        if(l==2&&s[0]=='.'&&s[1]=='.'){ while(o>1&&out[o-1]!='/')o--; if(o>1)o--; continue; }
        if(o>1&&o<cap-1)out[o++]='/'; for(int i=0;i<l&&o<cap-1;i++)out[o++]=s[i]; }
    out[o]=0; }

static int ht_get(const char* p){ if(!g_ht)return -1; u32 m=(u32)g_hcap-1; u32 i=sr_hash(p)&m; for(;;){ int v=g_ht[i]; if(v<0)return -1; if(sr_eq(g_nd[v].path,p))return v; i=(i+1)&m; } }
static void ht_put(int idx){ u32 m=(u32)g_hcap-1; u32 i=sr_hash(g_nd[idx].path)&m; while(g_ht[i]>=0)i=(i+1)&m; g_ht[i]=idx; }
static int ht_grow(void){
    if(g_ht&&(g_nn+1)*2<g_hcap)return 1;
    int nc=g_hcap?g_hcap*2:4096; while((g_nn+1)*2>=nc)nc*=2;
    int* t=(int*)kmalloc(sizeof(int)*(unsigned long)nc); if(!t)return 0; for(int i=0;i<nc;i++)t[i]=-1;
    if(g_ht)kfree(g_ht); g_ht=t; g_hcap=nc; for(int i=0;i<g_nn;i++)ht_put(i); return 1; }
static int nd_new(const char* path,int kind,int ent,int parent){
    if(g_nn>=g_cn){ int nc=g_cn?g_cn*2:4096; sr_node* n=(sr_node*)krealloc(g_nd,sizeof(sr_node)*(unsigned long)nc); if(!n)return -1; g_nd=n; g_cn=nc; }
    if(!ht_grow())return -1;
    char* d=sr_dup(path); if(!d)return -1;
    int i=g_nn++; g_nd[i].path=d; g_nd[i].kind=kind; g_nd[i].ent=ent; g_nd[i].parent=parent; g_nd[i].child=-1; g_nd[i].next=-1;
    if(parent>=0){ g_nd[i].next=g_nd[parent].child; g_nd[parent].child=i; }
    ht_put(i); return i; }

static int sr_walk(const char* path,int follow,int create,int depth,char* outp){
    if(depth>16)return -1;
    char n[256]; sr_norm(path,n,sizeof(n));
    if(n[1]==0){ if(outp){ outp[0]='/'; outp[1]=0; } return ht_get("/"); }
    int L=sr_len(n); int pos=1; int pv=ht_get("/");
    while(pos<=L){ int e=pos; while(e<L&&n[e]!='/')e++;
        char c=n[e]; n[e]=0; int v=ht_get(n);
        if(v<0&&create&&pv>=0)v=nd_new(n,0,-1,pv);
        n[e]=c;
        if(v<0)return -1;
        int last=(e>=L);
        if(g_nd[v].kind==2&&(!last||follow)){ char r[256]; int o=0; const char* t=g_e[g_nd[v].ent].tgt;
            if(t[0]!='/'){ int ps=pos-1; for(int i=0;i<ps&&o<200;i++)r[o++]=n[i]; r[o++]='/'; }
            while(*t&&o<220)r[o++]=*t++; for(int i=e;i<L&&o<254;i++)r[o++]=n[i]; r[o]=0; return sr_walk(r,follow,create,depth+1,outp); }
        if(last){ if(outp){ int i=0; for(;n[i]&&i<255;i++)outp[i]=n[i]; outp[i]=0; } return v; }
        if(g_nd[v].kind!=0)return -1;
        pv=v; pos=e+1; }
    return -1; }

static void sr_place(int ei){
    sr_ent* e=&g_e[ei]; char n[256]; sr_norm(e->name,n,sizeof(n)); int L=sr_len(n); if(L<=1)return;
    int s=L; while(s>0&&n[s-1]!='/')s--; char par[256]; if(s<=1){ par[0]='/'; par[1]=0; } else { for(int i=0;i<s-1;i++)par[i]=n[i]; par[s-1]=0; }
    char rp[256]; int pv=sr_walk(par,1,1,0,rp);
    if(pv<0||g_nd[pv].kind!=0)return;
    char full[256]; int o=0; for(int i=0;rp[i]&&o<250;i++)full[o++]=rp[i]; if(o>1)full[o++]='/'; for(int i=s;i<L&&o<255;i++)full[o++]=n[i]; full[o]=0;
    if(ht_get(full)>=0)return;
    nd_new(full,(e->mode&2)?2:1,ei,pv); }

static void sr_rebuild(void){
    for(int i=0;i<g_nn;i++)kfree(g_nd[i].path); g_nn=0; if(g_ht)for(int i=0;i<g_hcap;i++)g_ht[i]=-1;
    nd_new("/",0,-1,-1);
    for(int i=0;i<g_ne;i++) if(g_e[i].layer==0&&(g_e[i].mode&2)) sr_place(i);
    for(int li=g_nly-1;li>=1;li--){ if(!g_ly[li].used)continue; for(int i=0;i<g_ne;i++) if(g_e[i].layer==li) sr_place(i); }
    for(int i=0;i<g_ne;i++) if(g_e[i].layer==0&&!(g_e[i].mode&2)) sr_place(i); }

static int ent_push(sr_ent* x){
    if(g_ne>=g_ce){ int nc=g_ce?g_ce*2:4096; sr_ent* n=(sr_ent*)krealloc(g_e,sizeof(sr_ent)*(unsigned long)nc); if(!n)return 0; g_e=n; g_ce=nc; }
    g_e[g_ne++]=*x; return 1; }

static int sr_parse(int li){
    sr_layer* l=&g_ly[li]; const u8* b=l->blob; u32 sz=l->size;
    if(sz<8)return 0;
    if(b[0]=='N'&&b[1]=='V'&&b[2]=='S'&&b[3]=='R'){ u32 n=*(const u32*)(b+4); const sr_v1* t=(const sr_v1*)(b+8); const u8* data=b+8+(u64)n*sizeof(sr_v1); if(8+(u64)n*sizeof(sr_v1)>sz)return 0;
        l->v2=0; l->nblk=0; l->data=data;
        for(u32 i=0;i<n;i++){ sr_ent x; x.name=t[i].name; x.tgt=t[i].tgt; x.mode=t[i].mode; x.ulen=t[i].ulen; x.clen=t[i].clen; x.raw=data+t[i].off; x.blk=-1; x.boff=0; x.layer=li; x.cache=0; if(!ent_push(&x))return 0; }
        return 1; }
    if(b[0]=='N'&&b[1]=='V'&&b[2]=='S'&&b[3]=='2'){ if(sz<16)return 0; u32 n=*(const u32*)(b+4), nb=*(const u32*)(b+8), so=*(const u32*)(b+12);
        const sr_blk* bt=(const sr_blk*)(b+16); const sr_v2* et=(const sr_v2*)(b+16+(u64)nb*sizeof(sr_blk)); const char* st=(const char*)(b+so);
        if(16+(u64)nb*sizeof(sr_blk)+(u64)n*sizeof(sr_v2)>sz||so>sz)return 0;
        l->v2=1; l->nblk=(int)nb; l->blk=bt; l->data=b;
        l->bcache=(u8**)kmalloc(sizeof(u8*)*(nb?nb:1)); if(!l->bcache)return 0; for(u32 i=0;i<nb;i++)l->bcache[i]=0;
        for(u32 i=0;i<n;i++){ sr_ent x; x.name=st+et[i].name; x.tgt=st+et[i].tgt; x.mode=et[i].mode; x.ulen=et[i].ulen; x.blk=(int)et[i].blk; x.boff=et[i].boff; x.clen=0; x.raw=0; x.layer=li; x.cache=0; if(!ent_push(&x))return 0; }
        return 1; }
    return 0; }

static const u8* blk_get(sr_layer* l,int bi){
    if(bi<0||bi>=l->nblk)return 0; const sr_blk* k=&l->blk[bi];
    if(k->type==0)return l->data+k->off;
    if(l->bcache[bi])return l->bcache[bi];
    u8* d=(u8*)kmalloc(k->ulen?k->ulen:1); if(!d)return 0;
    int ok=0;
    if(k->type==1)ok=pz_inflate_raw(l->data+k->off,(int)k->clen,d,(int)k->ulen)==(int)k->ulen;
    else if(k->type==2)ok=xzk_decode_into(l->data+k->off,(long)k->clen,d,(long)k->ulen)==(long)k->ulen;
    if(!ok){ kfree(d); return 0; }
    l->bcache[bi]=d; return d; }

static const u8* ent_data(sr_ent* e){
    if(e->ulen==0)return (const u8*)"";
    sr_layer* l=&g_ly[e->layer];
    if(!l->v2){ if(e->cache)return e->cache; u8* d=(u8*)kmalloc(e->ulen); if(!d)return 0; if(pz_inflate_raw(e->raw,(int)e->clen,d,(int)e->ulen)!=(int)e->ulen){ kfree(d); return 0; } e->cache=d; return d; }
    const u8* b=blk_get(l,e->blk); if(!b)return 0; return b+e->boff; }

static int sr_addlayer(const char* tag,const u8* blob,u32 size,int owned){
    int li=-1; for(int i=0;i<g_nly;i++) if(!g_ly[i].used){ li=i; break; }
    if(li<0){ if(g_nly>=SR_MAXL)return -1; li=g_nly++; }
    sr_layer* l=&g_ly[li]; int k=0; for(;tag[k]&&k<63;k++)l->tag[k]=tag[k]; l->tag[k]=0;
    l->blob=blob; l->size=size; l->owned=owned; l->used=1; l->bcache=0; l->nblk=0;
    int ne0=g_ne;
    if(!sr_parse(li)){ g_ne=ne0; l->used=0; if(l->bcache)kfree(l->bcache); return -1; }
    return li; }

void sysroot_init(void){
    const u8* b=_binary_sysroot_bin_start; u32 sz=(u32)(_binary_sysroot_bin_end-_binary_sysroot_bin_start);
    sr_addlayer("base",b,sz,0); sr_rebuild(); }

int sysroot_add_layer(const char* tag,const u8* blob,u32 size,int owned){
    for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag))sysroot_del_layer(tag);
    int li=sr_addlayer(tag,blob,size,owned); if(li<0)return 0; sr_rebuild(); return 1; }

int sysroot_del_layer(const char* tag){
    int li=-1; for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag)){ li=i; break; }
    if(li<=0)return 0; sr_layer* l=&g_ly[li];
    int w=0; for(int i=0;i<g_ne;i++){ if(g_e[i].layer==li){ if(g_e[i].cache)kfree(g_e[i].cache); continue; } g_e[w++]=g_e[i]; } g_ne=w;
    if(l->bcache){ for(int i=0;i<l->nblk;i++) if(l->bcache[i])kfree(l->bcache[i]); kfree(l->bcache); }
    if(l->owned)kfree((void*)l->blob); l->used=0; l->blob=0; sr_rebuild(); return 1; }

int sysroot_has_layer(const char* tag){ for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag))return 1; return 0; }

int sysroot_layer_files(const char* tag,int idx,char* name,int cap){
    int li=-1; for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag)){ li=i; break; } if(li<0)return 0;
    int seen=0; for(int i=0;i<g_ne;i++) if(g_e[i].layer==li){ if(seen==idx){ int k=0; for(;g_e[i].name[k]&&k<cap-1;k++)name[k]=g_e[i].name[k]; name[k]=0; return 1; } seen++; }
    return 0; }

int sysroot_stats(int* files,int* layers){ int n=0; for(int i=0;i<g_nly;i++) if(g_ly[i].used)n++; if(files)*files=g_ne; if(layers)*layers=n; return g_nn; }

const u8* sysroot_get(const char* path,u32* len,int* exec){
    int v=sr_walk(path,1,0,0,0); if(v<0||g_nd[v].kind!=1)return 0; sr_ent* e=&g_e[g_nd[v].ent];
    const u8* d=ent_data(e); if(!d)return 0; if(len)*len=e->ulen; if(exec)*exec=(int)(e->mode&1); return d; }
int sysroot_isdir(const char* path){ int v=sr_walk(path,1,0,0,0); return v>0&&g_nd[v].kind==0; }
int sysroot_exists(const char* path){ int v=sr_walk(path,1,0,0,0); return v>0; }
int sysroot_islink(const char* path){ int v=sr_walk(path,0,0,0,0); return v>0&&g_nd[v].kind==2; }
int sysroot_readlink(const char* path,char* out,int cap){
    int v=sr_walk(path,0,0,0,0); if(v<=0||g_nd[v].kind!=2)return -1; const char* t=g_e[g_nd[v].ent].tgt; int k=0; for(;t[k]&&k<cap;k++)out[k]=t[k]; return k; }
int sysroot_list(const char* dir,int idx,char* name,int cap,int* isdir){
    int v=sr_walk(dir,1,0,0,0); if(v<0||g_nd[v].kind!=0)return 0;
    int c=g_nd[v].child; while(c>=0&&idx>0){ c=g_nd[c].next; idx--; } if(c<0)return 0;
    const char* p=g_nd[c].path; int s=sr_len(p); while(s>0&&p[s-1]!='/')s--; int k=0; for(;p[s+k]&&k<cap-1;k++)name[k]=p[s+k]; name[k]=0;
    if(isdir)*isdir=g_nd[c].kind==0; return 1; }
static int sr_match(const char* p){ int v=sr_walk(p,1,0,0,0); return v>0&&g_nd[v].kind==1; }
static int sr_read(const char* p,char* buf,int off,int len){ u32 L; const u8* d=sysroot_get(p,&L,0); if(!d)return -1; if(off<0||(u32)off>=L)return 0; if((u32)(off+len)>L)len=(int)(L-(u32)off); for(int i=0;i<len;i++)buf[i]=(char)d[off+i]; return len; }
static int sr_size(const char* p){ int v=sr_walk(p,1,0,0,0); if(v<=0||g_nd[v].kind!=1)return -1; return (int)g_e[g_nd[v].ent].ulen; }
const vfs_fs_ops SYSROOTFS={ "sysroot", sr_match, sr_read, sr_size };
extern int dt_mkdir_at(int parent,const char* name);
extern int dt_child(int parent,const char* name);
void sysroot_mkdirs(void){
    for(int i=1;i<g_nn;i++){ if(g_nd[i].kind!=0)continue; const char* a=g_nd[i].path; int depth=0; for(const char* q=a;*q;q++) if(*q=='/')depth++; if(depth>3)continue;
        int par=0; char comp[64]; int c=0; if(*a=='/')a++;
        for(;;a++){ if(*a=='/'||!*a){ comp[c]=0; if(c){ int d=dt_child(par,comp); if(d<0)d=dt_mkdir_at(par,comp); if(d<0)break; par=d; } c=0; if(!*a)break; continue; } if(c<63)comp[c++]=*a; } } }
