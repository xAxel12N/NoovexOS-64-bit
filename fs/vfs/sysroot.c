#include "include/nvx/base.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
extern int pz_inflate_raw(const unsigned char* src,int slen,unsigned char* out,int outcap);
extern long xzk_decode_into(const u8* in,long n,u8* out,long cap);
extern long zstk_decode_into(const u8* in,long n,u8* out,long cap);

typedef struct { char name[120]; char tgt[120]; u32 mode, off, clen, ulen; } sr_v1;
typedef struct { u32 type, off, clen, ulen; } sr_blk;
typedef struct { u32 name, tgt, mode, blk, boff, ulen; } sr_v2;
typedef struct { const char* name; const char* tgt; u32 mode, ulen, boff; int blk, layer; const u8* raw; u32 clen; u8* cache; int pch; } sr_ent;
typedef struct { int ent; u32 npg; u64* fr; } sr_pcf;
static void sr_pc_drop(int h);
static sr_pcf* g_pcf; static int g_npcf, g_cpcf;
u64 g_pc_fills, g_pc_hits, g_sr_unpack_ns, g_sr_unpack_bytes, g_pc_oom;
u64 sysroot_pc_page(int h,u32 pg);
u32 sysroot_pc_reclaim(u32 want);
extern u64 hr_ns(void);
extern u64 pframe_alloc(void); extern void pframe_free(u64 pa); extern void pframe_ref(u64 pa);
typedef struct { char tag[64]; const u8* blob; u32 size; int owned, used, v2, nblk, busy; const sr_blk* blk; const u8* data; u8** bcache; u32* bt; u8* bpin; } sr_layer;
extern volatile u64 g_ticks; extern unsigned long long heap_top, heap_base; extern u32 pframe_refcount(u64 pa);
static u64 g_bc_bytes, g_bc_pin; u64 g_bc_evicted;
u64 sysroot_bc_evict(u64 want,u32 min_age);
typedef struct { char* path; int kind, ent, parent, child, next; } sr_node;

#define SR_MAXL 256
static sr_layer g_ly[SR_MAXL]; static int g_nly;
static sr_ent* g_e; static int g_ne, g_ce;
static sr_node* g_nd; static int g_nn, g_cn;
static int* g_ht; static int g_hcap;

static int sr_len(const char* s){ int n=0; while(s[n])n++; return n; }
static int sr_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static u32 sr_hash(const char* s){ u32 h=2166136261u; while(*s){ h=(h^(u8)*s++)*16777619u; } return h; }
static char** g_arb; static u32* g_arc; static int g_narb, g_carb, g_arbi; static u32 g_arn;
static char* sr_dup(const char* s){ int n=sr_len(s); u32 need=(u32)n+1;
    while(g_arbi<g_narb&&g_arn+need>g_arc[g_arbi]){ g_arbi++; g_arn=0; }
    if(g_arbi>=g_narb){ if(g_narb>=g_carb){ int nc=g_carb?g_carb*2:64; char** b=(char**)krealloc(g_arb,sizeof(char*)*(unsigned long)nc); if(!b)return 0; g_arb=b; u32* c=(u32*)krealloc(g_arc,sizeof(u32)*(unsigned long)nc); if(!c)return 0; g_arc=c; g_carb=nc; }
        u32 cap=need>262144u?need:262144u; char* m=(char*)kmalloc(cap); if(!m)return 0; g_arb[g_narb]=m; g_arc[g_narb]=cap; g_arbi=g_narb++; g_arn=0; }
    char* d=g_arb[g_arbi]+g_arn; g_arn+=need; for(int i=0;i<=n;i++)d[i]=s[i]; return d; }

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
    g_arbi=0; g_arn=0; g_nn=0; if(g_ht)for(int i=0;i<g_hcap;i++)g_ht[i]=-1;
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
        for(u32 i=0;i<n;i++){ sr_ent x; x.name=t[i].name; x.tgt=t[i].tgt; x.mode=t[i].mode; x.ulen=t[i].ulen; x.clen=t[i].clen; x.raw=data+t[i].off; x.blk=-1; x.boff=0; x.layer=li; x.cache=0; x.pch=-1; if(!ent_push(&x))return 0; }
        return 1; }
    if(b[0]=='N'&&b[1]=='V'&&b[2]=='S'&&b[3]=='2'){ if(sz<16)return 0; u32 n=*(const u32*)(b+4), nb=*(const u32*)(b+8), so=*(const u32*)(b+12);
        const sr_blk* bt=(const sr_blk*)(b+16); const sr_v2* et=(const sr_v2*)(b+16+(u64)nb*sizeof(sr_blk)); const char* st=(const char*)(b+so);
        if(16+(u64)nb*sizeof(sr_blk)+(u64)n*sizeof(sr_v2)>sz||so>sz)return 0;
        l->v2=1; l->nblk=(int)nb; l->blk=bt; l->data=b;
        l->bcache=(u8**)kmalloc(sizeof(u8*)*(nb?nb:1)); l->bt=(u32*)kmalloc(sizeof(u32)*(nb?nb:1)); l->bpin=(u8*)kmalloc(nb?nb:1); if(!l->bcache||!l->bt||!l->bpin)return 0; for(u32 i=0;i<nb;i++){ l->bcache[i]=0; l->bt[i]=0; l->bpin[i]=0; }
        for(u32 i=0;i<n;i++){ sr_ent x; x.name=st+et[i].name; x.tgt=st+et[i].tgt; x.mode=et[i].mode; x.ulen=et[i].ulen; x.blk=(int)et[i].blk; x.boff=et[i].boff; x.clen=0; x.raw=0; x.layer=li; x.cache=0; x.pch=-1; if(!ent_push(&x))return 0; }
        return 1; }
    return 0; }

int g_sr_nolock[64];
long xzk_decode_unlocked(const u8* in,long n,u8* out,long cap); long zstk_decode_unlocked(const u8* in,long n,u8* out,long cap);
static void sr_bc_budget(void){ u64 cap=(heap_top-heap_base)/4; u64 un=g_bc_bytes>g_bc_pin?g_bc_bytes-g_bc_pin:0; if(un>cap)sysroot_bc_evict(un-cap*3/4,1); }
static void sr_pin(sr_layer* l,int bi){ if(!l->bpin[bi]){ l->bpin[bi]=1; g_bc_pin+=l->blk[bi].ulen; } }
static const u8* blk_get(sr_layer* l,int bi,int pin){
    if(bi<0||bi>=l->nblk)return 0; const sr_blk* k=&l->blk[bi];
    if(k->type==0)return l->data+k->off;
    if(l->bcache[bi]){ l->bt[bi]=(u32)g_ticks; if(pin)sr_pin(l,bi); return l->bcache[bi]; }
    u8* d=(u8*)kmalloc(k->ulen?k->ulen:1); if(!d&&sysroot_bc_evict((u64)k->ulen*2,1))d=(u8*)kmalloc(k->ulen?k->ulen:1); if(!d)return 0;
    int ok=0; u64 t0=hr_ns(); int nl=g_sr_nolock[cpu_id()&63]&&k->type!=1; const u8* src=l->data+k->off; long cl=(long)k->clen, ul=(long)k->ulen; int kt=k->type;
    extern volatile int g_frz_hold; int ofh=g_frz_hold; if(nl){ l->busy++; if(cpu_id()==0)g_frz_hold=1; }
    if(kt==1)ok=pz_inflate_raw(src,(int)cl,d,(int)ul)==(int)ul;
    else if(kt==2)ok=(nl?xzk_decode_unlocked(src,cl,d,ul):xzk_decode_into(src,cl,d,ul))==ul;
    else if(kt==3)ok=(nl?zstk_decode_unlocked(src,cl,d,ul):zstk_decode_into(src,cl,d,ul))==ul;
    if(nl){ l->busy--; if(cpu_id()==0)g_frz_hold=ofh; }
    if(!ok){ kfree(d); return 0; }
    g_sr_unpack_ns+=hr_ns()-t0; g_sr_unpack_bytes+=(u64)ul;
    if(l->bcache[bi]){ kfree(d); l->bt[bi]=(u32)g_ticks; if(pin)sr_pin(l,bi); return l->bcache[bi]; }
    l->bcache[bi]=d; l->bt[bi]=(u32)g_ticks; if(pin)sr_pin(l,bi); g_bc_bytes+=k->ulen; sr_bc_budget(); return d; }

static const u8* ent_data(sr_ent* e,int pin){
    if(e->ulen==0)return (const u8*)"";
    sr_layer* l=&g_ly[e->layer];
    if(!l->v2){ if(e->cache)return e->cache; u8* d=(u8*)kmalloc(e->ulen); if(!d)return 0; if(pz_inflate_raw(e->raw,(int)e->clen,d,(int)e->ulen)!=(int)e->ulen){ kfree(d); return 0; } e->cache=d; return d; }
    u32 bo=e->boff; const u8* b=blk_get(l,e->blk,pin); if(!b)return 0; return b+bo; }

static int sr_addlayer(const char* tag,const u8* blob,u32 size,int owned){
    int base=sr_eq(tag,"base"), li=-1;
    if(base){ if(g_nly>0&&g_ly[0].used)return -1; li=0; if(g_nly<1)g_nly=1; }
    else { for(int i=1;i<g_nly;i++) if(!g_ly[i].used){ li=i; break; }
        if(li<0){ if(g_nly<1)g_nly=1; if(g_nly>=SR_MAXL)return -1; li=g_nly++; } }
    sr_layer* l=&g_ly[li]; int k=0; for(;tag[k]&&k<63;k++)l->tag[k]=tag[k]; l->tag[k]=0;
    l->blob=blob; l->size=size; l->owned=owned; l->used=1; l->bcache=0; l->bt=0; l->bpin=0; l->nblk=0; l->busy=0;
    int ne0=g_ne;
    if(!sr_parse(li)){ g_ne=ne0; l->used=0; if(l->bcache)kfree(l->bcache); if(l->bt)kfree(l->bt); if(l->bpin)kfree(l->bpin); l->bcache=0; l->bt=0; l->bpin=0; return -1; }
    return li; }

#define SR_MOD SR_SLOT
#define SR_MODMAX SR_SLOTMAX
static u32 g_sr_tab[256]; static int g_sr_tabok;
int g_sr_src;
u32 sysroot_crc(const u8* p,u32 n){
    if(!g_sr_tabok){ for(u32 i=0;i<256;i++){ u32 c=i; for(int k=0;k<8;k++)c=(c&1)?0xEDB88320u^(c>>1):c>>1; g_sr_tab[i]=c; } g_sr_tabok=1; }
    u32 c=0xFFFFFFFFu; for(u32 i=0;i<n;i++)c=g_sr_tab[(c^p[i])&255]^(c>>8); return c^0xFFFFFFFFu; }
extern const u8 nvx_kimg_hdr[] __attribute__((weak));
u32 sysroot_want_crc(void){ if(!nvx_kimg_hdr)return 0; return *(volatile const u32*)(nvx_kimg_hdr+16); }
u32 sysroot_want_size(void){ if(!nvx_kimg_hdr)return 0; return *(volatile const u32*)(nvx_kimg_hdr+20); }
static u64 g_srb;
static int sr_magic(u64 a){ return *(volatile const u32*)a==0x3253564Eu; }
u64 sysroot_slot_base(void){
    if(g_srb)return g_srb;
    volatile u32* m=(volatile u32*)0x502CULL; volatile u64* a=(volatile u64*)0x5030ULL;
    if(*m==0x49464555u&&*a>=0x100000ULL&&*a<0x100000000ULL&&!(*a&0xFFFULL)){ g_srb=*a; return g_srb; }
    if(sr_magic(SR_MOD)){ g_srb=SR_MOD; return g_srb; }
    if(mm_known()){ u64 best=0; for(int i=0;i<g_mmr_n;i++){ if(g_mmr[i].t!=7&&g_mmr[i].t!=4)continue; u64 s=g_mmr[i].s<SR_MOD?SR_MOD:g_mmr[i].s, e=g_mmr[i].e>0x100000000ULL?0x100000000ULL:g_mmr[i].e;
            s=(s+0x1FFFFFULL)&~0x1FFFFFULL; for(u64 p=s;p+0x200000ULL<=e;p+=0x200000ULL) if(sr_magic(p)&&p>best)best=p; }
        if(best){ g_srb=best; return g_srb; } }
    return SR_MOD; }
u64 sysroot_slot_end(void){ u32 sz=sysroot_want_size(); if(sz<8||sz>SR_MODMAX)return 0; return (sysroot_slot_base()+(u64)sz+0x1FFFFFULL)&~0x1FFFFFULL; }
void sysroot_file_name(u32 crc,char* out){
    const char* d="/sysroot/"; int o=0; while(*d)out[o++]=*d++;
    for(int i=7;i>=0;i--){ u32 v=(crc>>(i*4))&15; out[o++]=(char)(v<10?'0'+v:'A'+v-10); }
    out[o++]='.'; out[o++]='B'; out[o++]='I'; out[o++]='N'; out[o]=0; }
int sysroot_base_ok(void){ return g_nly>0&&g_ly[0].used; }
const u8* sysroot_base_blob(u32* size){ if(!sysroot_base_ok())return 0; if(size)*size=g_ly[0].size; return g_ly[0].blob; }
int sysroot_set_base(const u8* blob,u32 size,int owned){
    if(sysroot_base_ok())return 1;
    if(!size||size!=sysroot_want_size()||sysroot_crc(blob,size)!=sysroot_want_crc())return 0;
    if(sr_addlayer("base",blob,size,owned)<0)return 0;
    sr_rebuild(); return 1; }

void sysroot_init(void){
    g_nly=1; g_ly[0].used=0; g_sr_src=0;
    u32 sz=sysroot_want_size(), c=sysroot_want_crc();
    extern unsigned long long heap_base;
    extern unsigned long long heap_top; u64 sb=sysroot_slot_base(), se=sysroot_slot_end();
    if(sz>=8&&sz<=SR_MODMAX&&!(*(volatile u32*)0x502CULL==0x49464555u&&*(volatile u64*)0x5030ULL==0)&&(heap_base>=se||heap_top<=sb)){ const u8* m=(const u8*)sb; if(sysroot_crc(m,sz)==c&&sr_addlayer("base",m,sz,0)>=0)g_sr_src=1; }
    { extern void klog(const char*); char t[96]; const char* p=g_sr_src?"linux files: found at 0x":"linux files: not found in memory, looked at 0x"; int k=0; while(*p)t[k++]=*p++;
      for(int i=28;i>=0;i-=4)t[k++]="0123456789ABCDEF"[(sb>>i)&15]; t[k]=0; klog(t); }
    sr_rebuild(); }

static int sr_del_idx(int li);
int sysroot_add_layer(const char* tag,const u8* blob,u32 size,int owned){
    if(sr_eq(tag,"base"))return sysroot_set_base(blob,size,owned);
    int old=-1; for(int i=1;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag)){ old=i; break; }
    if(old>0){ if(g_ly[old].busy)return 0;
        int fr=0; for(int i=1;i<g_nly;i++) if(!g_ly[i].used){ fr=1; break; }
        if(!fr&&g_nly>=SR_MAXL){ if(!sr_del_idx(old))return 0; old=-1; } }
    int li=sr_addlayer(tag,blob,size,owned); if(li<0)return 0;
    if(old>0){ sr_layer t=g_ly[old]; g_ly[old]=g_ly[li]; g_ly[li]=t;
        for(int i=0;i<g_ne;i++){ if(g_e[i].layer==old)g_e[i].layer=li; else if(g_e[i].layer==li)g_e[i].layer=old; }
        if(sr_del_idx(li))return 1; }
    sr_rebuild(); return 1; }

int sysroot_del_layer(const char* tag){
    int li=-1; for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag)){ li=i; break; }
    return sr_del_idx(li); }

static int sr_del_idx(int li){
    if(li<=0||li>=g_nly||!g_ly[li].used)return 0; sr_layer* l=&g_ly[li]; if(l->busy)return 0;
    int w=0; for(int i=0;i<g_ne;i++){ if(g_e[i].layer==li){ if(g_e[i].cache)kfree(g_e[i].cache); if(g_e[i].pch>=0)sr_pc_drop(g_e[i].pch); continue; } g_e[w++]=g_e[i]; } g_ne=w;
    for(int i=0;i<g_ne;i++) if(g_e[i].pch>=0)g_pcf[g_e[i].pch].ent=i;
    if(l->bcache){ for(int i=0;i<l->nblk;i++) if(l->bcache[i]){ kfree(l->bcache[i]); if(g_bc_bytes>=l->blk[i].ulen)g_bc_bytes-=l->blk[i].ulen; if(l->bpin&&l->bpin[i]&&g_bc_pin>=l->blk[i].ulen)g_bc_pin-=l->blk[i].ulen; } kfree(l->bcache); l->bcache=0; } if(l->bt){ kfree(l->bt); l->bt=0; } if(l->bpin){ kfree(l->bpin); l->bpin=0; }
    if(l->owned)kfree((void*)l->blob); l->used=0; l->blob=0; sr_rebuild(); return 1; }

const u8* sysroot_layer_blob(const char* tag,u32* size){ for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag)){ if(size)*size=g_ly[i].size; return g_ly[i].blob; } return 0; }
int sysroot_has_layer(const char* tag){ for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag))return 1; return 0; }

int sysroot_layer_files(const char* tag,int idx,char* name,int cap){
    int li=-1; for(int i=0;i<g_nly;i++) if(g_ly[i].used&&sr_eq(g_ly[i].tag,tag)){ li=i; break; } if(li<0)return 0;
    int seen=0; for(int i=0;i<g_ne;i++) if(g_e[i].layer==li){ if(seen==idx){ int k=0; for(;g_e[i].name[k]&&k<cap-1;k++)name[k]=g_e[i].name[k]; name[k]=0; return 1; } seen++; }
    return 0; }

int sysroot_stats(int* files,int* layers){ int n=0; for(int i=0;i<g_nly;i++) if(g_ly[i].used)n++; if(files)*files=g_ne; if(layers)*layers=n; return g_nn; }

static const u8* sr_get(const char* path,u32* len,int* exec,int pin){
    int v=sr_walk(path,1,0,0,0); if(v<0||g_nd[v].kind!=1)return 0; sr_ent* e=&g_e[g_nd[v].ent]; u32 ul=e->ulen, md=e->mode;
    const u8* d=ent_data(e,pin); if(!d)return 0; if(len)*len=ul; if(exec)*exec=(int)(md&1); return d; }
const u8* sysroot_get(const char* path,u32* len,int* exec){ return sr_get(path,len,exec,1); }
int sysroot_meta(const char* path,u32* len,int* exec){
    int v=sr_walk(path,1,0,0,0); if(v<0||g_nd[v].kind!=1)return 0; sr_ent* e=&g_e[g_nd[v].ent]; if(len)*len=e->ulen; if(exec)*exec=(int)(e->mode&1); return 1; }
const u8* sysroot_get_unlocked(const char* path,u32* len,int* exec){ int c=cpu_id()&63; int o=g_sr_nolock[c]; g_sr_nolock[c]=1; const u8* d=sr_get(path,len,exec,0); g_sr_nolock[c]=o; return d; }
u32 sysroot_ino(const char* path,int follow){ int v=sr_walk(path,follow,0,0,0); if(v<=0)return 0; return 0x10000000u|(sr_hash(g_nd[v].path)&0x0FFFFFFFu); }
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
static int sr_read(const char* p,char* buf,int off,int len){ u32 L; const u8* d=sr_get(p,&L,0,0); if(!d)return -1; if(off<0||(u32)off>=L)return 0; if((u32)(off+len)>L)len=(int)(L-(u32)off); for(int i=0;i<len;i++)buf[i]=(char)d[off+i]; return len; }
static int sr_size(const char* p){ int v=sr_walk(p,1,0,0,0); if(v<=0||g_nd[v].kind!=1)return -1; return (int)g_e[g_nd[v].ent].ulen; }
int sysroot_fsize(const char* p){ return sr_size(p); }
static int sr_pc_new(int ei){
    sr_ent* e=&g_e[ei]; if(e->pch>=0)return e->pch;
    if(g_npcf>=(1<<20)-1)return -1;
    if(g_npcf>=g_cpcf){ int nc=g_cpcf?g_cpcf*2:256; sr_pcf* n=(sr_pcf*)krealloc(g_pcf,sizeof(sr_pcf)*(unsigned long)nc); if(!n)return -1; g_pcf=n; g_cpcf=nc; }
    u32 np=(e->ulen+4095u)/4096u; u64* fr=(u64*)kmalloc(sizeof(u64)*(np?np:1)); if(!fr)return -1; for(u32 i=0;i<np;i++)fr[i]=0;
    g_pcf[g_npcf].ent=ei; g_pcf[g_npcf].npg=np; g_pcf[g_npcf].fr=fr; e->pch=g_npcf; return g_npcf++; }
static void sr_pc_keep(u32 h,u32 pg){ u64 f=sysroot_pc_page((int)h,pg); if(f)pframe_free(f); }
static void sr_pc_drop(int h){
    if(h<0||h>=g_npcf||!g_pcf[h].fr||g_pcf[h].ent<0)return;
    extern void jc_file_markers(u32 h,void (*fn)(u32,u32));
    jc_file_markers((u32)h,sr_pc_keep);
    g_pcf[h].ent=-1; }
u64 g_pc_reclaimed;
u32 sysroot_pc_reclaim(u32 want){
    extern u32 pframe_refcount(u64 pa); u32 n=0;
    for(int h=0;h<g_npcf&&n<want;h++){ sr_pcf* c=&g_pcf[h]; if(!c->fr||c->ent<0)continue;
        for(u32 i=0;i<c->npg&&n<want;i++) if(c->fr[i]&&pframe_refcount(c->fr[i])==1){ pframe_free(c->fr[i]); c->fr[i]=0; n++; } }
    g_pc_reclaimed+=n; return n; }
int sysroot_pc_handle(const char* path,u32* len){
    int v=sr_walk(path,1,0,0,0); if(v<0||g_nd[v].kind!=1)return -1;
    int ei=g_nd[v].ent; if(len)*len=g_e[ei].ulen; return sr_pc_new(ei); }
int sysroot_pc_handle_ptr(const u8* data,u32 len){
    for(int i=0;i<g_ne;i++){ sr_ent* e=&g_e[i]; if(e->ulen!=len||(e->mode&2))continue;
        const u8* p=0; sr_layer* l=&g_ly[e->layer];
        if(!l->v2)p=e->cache;
        else if(e->blk>=0&&e->blk<l->nblk){ const sr_blk* k=&l->blk[e->blk]; if(k->type==0)p=l->data+k->off+e->boff; else if(l->bcache[e->blk])p=l->bcache[e->blk]+e->boff; }
        if(p==data)return sr_pc_new(i); }
    return -1; }
void sysroot_pc_stats(u64* files,u64* fills,u64* hits,u64* ns,u64* bytes){ if(files)*files=(u64)g_npcf; if(fills)*fills=g_pc_fills; if(hits)*hits=g_pc_hits; if(ns)*ns=g_sr_unpack_ns; if(bytes)*bytes=g_sr_unpack_bytes; }
u32 sysroot_pc_pages(int h){ return (h>=0&&h<g_npcf&&g_pcf[h].fr&&g_pcf[h].ent>=0)?g_pcf[h].npg:0; }
u64 sysroot_pc_page(int h,u32 pg){
    if(h<0||h>=g_npcf||!g_pcf[h].fr||pg>=g_pcf[h].npg)return 0;
    u64 f=g_pcf[h].fr[pg];
    if(!f){ if(g_pcf[h].ent<0)return 0;
        int e0=g_pcf[h].ent; const u8* d=ent_data(&g_e[e0],0); if(!d)return 0;
        if(g_pcf[h].ent!=e0||!g_pcf[h].fr||pg>=g_pcf[h].npg)return 0;
        if(g_pcf[h].fr[pg]){ f=g_pcf[h].fr[pg]; g_pc_hits++; pframe_ref(f); return f; }
        sr_ent* e=&g_e[g_pcf[h].ent];
        extern u32 pframe_free_count(void); if(pframe_free_count()<8192u)sysroot_pc_reclaim(4096u);
        f=pframe_alloc(); if(!f&&sysroot_pc_reclaim(4096u))f=pframe_alloc(); if(!f){ g_pc_oom++; return 0; }
        u32 o=pg*4096u, n=e->ulen-o; if(n>4096u)n=4096u; u8* q=(u8*)f;
        for(u32 i=0;i<n;i++)q[i]=d[o+i]; for(u32 i=n;i<4096u;i++)q[i]=0;
        g_pcf[h].fr[pg]=f; g_pc_fills++; }
    else g_pc_hits++;
    pframe_ref(f); return f; }
const vfs_fs_ops SYSROOTFS={ "sysroot", sr_match, sr_read, sr_size };
extern int dt_mkdir_at(int parent,const char* name);
extern int dt_child(int parent,const char* name);
u64 sysroot_bc_evict(u64 want,u32 min_age){
    static const u32 ages[4]={6000,1000,100,0}; u64 got=0; u32 now=(u32)g_ticks;
    for(int a=0;a<4&&got<want;a++){ u32 ma=ages[a]<min_age?min_age:ages[a];
        for(int li=0;li<g_nly&&got<want;li++){ sr_layer* l=&g_ly[li]; if(!l->used||!l->v2||!l->bcache||!l->bt||!l->bpin||l->busy)continue;
            for(int i=0;i<l->nblk&&got<want;i++){ if(!l->bcache[i]||l->bpin[i]||l->blk[i].type==0||now-l->bt[i]<ma)continue;
                kfree(l->bcache[i]); l->bcache[i]=0; got+=l->blk[i].ulen; if(g_bc_bytes>=l->blk[i].ulen)g_bc_bytes-=l->blk[i].ulen; g_bc_evicted+=l->blk[i].ulen; } } }
    return got; }
void sysroot_cache_stats(u64* pc_frames,u64* pc_free,u64* bc_bytes,u64* bc_unpinned){
    u64 pf=0, pr=0; for(int h=0;h<g_npcf;h++){ sr_pcf* c=&g_pcf[h]; if(!c->fr||c->ent<0)continue; for(u32 i=0;i<c->npg;i++) if(c->fr[i]){ pf++; if(pframe_refcount(c->fr[i])==1)pr++; } }
    u64 bu=0; for(int li=0;li<g_nly;li++){ sr_layer* l=&g_ly[li]; if(!l->used||!l->v2||!l->bcache||!l->bpin)continue; for(int i=0;i<l->nblk;i++) if(l->bcache[i]&&!l->bpin[i]&&l->blk[i].type)bu+=l->blk[i].ulen; }
    if(pc_frames)*pc_frames=pf; if(pc_free)*pc_free=pr; if(bc_bytes)*bc_bytes=g_bc_bytes; if(bc_unpinned)*bc_unpinned=bu; }
void sysroot_drop_caches(int what){ if(what&1){ sysroot_pc_reclaim(0xFFFFFFFFu); sysroot_bc_evict(~0ULL,1); } }
void sysroot_mkdirs(void){
    for(int i=1;i<g_nn;i++){ if(g_nd[i].kind!=0)continue; const char* a=g_nd[i].path; int depth=0; for(const char* q=a;*q;q++) if(*q=='/')depth++; if(depth>3)continue;
        int par=0; char comp[64]; int c=0; if(*a=='/')a++;
        for(;;a++){ if(*a=='/'||!*a){ comp[c]=0; if(c){ int d=dt_child(par,comp); if(d<0)d=dt_mkdir_at(par,comp); if(d<0)break; par=d; } c=0; if(!*a)break; continue; } if(c<63)comp[c++]=*a; } } }
