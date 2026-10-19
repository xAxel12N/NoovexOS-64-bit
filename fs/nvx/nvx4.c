#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/fs/ntfs/ntfsk.h"
#define N4_MAGIC   0x3458564Eu
#define N4_MMAGIC  0x544D344Eu
#define N4_BS      4096u
#define N4_SPB     8u
#define N4_INL     17
#define N4_INDN    512
#define N4_MAXEXT  (N4_INL+N4_INDN)
#define N4_MAXBLK  (16u<<20)
typedef struct { u32 start, count; } n4ext;
typedef struct { u32 used, dir, mode, uid, gid, nlink, mtime, ctime, atime, folder; u64 len; u32 nex, ind; n4ext ext[N4_INL]; char name[64]; } n4ino;
typedef struct { char name[64]; int parent; u32 used, mode, uid, gid, mtime, ctime, atime; u32 pad[8]; } n4dir;
typedef struct { u32 used, dir, ino; char name[52]; } n4lnk;
typedef struct { u32 magic, version; u64 gen; u32 crc, size, ninode, ndir, nlink, bsize; u32 pad[6]; } n4hdr;
typedef struct { n4hdr h; n4ino ino[FS_N]; n4dir dir[DN_MAX]; n4lnk lk[NLINKS]; } n4meta;
typedef struct { u32 magic, version, bsize, nblocks, metab, meta_a, meta_b, data0; u64 created; u32 inodes, dirs, links; u32 pad[115]; } n4super;
_Static_assert(sizeof(n4ino)==256,"n4ino");
_Static_assert(sizeof(n4dir)==128,"n4dir");
_Static_assert(sizeof(n4lnk)==64,"n4lnk");
_Static_assert(sizeof(n4hdr)==64,"n4hdr");
_Static_assert(sizeof(n4super)==512,"n4super");
#define N4_METAB ((u32)((sizeof(n4meta)+N4_BS-1)/N4_BS))

int g_n4=0;
static u32 g_n4_nb, g_n4_ma, g_n4_mb, g_n4_d0, g_n4_free, g_n4_hint;
static u64 g_n4_gen;
static u8* g_n4_bm; static u8* g_n4_img; static u8* g_n4_shadow; static int g_n4_stale[2];
static n4ext g_n4x[FS_N][N4_INL]; static u32 g_n4nx[FS_N]; static n4ext* g_n4xx[FS_N]; static u32 g_n4ind[FS_N]; static u8 g_n4indd[FS_N];
static n4ext* g_n4pf; static int g_n4pfn, g_n4pfcap;
static u8 g_n4blk[N4_BS];
const char* g_n4_err="";
static u32 g_n4_crct[256]; static int g_n4_crci;

static u32 n4_crc(const u8* p,u32 n,u32 c){
    if(!g_n4_crci){ for(u32 i=0;i<256;i++){ u32 x=i; for(int j=0;j<8;j++)x=(x>>1)^(0xEDB88320u&(0u-(x&1))); g_n4_crct[i]=x; } g_n4_crci=1; }
    c=~c; for(u32 i=0;i<n;i++)c=g_n4_crct[(c^p[i])&0xFF]^(c>>8); return ~c;
}
static int n4_rd(u32 blk,u32 n,void* buf){ if(!n)return 0; u32 lba=g_nvx_base+blk*N4_SPB; return g_nvx_generic?gdisk_readn(lba,(int)(n*N4_SPB),(u8*)buf):ata_read(lba,(int)(n*N4_SPB),(u8*)buf); }
static int n4_wr(u32 blk,u32 n,const void* buf){ if(!n)return 0; u32 lba=g_nvx_base+blk*N4_SPB; return g_nvx_generic?gdisk_writen(lba,(int)(n*N4_SPB),(const u8*)buf):ata_writen(lba,(int)(n*N4_SPB),(const u8*)buf); }
static void n4_barrier(void){ if(g_nvx_generic)gdisk_flush(); }
static int bm_get(u32 b){ return (g_n4_bm[b>>3]>>(b&7))&1; }
static void bm_set(u32 b){ g_n4_bm[b>>3]|=(u8)(1u<<(b&7)); }
static void bm_clr(u32 b){ g_n4_bm[b>>3]&=(u8)~(1u<<(b&7)); }
static void n4_mark(u32 s,u32 n,int used){ for(u32 i=0;i<n;i++){ u32 b=s+i; if(b>=g_n4_nb)break; if(used){ if(!bm_get(b)){ bm_set(b); g_n4_free--; } } else if(bm_get(b)){ bm_clr(b); g_n4_free++; } } }
static u32 n4_find_free(u32 from){
    if(from>=g_n4_nb)from=g_n4_d0;
    for(int pass=0;pass<2;pass++){ u32 b=pass?g_n4_d0:from, end=pass?from:g_n4_nb;
        while(b<end){ if((b&7)==0&&b+8<=end&&g_n4_bm[b>>3]==0xFF){ b+=8; continue; } if(!bm_get(b))return b; b++; } }
    return 0xFFFFFFFFu;
}
static u32 n4_alloc(u32 want,u32 hint,u32* got){
    *got=0; if(!want||!g_n4_free)return 0;
    u32 s=0xFFFFFFFFu;
    if(hint>=g_n4_d0&&hint<g_n4_nb&&!bm_get(hint))s=hint; else s=n4_find_free(hint>=g_n4_d0?hint:g_n4_hint);
    if(s==0xFFFFFFFFu)return 0;
    u32 n=0; while(n<want&&s+n<g_n4_nb&&!bm_get(s+n))n++;
    n4_mark(s,n,1); *got=n; g_n4_hint=s+n; return s;
}
static n4ext* n4_ext(int k,u32 i){ return i<N4_INL?&g_n4x[k][i]:&g_n4xx[k][i-N4_INL]; }
static u32 n4_nblocks(int k){ u32 t=0; for(u32 i=0;i<g_n4nx[k];i++)t+=n4_ext(k,i)->count; return t; }
static void n4_pfree(u32 s,u32 n){
    if(!n)return;
    if(g_n4pfn>=g_n4pfcap){ int nc=g_n4pfcap?g_n4pfcap*2:256; n4ext* p=(n4ext*)kmalloc((unsigned long)nc*sizeof(n4ext)); if(!p){ n4_mark(s,n,0); return; }
        for(int i=0;i<g_n4pfn;i++)p[i]=g_n4pf[i]; if(g_n4pf)kfree(g_n4pf); g_n4pf=p; g_n4pfcap=nc; }
    g_n4pf[g_n4pfn].start=s; g_n4pf[g_n4pfn].count=n; g_n4pfn++;
}
static void n4_trim(int k,u32 keep,int now){
    u32 t=0, nx=0;
    for(u32 i=0;i<g_n4nx[k];i++){ n4ext* e=n4_ext(k,i);
        if(t>=keep){ if(e->count){ if(now)n4_mark(e->start,e->count,0); else n4_pfree(e->start,e->count); } e->count=0; e->start=0; continue; }
        if(t+e->count>keep){ u32 cut=keep-t; if(now)n4_mark(e->start+cut,e->count-cut,0); else n4_pfree(e->start+cut,e->count-cut); e->count=cut; }
        t+=e->count; nx=i+1; }
    if(nx!=g_n4nx[k])g_n4indd[k]=1;
    g_n4nx[k]=nx;
}
static int n4_push(int k,u32 s,u32 n){
    if(g_n4nx[k]){ n4ext* l=n4_ext(k,g_n4nx[k]-1); if(l->start+l->count==s){ l->count+=n; if(g_n4nx[k]>N4_INL)g_n4indd[k]=1; return 0; } }
    if(g_n4nx[k]>=N4_MAXEXT)return -1;
    if(g_n4nx[k]>=N4_INL&&!g_n4xx[k]){ g_n4xx[k]=(n4ext*)kmalloc(N4_INDN*sizeof(n4ext)); if(!g_n4xx[k])return -1; }
    n4ext* e=n4_ext(k,g_n4nx[k]); e->start=s; e->count=n; g_n4nx[k]++; g_n4indd[k]=1; return 0;
}
static int n4_resize(int k,u32 nb){
    u32 cur=n4_nblocks(k);
    if(nb<cur){ n4_trim(k,nb,0); return 0; }
    u32 need=nb-cur;
    while(need){ u32 hint=g_n4_hint; if(g_n4nx[k]){ n4ext* l=n4_ext(k,g_n4nx[k]-1); hint=l->start+l->count; }
        u32 got=0, s=n4_alloc(need,hint,&got);
        if(!got||n4_push(k,s,got)){ if(got)n4_mark(s,got,0); n4_trim(k,cur,1); g_n4_err="disk full"; return -1; }
        need-=got; }
    return 0;
}
static u32 n4_map(int k,u32 fb,u32* run){
    u32 t=0; for(u32 i=0;i<g_n4nx[k];i++){ n4ext* e=n4_ext(k,i); if(fb<t+e->count){ *run=e->count-(fb-t); return e->start+(fb-t); } t+=e->count; }
    *run=0; return 0;
}
static int n4_write_range(int k,u32 b0,u32 b1){
    long len=FS[k].len; const char* d=FS[k].data;
    u32 fb=b0;
    while(fb<b1){ u32 run=0, db=n4_map(k,fb,&run); if(!run)return -1; if(run>b1-fb)run=b1-fb; if(run>256)run=256;
        u32 full=run; if((long)(fb+run)*(long)N4_BS>len){ full=(u32)(len/(long)N4_BS)>fb?(u32)(len/(long)N4_BS)-fb:0; if(full>run)full=run; }
        if(full&&n4_wr(db,full,d+(long)fb*N4_BS))return -1;
        for(u32 q=full;q<run;q++){ long off=(long)(fb+q)*N4_BS; long n=len-off; if(n<0)n=0; if(n>(long)N4_BS)n=N4_BS;
            for(long i=0;i<n;i++)g_n4blk[i]=(u8)d[off+i]; for(long i=n;i<(long)N4_BS;i++)g_n4blk[i]=0;
            if(n4_wr(db+q,1,g_n4blk))return -1; }
        fb+=run; }
    return 0;
}
static int n4_sync_file(int k){
    if(!g_n4||!FS[k].used||FS[k].orphan)return 0;
    if(dt_is_tmp(FS[k].dir)){ if(g_n4nx[k]){ n4_trim(k,0,0); FS[k].clean=0; } return 0; }
    long len=FS[k].len; u32 nb=(u32)((len+(long)N4_BS-1)/(long)N4_BS); u32 cur=n4_nblocks(k);
    if(FS[k].lazy){ if(cur>nb)n4_trim(k,nb,0); }
    else if(!(FS[k].clean&&cur==nb&&!g_n4indd[k])){
        if(FS[k].lazy&&fs_ensure(k))return -1;
        if(!FS[k].data&&len)return -1;
        if(n4_resize(k,nb))return -1;
        u32 lo=nb, hi=0;
        if(!FS[k].clean){ long dl=FS[k].dlo, dh=FS[k].dhi; if(dl<0)dl=0; if(dh>len)dh=len; if(dh>dl){ lo=(u32)(dl/(long)N4_BS); hi=(u32)((dh+(long)N4_BS-1)/(long)N4_BS); } }
        if(cur<nb){ if(cur<lo)lo=cur; hi=nb; }
        if(hi>nb)hi=nb;
        if(hi>lo&&n4_write_range(k,lo,hi))return -1;
        FS[k].clean=1; FS[k].dlo=FS[k].dhi=0;
    }
    if(g_n4indd[k]){
        if(g_n4nx[k]>N4_INL){ u32 got=0, s=n4_alloc(1,g_n4_hint,&got); if(!got){ g_n4_err="disk full"; return -1; }
            for(u32 i=0;i<N4_BS;i++)g_n4blk[i]=0; n4ext* o=(n4ext*)g_n4blk; for(u32 i=N4_INL;i<g_n4nx[k];i++)o[i-N4_INL]=g_n4xx[k][i-N4_INL];
            if(n4_wr(s,1,g_n4blk)){ n4_mark(s,1,0); return -1; }
            if(g_n4ind[k])n4_pfree(g_n4ind[k],1); g_n4ind[k]=s; }
        else if(g_n4ind[k]){ n4_pfree(g_n4ind[k],1); g_n4ind[k]=0; }
        g_n4indd[k]=0; }
    return 0;
}
static void n4_cpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; for(;i<cap;i++)d[i]=0; }
static void n4_serialize(void){
    n4meta* m=(n4meta*)g_n4_img; u8* z=g_n4_img; for(u32 i=0;i<N4_METAB*N4_BS;i++)z[i]=0;
    for(int k=0;k<FS_N;k++){ n4ino* e=&m->ino[k]; if(!FS[k].used||FS[k].orphan||dt_is_tmp(FS[k].dir))continue;
        e->used=1; e->dir=(u32)FS[k].dir; e->mode=FS[k].mode?FS[k].mode:0100644u; e->uid=(u32)FS[k].uid; e->gid=(u32)FS[k].gid; e->nlink=FS[k].nlink?FS[k].nlink:1;
        e->mtime=FS[k].mtime; e->ctime=FS[k].ctime; e->atime=FS[k].atime; e->folder=(u32)FS[k].folder;
        u64 cap=(u64)n4_nblocks(k)*N4_BS; e->len=(u64)FS[k].len<cap?(u64)FS[k].len:cap;
        e->nex=g_n4nx[k]; e->ind=g_n4ind[k]; for(u32 i=0;i<g_n4nx[k]&&i<N4_INL;i++)e->ext[i]=g_n4x[k][i];
        n4_cpy(e->name,FS[k].name,64); }
    for(int i=0;i<DN_MAX;i++){ n4dir* d=&m->dir[i]; if(!DT[i].used)continue; d->used=1; d->parent=DT[i].parent; d->mode=DT[i].mode?DT[i].mode:0040755u; d->uid=(u32)DT[i].uid; d->gid=(u32)DT[i].gid;
        d->mtime=DT[i].mtime; d->ctime=DT[i].ctime; d->atime=DT[i].atime; n4_cpy(d->name,DT[i].name,64); }
    for(int i=0;i<NLINKS;i++){ n4lnk* l=&m->lk[i]; if(!g_lk[i].used||g_lk[i].ino<0||g_lk[i].ino>=FS_N||!FS[g_lk[i].ino].used||FS[g_lk[i].ino].orphan||dt_is_tmp(FS[g_lk[i].ino].dir)||dt_is_tmp(g_lk[i].dir))continue; l->used=1; l->dir=(u32)g_lk[i].dir; l->ino=(u32)g_lk[i].ino; n4_cpy(l->name,g_lk[i].name,52); }
    m->h.magic=N4_MMAGIC; m->h.version=4; m->h.gen=g_n4_gen+1; m->h.size=(u32)sizeof(n4meta); m->h.ninode=FS_N; m->h.ndir=DN_MAX; m->h.nlink=NLINKS; m->h.bsize=N4_BS; m->h.crc=0;
    m->h.crc=n4_crc(g_n4_img,(u32)sizeof(n4meta),0);
}
static int n4_write_meta(int full){
    for(int c=0;c<2;c++){ u32 base=c?g_n4_mb:g_n4_ma; int all=full||g_n4_stale[c]; u32 i=0;
        while(i<N4_METAB){ int ch=all; if(!ch){ const u64* a=(const u64*)(g_n4_img+(u64)i*N4_BS); const u64* b=(const u64*)(g_n4_shadow+(u64)i*N4_BS); for(u32 q=0;q<N4_BS/8;q++) if(a[q]!=b[q]){ ch=1; break; } }
            if(!ch){ i++; continue; }
            u32 j=i+1; while(j<N4_METAB&&j-i<64){ int c2=all; if(!c2){ const u64* a=(const u64*)(g_n4_img+(u64)j*N4_BS); const u64* b=(const u64*)(g_n4_shadow+(u64)j*N4_BS); for(u32 q=0;q<N4_BS/8;q++) if(a[q]!=b[q]){ c2=1; break; } } if(!c2)break; j++; }
            if(n4_wr(base+i,j-i,g_n4_img+(u64)i*N4_BS)){ g_n4_stale[c]=1; g_n4_err="metadata write failed"; return -1; }
            i=j; }
        n4_barrier(); g_n4_stale[c]=0; }
    return 0;
}
static int n4_commit(void){
    if(!g_n4)return -1; int bad=0;
    for(int k=0;k<FS_N;k++) if(FS[k].used&&!FS[k].orphan&&n4_sync_file(k))bad=1;
    n4_barrier();
    n4_serialize();
    if(n4_write_meta(0))return -1;
    kmemcpy(g_n4_shadow,g_n4_img,(unsigned long)N4_METAB*N4_BS); g_n4_gen++;
    for(int i=0;i<g_n4pfn;i++)n4_mark(g_n4pf[i].start,g_n4pf[i].count,0); g_n4pfn=0;
    return bad?-1:0;
}
static int n4_read_file(int k,char* buf,long len){
    if(!g_n4)return -1; u32 nb=(u32)((len+(long)N4_BS-1)/(long)N4_BS); u32 fb=0;
    while(fb<nb){ u32 run=0, db=n4_map(k,fb,&run); if(!run)return -1; if(run>nb-fb)run=nb-fb; if(run>256)run=256;
        u32 full=run; if((long)(fb+run)*(long)N4_BS>len)full=run-1;
        if(full&&n4_rd(db,full,buf+(long)fb*N4_BS))return -1;
        if(full<run){ if(n4_rd(db+full,1,g_n4blk))return -1; long off=(long)(fb+full)*N4_BS; for(long i=0;off+i<len&&i<(long)N4_BS;i++)buf[off+i]=(char)g_n4blk[i]; }
        fb+=run; }
    return 0;
}
static int n4_alloc_state(void){
    if(!g_n4_img){ g_n4_img=(u8*)kmalloc((unsigned long)N4_METAB*N4_BS); g_n4_shadow=(u8*)kmalloc((unsigned long)N4_METAB*N4_BS); }
    if(!g_n4_img||!g_n4_shadow)return -1;
    if(g_n4_bm)kfree(g_n4_bm); g_n4_bm=(u8*)kmalloc((unsigned long)(g_n4_nb+7)/8+8); if(!g_n4_bm)return -1;
    for(u32 i=0;i<(g_n4_nb+7)/8+8;i++)g_n4_bm[i]=0;
    g_n4_free=g_n4_nb; n4_mark(0,g_n4_d0,1); g_n4_hint=g_n4_d0; g_n4pfn=0;
    for(int k=0;k<FS_N;k++){ g_n4nx[k]=0; g_n4ind[k]=0; g_n4indd[k]=0; }
    return 0;
}
static int n4_layout(u32 secs){
    u32 nb=secs/N4_SPB; if(nb>N4_MAXBLK)nb=N4_MAXBLK;
    g_n4_ma=4; g_n4_mb=4+N4_METAB; g_n4_d0=4+2*N4_METAB;
    if(nb<g_n4_d0+64){ g_n4_err="partition too small"; return -1; }
    g_n4_nb=nb; return 0;
}
static int n4_valid_meta(const u8* img,u64* gen){
    const n4meta* m=(const n4meta*)img;
    if(m->h.magic!=N4_MMAGIC||m->h.version!=4||m->h.size!=sizeof(n4meta)||m->h.ninode!=FS_N||m->h.ndir!=DN_MAX||m->h.nlink!=NLINKS||m->h.bsize!=N4_BS)return 0;
    n4hdr h=m->h; u32 want=h.crc; ((n4meta*)img)->h.crc=0; u32 got=n4_crc(img,(u32)sizeof(n4meta),0); ((n4meta*)img)->h.crc=want;
    if(got!=want)return 0; *gen=h.gen; return 1;
}
static void n4_drop_file(int k){ for(u32 i=0;i<N4_INL;i++){ g_n4x[k][i].start=0; g_n4x[k][i].count=0; } g_n4nx[k]=0; g_n4ind[k]=0; }
static int n4_claim(u32 s,u32 n){ if(s<g_n4_d0||n==0||s+n>g_n4_nb||s+n<s)return -1; for(u32 i=0;i<n;i++) if(bm_get(s+i))return -1; n4_mark(s,n,1); return 0; }

int n4_mount(u32 secs){
    n4super sb;
    { u8 b[512]; if(g_nvx_generic?gdisk_read(g_nvx_base,1,b):ata_read(g_nvx_base,1,b))return -1; kmemcpy(&sb,b,512); }
    if(sb.magic!=N4_MAGIC||sb.version!=4||sb.bsize!=N4_BS||sb.metab!=N4_METAB||sb.inodes!=FS_N||sb.dirs!=DN_MAX||sb.links!=NLINKS){ g_n4_err="unsupported NVX4 layout"; return -1; }
    if(sb.nblocks*(u64)N4_SPB>(u64)secs||sb.nblocks>N4_MAXBLK||sb.meta_a!=4||sb.meta_b!=4+N4_METAB||sb.data0!=4+2*N4_METAB||sb.nblocks<sb.data0+64){ g_n4_err="NVX4 superblock does not fit the partition"; return -1; }
    g_n4_nb=sb.nblocks; g_n4_ma=sb.meta_a; g_n4_mb=sb.meta_b; g_n4_d0=sb.data0;
    if(n4_alloc_state()){ g_n4_err="not enough memory"; return -1; }
    u64 ga=0, gb=0; int va=0, vb=0;
    if(!n4_rd(g_n4_ma,N4_METAB,g_n4_img))va=n4_valid_meta(g_n4_img,&ga);
    if(!n4_rd(g_n4_mb,N4_METAB,g_n4_shadow))vb=n4_valid_meta(g_n4_shadow,&gb);
    if(!va&&!vb){ g_n4_err="both metadata copies are damaged"; return -1; }
    if(!va||(vb&&gb>ga)){ u8* t=g_n4_img; g_n4_img=g_n4_shadow; g_n4_shadow=t; g_n4_gen=gb; g_n4_stale[0]=1; g_n4_stale[1]=0; }
    else { g_n4_gen=ga; g_n4_stale[0]=0; g_n4_stale[1]=!(vb&&gb==ga); }
    kmemcpy(g_n4_shadow,g_n4_img,(unsigned long)N4_METAB*N4_BS);
    n4meta* m=(n4meta*)g_n4_img;
    for(int k=0;k<FS_N;k++){ fs_free(k); FS[k].used=0; g_fs_dirty[k]=0; if(g_n4xx[k]){ kfree(g_n4xx[k]); g_n4xx[k]=0; } n4_drop_file(k); }
    int damaged=0;
    for(int k=0;k<FS_N;k++){ n4ino* e=&m->ino[k]; if(!e->used)continue;
        FsFile* f=&FS[k]; n4_cpy(f->name,e->name,64); f->dir=(int)e->dir; f->mode=e->mode; f->uid=(int)e->uid; f->gid=(int)e->gid; f->nlink=e->nlink?e->nlink:1;
        f->mtime=e->mtime; f->ctime=e->ctime; f->atime=e->atime; f->folder=(int)e->folder; f->used=1; f->orphan=0; f->tick=0;
        int ok=e->nex<=N4_MAXEXT;
        u32 nx=ok?e->nex:0;
        if(ok&&nx>N4_INL){ g_n4xx[k]=(n4ext*)kmalloc(N4_INDN*sizeof(n4ext)); if(!g_n4xx[k]||e->ind<g_n4_d0||e->ind>=g_n4_nb||n4_rd(e->ind,1,g_n4blk))ok=0;
            else { n4ext* src=(n4ext*)g_n4blk; for(u32 i=0;i<N4_INDN;i++)g_n4xx[k][i]=src[i]; } }
        if(ok){ for(u32 i=0;i<nx&&i<N4_INL;i++)g_n4x[k][i]=e->ext[i]; g_n4nx[k]=nx; g_n4ind[k]=nx>N4_INL?e->ind:0;
            if(g_n4ind[k]&&n4_claim(g_n4ind[k],1))ok=0;
            for(u32 i=0;ok&&i<nx;i++){ n4ext* x=n4_ext(k,i); if(n4_claim(x->start,x->count)){ for(u32 j=0;j<i;j++){ n4ext* y=n4_ext(k,j); n4_mark(y->start,y->count,0); } if(g_n4ind[k])n4_mark(g_n4ind[k],1,0); ok=0; } } }
        if(!ok){ n4_drop_file(k); damaged++; f->len=0; f->lazy=0; f->clean=0; continue; }
        u64 cap=(u64)n4_nblocks(k)*N4_BS; u64 L=e->len; if(L>cap)L=cap; if(L>(u64)(FS_CAP-1))L=FS_CAP-1;
        f->len=(int)L; f->lazy=f->len>0; f->clean=1; f->dlo=f->dhi=0; }
    for(int i=0;i<DN_MAX;i++){ n4dir* d=&m->dir[i]; DT[i].used=(int)d->used; if(!d->used)continue; n4_cpy(DT[i].name,d->name,64); DT[i].parent=d->parent; DT[i].mode=d->mode; DT[i].uid=(int)d->uid; DT[i].gid=(int)d->gid; DT[i].mtime=d->mtime; DT[i].ctime=d->ctime; DT[i].atime=d->atime; }
    for(int i=1;i<DN_MAX;i++) if(DT[i].used&&(DT[i].parent<0||DT[i].parent>=DN_MAX||!m->dir[DT[i].parent].used||DT[i].parent==i))DT[i].parent=0;
    for(int i=0;i<NLINKS;i++){ n4lnk* l=&m->lk[i]; g_lk[i].used=(int)l->used&&l->ino<FS_N&&FS[l->ino].used; g_lk[i].dir=(int)l->dir; g_lk[i].ino=(int)l->ino; n4_cpy(g_lk[i].name,l->name,52); }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&(FS[k].dir<0||FS[k].dir>=DN_MAX||!DT[FS[k].dir].used))FS[k].dir=0;
    g_n4=1; dt_load_done();
    if(damaged){ g_n4_err="some damaged files were emptied"; klog("nvx4: damaged extents found, affected files emptied"); }
    return 0;
}
static int n4_write_super(u64 created){
    u8 b[512]; for(int i=0;i<512;i++)b[i]=0; n4super* s=(n4super*)b;
    s->magic=N4_MAGIC; s->version=4; s->bsize=N4_BS; s->nblocks=g_n4_nb; s->metab=N4_METAB; s->meta_a=g_n4_ma; s->meta_b=g_n4_mb; s->data0=g_n4_d0; s->created=created; s->inodes=FS_N; s->dirs=DN_MAX; s->links=NLINKS;
    int r=g_nvx_generic?gdisk_writen(g_nvx_base,1,b):ata_writen(g_nvx_base,1,b); n4_barrier(); return r;
}
static u32 g_n4_res[64][2]; int g_n4_nres;
void n4_reserve_secs(u32 sec,u32 n){ if(!n||g_n4_nres>=64)return; g_n4_res[g_n4_nres][0]=sec/N4_SPB; g_n4_res[g_n4_nres][1]=(sec+n+N4_SPB-1)/N4_SPB-sec/N4_SPB; g_n4_nres++; }
static void n4_apply_res(int set){
    for(int i=0;i<g_n4_nres;i++){ u32 s=g_n4_res[i][0], e=s+g_n4_res[i][1]; if(s<g_n4_d0)s=g_n4_d0; if(e>g_n4_nb)e=g_n4_nb; if(s<e)n4_mark(s,e-s,set); }
}
#define N4_CB_MAGIC 0x3156434E4B425856ULL
#define N4_CB_CHUNK 16u
typedef struct { u64 magic; u32 zone, bstart, nb, pad; u64 sum, self; } n4cbm;
static u64 n4_fnv(u64 h,const u8* p,u32 n){ for(u32 i=0;i<n;i++){ h^=p[i]; h*=0x100000001B3ULL; } return h; }
static u64 n4_cb_self(const n4cbm* m){ return n4_fnv(0xCBF29CE484222325ULL,(const u8*)m,(u32)(sizeof(n4cbm)-8)); }
static int n4_cb_read(u32 nb,n4cbm* out){ u8* b=(u8*)kmalloc(N4_BS); if(!b)return 0; int ok=0;
    if(nb>2&&!n4_rd(nb-1,1,b)){ kmemcpy(out,b,sizeof(n4cbm)); ok=out->magic==N4_CB_MAGIC&&out->self==n4_cb_self(out)&&out->nb==nb&&out->zone>0&&out->bstart>=out->zone&&out->bstart+out->zone<nb; }
    kfree(b); return ok; }
static void n4_cb_clear(u32 nb){ u8* b=(u8*)kmalloc(N4_BS); if(!b)return; for(u32 i=0;i<N4_BS;i++)b[i]=0; n4_wr(nb-1,1,b); kfree(b); }
int n4_conv_backup(u32 secs){
    u32 nb=secs/N4_SPB, zone=4+2*N4_METAB; if(nb<2*zone+2)return -1;
    u32 bs=nb-1-zone; for(int i=0;i<g_n4_nres;i++) if(g_n4_res[i][0]+g_n4_res[i][1]>bs)return -1;
    u8* b=(u8*)kmalloc(N4_CB_CHUNK*N4_BS); if(!b)return -1; u64 h=0xCBF29CE484222325ULL;
    for(u32 c=0;c<zone;c+=N4_CB_CHUNK){ u32 k=zone-c<N4_CB_CHUNK?zone-c:N4_CB_CHUNK; if(n4_rd(c,k,b)||n4_wr(bs+c,k,b)){ kfree(b); return -1; } h=n4_fnv(h,b,k*N4_BS); }
    n4cbm m; for(u32 i=0;i<sizeof(m);i++)((u8*)&m)[i]=0; m.magic=N4_CB_MAGIC; m.zone=zone; m.bstart=bs; m.nb=nb; m.sum=h; m.self=n4_cb_self(&m);
    for(u32 i=0;i<N4_BS;i++)b[i]=0; kmemcpy(b,&m,sizeof(m)); int r=n4_wr(nb-1,1,b); kfree(b); if(r)return -1;
    if(g_n4_nres<64){ g_n4_res[g_n4_nres][0]=bs; g_n4_res[g_n4_nres][1]=nb-bs; g_n4_nres++; }
    return 0; }
void n4_conv_done(u32 secs){ n4cbm m; u32 nb=secs/N4_SPB; if(n4_cb_read(nb,&m))n4_cb_clear(nb); }
int n4_conv_restore(u32 secs){
    u32 nb=secs/N4_SPB; n4cbm m; if(!n4_cb_read(nb,&m))return 0;
    u8* b=(u8*)kmalloc(N4_CB_CHUNK*N4_BS); if(!b)return -1; u64 h=0xCBF29CE484222325ULL;
    for(u32 c=0;c<m.zone;c+=N4_CB_CHUNK){ u32 k=m.zone-c<N4_CB_CHUNK?m.zone-c:N4_CB_CHUNK; if(n4_rd(m.bstart+c,k,b)){ kfree(b); return -1; } h=n4_fnv(h,b,k*N4_BS); }
    if(h!=m.sum){ kfree(b); n4_cb_clear(nb); return -1; }
    for(u32 c=0;c<m.zone;c+=N4_CB_CHUNK){ u32 k=m.zone-c<N4_CB_CHUNK?m.zone-c:N4_CB_CHUNK; if(n4_rd(m.bstart+c,k,b)||n4_wr(c,k,b)){ kfree(b); return -1; } }
    kfree(b); return 1; }
int n4_format(u32 secs){
    if(n4_layout(secs))return -1;
    if(n4_alloc_state()){ g_n4_err="not enough memory"; return -1; }
    for(int k=0;k<FS_N;k++){ if(g_n4xx[k]){ kfree(g_n4xx[k]); g_n4xx[k]=0; } n4_drop_file(k); }
    n4_apply_res(1);
    g_n4=1; g_n4_gen=0;
    for(int k=0;k<FS_N;k++) if(FS[k].used&&!FS[k].orphan){ if(fs_ensure(k)){ g_n4=0; g_n4_err="not enough memory"; return -1; } FS[k].clean=0; FS[k].dlo=0; FS[k].dhi=0x7FFFFFFF;
        if(n4_sync_file(k)){ g_n4=0; if(!g_n4_err[0])g_n4_err="write failed"; return -1; } }
    n4_barrier();
    n4_serialize(); g_n4_stale[0]=g_n4_stale[1]=1;
    if(n4_write_meta(1)){ g_n4=0; return -1; }
    kmemcpy(g_n4_shadow,g_n4_img,(unsigned long)N4_METAB*N4_BS); g_n4_gen=1;
    if(n4_write_super(k_realtime_ms()/1000ULL)){ g_n4=0; g_n4_err="superblock write failed"; return -1; }
    n4_apply_res(0);
    g_n4_nres=0; g_n4pfn=0;
    return 0;
}
void n4_usage(u64* total,u64* freeb){ if(g_n4){ *total=(u64)(g_n4_nb-g_n4_d0)*N4_BS; *freeb=(u64)g_n4_free*N4_BS; } else { *total=0; *freeb=0; } }

int fs_backend_load(int k){
    if(k<0||k>=FS_N||!FS[k].used)return -1;
    long len=FS[k].len; if(FS[k].data){ kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; }
    if(fs_reserve(k,len))return -1;
    int r=-1;
    if(g_rootfs)r=nk_root_read(k,FS[k].data,len); else if(g_n4)r=n4_read_file(k,FS[k].data,len);
    if(r){ kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; return -1; }
    FS[k].data[len]=0; return 0;
}
int fs_reloadable(int k){
    if(FS[k].orphan||!disk_ok)return 0;
    if(dt_is_tmp(FS[k].dir))return 0;
    if(g_rootfs)return 1;
    if(g_n4)return (u64)n4_nblocks(k)*N4_BS>=(u64)FS[k].len&&!g_n4indd[k];
    return 0;
}
int nvx3_sync(int k){
    if(!disk_ok||k<0||k>=FS_N||!FS[k].used||FS[k].orphan)return -1;
    if(dt_is_tmp(FS[k].dir))return k;
    if(g_rootfs)return nk_root_sync_file(k);
    if(!g_n4)return -1;
    if(n4_sync_file(k))return -1;
    if(n4_commit())return -1;
    return k;
}
void nvx3_flush_meta(void){ g_fs_wb_pending=0; if(!disk_ok)return; if(g_rootfs){ nk_root_flush(); return; } if(g_n4)n4_commit(); }
void nvx3_sync_all(void){ g_fs_wb_pending=0; if(!disk_ok)return; if(g_rootfs){ for(int k=0;k<FS_N;k++) if(FS[k].used&&!FS[k].orphan&&!FS[k].clean)nk_root_sync_file(k); nk_root_flush(); return; } if(g_n4)n4_commit(); }
int fs_sync(int k){ if(!disk_ok||k<0||k>=FS_N||!FS[k].used||FS[k].orphan)return -1; return nvx3_sync(k); }
