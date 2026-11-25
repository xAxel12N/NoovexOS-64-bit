#include "fs/ext4/ext4.h"
#ifdef EXT4_HOST
#include <time.h>
static u32 e4_now(void){ return (u32)time(0); }
#else
#include "include/nvx/drivers/rtc/cmos.h"
static u32 e4_now(void){ int h,m,s,y,mo,d; read_clock(&h,&m,&s,&y,&mo,&d); if(y<100)y+=2000; if(mo<1||mo>12||d<1)return 0;
    static const u16 cum[12]={0,31,59,90,120,151,181,212,243,273,304,334}; u32 days=(u32)(y-1970)*365+(u32)((y-1969)/4)-(u32)((y-1901)/100)+(u32)((y-1601)/400)+cum[mo-1]+(u32)(d-1);
    if(mo>2&&((y%4==0&&y%100!=0)||y%400==0))days++; return days*86400u+(u32)h*3600u+(u32)m*60u+(u32)s; }
#endif

ext4_fs g_ext4;

static u32 e4_g_crc_tab[256]; static int e4_g_crc_ok;
u32 e4_crc32c(u32 crc,const void* p,unsigned long n){
    if(!e4_g_crc_ok){ for(u32 i=0;i<256;i++){ u32 c=i; for(int k=0;k<8;k++)c=(c&1)?(c>>1)^0x82F63B78u:c>>1; e4_g_crc_tab[i]=c; } e4_g_crc_ok=1; }
    const u8* b=(const u8*)p; while(n--)crc=e4_g_crc_tab[(crc^*b++)&0xFF]^(crc>>8); return crc; }
static u16 e4_crc16(u16 crc,const void* p,unsigned long n){ const u8* b=(const u8*)p; while(n--){ crc^=*b++; for(int k=0;k<8;k++)crc=(crc&1)?(u16)((crc>>1)^0xA001):(u16)(crc>>1); } return crc; }

void e4_sb_csum_set(ext4_fs* fs){ if(fs->csum)e4_st32(fs->sb+0x3FC,e4_crc32c(~0u,fs->sb,0x3FC)); }

int ext4_bread_raw(ext4_fs* fs,u64 blk,void* buf){ if(blk>=fs->blocks_count)return E4_ERR_IO; return fs->rd(fs->base+blk*fs->spb,(int)fs->spb,buf)?E4_ERR_IO:0; }
int ext4_bwrite_raw(ext4_fs* fs,u64 blk,const void* buf){ if(fs->ro||!fs->wr)return E4_ERR_ROFS; if(blk>=fs->blocks_count)return E4_ERR_IO; return fs->wr(fs->base+blk*fs->spb,(int)fs->spb,buf)?E4_ERR_IO:0; }
int ext4_jnl_peek(ext4_fs* fs,u64 blk,void* buf);
int ext4_bread(ext4_fs* fs,u64 blk,void* buf){ if(ext4_jnl_peek(fs,blk,buf))return 0; return ext4_bread_raw(fs,blk,buf); }
int ext4_bwrite(ext4_fs* fs,u64 blk,const void* buf){ if(fs->jabort)return E4_ERR_NOSPC; return ext4_bwrite_raw(fs,blk,buf); }
int ext4_bwrite_meta(ext4_fs* fs,u64 blk,const void* buf){ if(fs->ro)return E4_ERR_ROFS; if(fs->jabort)return E4_ERR_NOSPC; if(fs->jnl&&fs->in_txn)return ext4_jnl_write(fs,blk,buf); return ext4_bwrite_raw(fs,blk,buf); }

static u8* e4_gd(ext4_fs* fs,u32 g){ return fs->gdt+(u64)g*fs->desc_size; }
static u64 e4_gd64(ext4_fs* fs,u32 g,int lo,int hi){ u8* d=e4_gd(fs,g); u64 v=e4_le32(d+lo); if(fs->has64&&fs->desc_size>=64)v|=(u64)e4_le32(d+hi)<<32; return v; }
static u32 e4_gd32(ext4_fs* fs,u32 g,int lo,int hi){ u8* d=e4_gd(fs,g); u32 v=e4_le16(d+lo); if(fs->has64&&fs->desc_size>=64)v|=(u32)e4_le16(d+hi)<<16; return v; }
static void e4_gd32s(ext4_fs* fs,u32 g,int lo,int hi,u32 v){ u8* d=e4_gd(fs,g); e4_st16(d+lo,(u16)v); if(fs->has64&&fs->desc_size>=64)e4_st16(d+hi,(u16)(v>>16)); }
#define E4GD_BBMP(fs,g) e4_gd64(fs,g,0x0,0x20)
#define E4GD_IBMP(fs,g) e4_gd64(fs,g,0x4,0x24)
#define E4GD_ITAB(fs,g) e4_gd64(fs,g,0x8,0x28)
#define E4GD_FREEB(fs,g) e4_gd32(fs,g,0xC,0x2C)
#define E4GD_FREEI(fs,g) e4_gd32(fs,g,0xE,0x2E)
#define E4GD_DIRS(fs,g) e4_gd32(fs,g,0x10,0x30)
#define E4GD_UNUSED(fs,g) e4_gd32(fs,g,0x1C,0x32)
static u16 e4_gd_flags(ext4_fs* fs,u32 g){ return e4_le16(e4_gd(fs,g)+0x12); }
static u16 e4_gd_csum_calc(ext4_fs* fs,u32 g){ u8* d=e4_gd(fs,g); u8 le[4]; e4_st32(le,g);
    if(fs->csum){ static const u8 z[2]={0,0}; u32 c=e4_crc32c(fs->csum_seed,le,4); c=e4_crc32c(c,d,0x1E); c=e4_crc32c(c,z,2); if(fs->desc_size>0x20)c=e4_crc32c(c,d+0x20,fs->desc_size-0x20); return (u16)(c&0xFFFF); }
    u16 c=e4_crc16(0xFFFF,fs->uuid,16); c=e4_crc16(c,le,4); c=e4_crc16(c,d,0x1E); if(fs->has64&&fs->desc_size>0x20)c=e4_crc16(c,d+0x20,fs->desc_size-0x20); return c; }
static void e4_gd_touch(ext4_fs* fs,u32 g){ if(fs->csum||fs->gdtcsum)e4_st16(e4_gd(fs,g)+0x1E,e4_gd_csum_calc(fs,g)); fs->gdt_dirty[(u64)g*fs->desc_size/fs->bsize]=1; fs->dirty=1; }

static u32 e4_ino_seed(ext4_fs* fs,u32 ino,u32 gen){ u8 le[4]; e4_st32(le,ino); u32 c=e4_crc32c(fs->csum_seed,le,4); e4_st32(le,gen); return e4_crc32c(c,le,4); }

static int e4_ino_loc(ext4_fs* fs,u32 ino,u64* blk,u32* off){ if(ino<1||ino>fs->inodes_count)return E4_ERR_INVAL; u32 g=(ino-1)/fs->inodes_per_group, i=(ino-1)%fs->inodes_per_group;
    u64 b=(u64)i*fs->inode_size; *blk=E4GD_ITAB(fs,g)+b/fs->bsize; *off=(u32)(b%fs->bsize); return 0; }

int ext4_iget(ext4_fs* fs,u32 ino,e4_inode* in){ u64 b; u32 o; if(e4_ino_loc(fs,ino,&b,&o))return E4_ERR_INVAL; u8* buf=(u8*)E4_ALLOC(fs->bsize); if(!buf)return E4_ERR_NOMEM;
    int r=ext4_bread(fs,b,buf); if(r){ E4_FREE(buf); return r; } u8* p=buf+o; u32 isz=fs->inode_size; e4_set(in,0,sizeof(*in)); e4_cpy(in->raw,p,isz>sizeof(in->raw)?sizeof(in->raw):isz);
    if(fs->csum){ u16 ex=isz>128?e4_le16(p+0x80):0; u32 want=e4_le16(p+0x7C); int hi=isz>128&&ex>=4; if(hi)want|=(u32)e4_le16(p+0x82)<<16; u8 s0=p[0x7C],s1=p[0x7D],s2=p[0x82],s3=p[0x83];
        p[0x7C]=p[0x7D]=0; if(hi){ p[0x82]=p[0x83]=0; } u32 c=e4_crc32c(e4_ino_seed(fs,ino,e4_le32(p+0x64)),p,isz); if(!hi)c&=0xFFFF; p[0x7C]=s0; p[0x7D]=s1; p[0x82]=s2; p[0x83]=s3;
        if(c!=want){ int z=1; for(u32 k=0;k<isz;k++) if(p[k]){ z=0; break; } if(!z){ E4_FREE(buf); return E4_ERR_CORRUPT; } } }
    E4_FREE(buf); p=in->raw; in->mode=e4_le16(p); in->uid=e4_le16(p+2); in->size=e4_le32(p+4)|((u64)e4_le32(p+0x6C)<<32); in->atime=e4_le32(p+8); in->ctime=e4_le32(p+0xC); in->mtime=e4_le32(p+0x10); in->dtime=e4_le32(p+0x14);
    in->gid=e4_le16(p+0x18); in->links=e4_le16(p+0x1A); in->blocks=e4_le32(p+0x1C)|((u64)e4_le16(p+0x74)<<32); in->flags=e4_le32(p+0x20); for(int k=0;k<15;k++)in->blk[k]=e4_le32(p+0x28+4*k); in->gen=e4_le32(p+0x64);
    return 0; }

int ext4_iput(ext4_fs* fs,u32 ino,e4_inode* in){ u64 b; u32 o; if(e4_ino_loc(fs,ino,&b,&o))return E4_ERR_INVAL; u8* p=in->raw; u32 isz=fs->inode_size;
    e4_st16(p,in->mode); e4_st16(p+2,in->uid); e4_st32(p+4,(u32)in->size); e4_st32(p+0x6C,(u32)(in->size>>32)); e4_st32(p+8,in->atime); e4_st32(p+0xC,in->ctime); e4_st32(p+0x10,in->mtime); e4_st32(p+0x14,in->dtime);
    e4_st16(p+0x18,in->gid); e4_st16(p+0x1A,in->links); e4_st32(p+0x1C,(u32)in->blocks); e4_st16(p+0x74,(u16)(in->blocks>>32)); e4_st32(p+0x20,in->flags); for(int k=0;k<15;k++)e4_st32(p+0x28+4*k,in->blk[k]); e4_st32(p+0x64,in->gen);
    u8* buf=(u8*)E4_ALLOC(fs->bsize); if(!buf)return E4_ERR_NOMEM; int r=ext4_bread(fs,b,buf); if(r){ E4_FREE(buf); return r; } u8* q=buf+o;
    if(in->fresh){ e4_set(q,0,isz); in->fresh=0; } e4_cpy(q,p,isz>sizeof(in->raw)?sizeof(in->raw):isz);
    if(fs->csum){ u16 ex=isz>128?e4_le16(q+0x80):0; int hi=isz>128&&ex>=4; q[0x7C]=q[0x7D]=0; if(hi){ q[0x82]=q[0x83]=0; } u32 c=e4_crc32c(e4_ino_seed(fs,ino,in->gen),q,isz); e4_st16(q+0x7C,(u16)c); if(hi)e4_st16(q+0x82,(u16)(c>>16)); }
    r=ext4_bwrite_meta(fs,b,buf); E4_FREE(buf); return r; }

static int e4_ext_csum_ok(ext4_fs* fs,u32 ino,u32 gen,u8* b){ if(!fs->csum)return 1; u32 n=12+12*(u32)e4_le16(b+4); if(n+4>fs->bsize)return 0; return e4_crc32c(e4_ino_seed(fs,ino,gen),b,n)==e4_le32(b+n); }
static void e4_ext_csum_set(ext4_fs* fs,u32 ino,u32 gen,u8* b){ if(!fs->csum)return; u32 n=12+12*(u32)e4_le16(b+4); if(n+4<=fs->bsize)e4_st32(b+n,e4_crc32c(e4_ino_seed(fs,ino,gen),b,n)); }

static int e4_ext_hdr_ok(ext4_fs* fs,const u8* n,int is_root,int want_depth){ u16 en=e4_le16(n+2), mx=e4_le16(n+4), dp=e4_le16(n+6);
    u32 cap=is_root?4:(fs->bsize-12)/12; if(e4_le16(n)!=E4_EXT_MAGIC||mx>cap||en>mx||dp>5)return 0; if(want_depth>=0&&dp!=want_depth)return 0; return 1; }
static int e4_ext_map(ext4_fs* fs,e4_inode* in,u64 lblk,u64* pblk,u32* run,int* unin){
    u8* node=(u8*)in->blk; u8* buf=0; int depth=0, guard=0, want=-1; if(unin)*unin=0;
    for(;;){ if(!e4_ext_hdr_ok(fs,node,node==(u8*)in->blk,want)){ if(buf)E4_FREE(buf); return E4_ERR_CORRUPT; } u16 n=e4_le16(node+2); depth=e4_le16(node+6);
        if(depth==0){ for(u16 i=0;i<n;i++){ u8* e=node+12+12*i; u32 lb=e4_le32(e); u16 len=e4_le16(e+4); int uninit=len>32768; if(uninit)len-=32768; u64 pb=((u64)e4_le16(e+6)<<32)|e4_le32(e+8);
                if(lblk>=lb&&lblk<(u64)lb+len){ *pblk=uninit?0:pb+(lblk-lb); if(unin)*unin=uninit; if(run)*run=(u32)((u64)lb+len-lblk); if(buf)E4_FREE(buf); return 0; } }
            *pblk=0; if(run)*run=1; if(buf)E4_FREE(buf); return 0; }
        int pick=-1; for(u16 i=0;i<n;i++){ if(e4_le32(node+12+12*i)<=lblk)pick=i; else break; }
        if(pick<0||++guard>8){ *pblk=0; if(buf)E4_FREE(buf); return 0; } u8* ix=node+12+12*pick; u64 child=((u64)e4_le16(ix+8)<<32)|e4_le32(ix+4);
        if(!buf){ buf=(u8*)E4_ALLOC(fs->bsize); if(!buf)return E4_ERR_NOMEM; } if(ext4_bread(fs,child,buf)){ E4_FREE(buf); return E4_ERR_IO; } node=buf; want=depth-1; }
}
static int e4_bm_map(ext4_fs* fs,e4_inode* in,u64 lblk,u64* pblk){
    u32 apb=fs->bsize/4; if(lblk<12){ *pblk=in->blk[lblk]; return 0; } lblk-=12; u32 root; int lvl;
    if(lblk<apb){ root=in->blk[12]; lvl=1; } else if((lblk-=apb)<(u64)apb*apb){ root=in->blk[13]; lvl=2; } else { lblk-=(u64)apb*apb; root=in->blk[14]; lvl=3; }
    u8* buf=(u8*)E4_ALLOC(fs->bsize); if(!buf)return E4_ERR_NOMEM; u32 cur=root;
    for(int l=lvl;l>0&&cur;l--){ u64 div=1; for(int k=1;k<l;k++)div*=apb; if(ext4_bread(fs,cur,buf)){ E4_FREE(buf); return E4_ERR_IO; } cur=e4_le32(buf+4*((lblk/div)%apb)); }
    E4_FREE(buf); *pblk=cur; return 0; }
static int e4_bmap_u(ext4_fs* fs,e4_inode* in,u64 lblk,u64* pblk,int* unin){ if(unin)*unin=0; if(in->flags&E4_IF_EXTENTS)return e4_ext_map(fs,in,lblk,pblk,0,unin); return e4_bm_map(fs,in,lblk,pblk); }
int ext4_bmap(ext4_fs* fs,e4_inode* in,u64 lblk,u64* pblk){ return e4_bmap_u(fs,in,lblk,pblk,0); }

static int e4_bitmap_csum_set(ext4_fs* fs,u32 g,u8* bm,int inode){ if(!fs->csum)return 0; u32 n=inode?fs->inodes_per_group/8:fs->blocks_per_group/8; u32 c=e4_crc32c(fs->csum_seed,bm,n);
    u8* d=e4_gd(fs,g); int lo=inode?0x1A:0x18, hi=inode?0x3A:0x38; e4_st16(d+lo,(u16)c); if(fs->desc_size>=64)e4_st16(d+hi,(u16)(c>>16)); return 0; }
static u64 e4_grp_first(ext4_fs* fs,u32 g){ return fs->first_data_block+(u64)g*fs->blocks_per_group; }
static u32 e4_grp_nblocks(ext4_fs* fs,u32 g){ u64 f=e4_grp_first(fs,g); u64 n=fs->blocks_count-f; return n>fs->blocks_per_group?fs->blocks_per_group:(u32)n; }

static int e4_is_meta(ext4_fs* fs,u64 blk,u32 n);
u64 ext4_gdt_loc(ext4_fs* fs,u32 i);
static int e4_balloc(ext4_fs* fs,u64 goal,u32 want,u64* out,u32* got){
    if(!want)want=1; u8* bm=(u8*)E4_ALLOC(fs->bsize); if(!bm)return E4_ERR_NOMEM;
    u32 g0=goal>=fs->first_data_block?(u32)((goal-fs->first_data_block)/fs->blocks_per_group):0; if(g0>=fs->ngroups)g0=0;
    for(u32 k=0;k<fs->ngroups;k++){ u32 g=(g0+k)%fs->ngroups; if(!E4GD_FREEB(fs,g)||(e4_gd_flags(fs,g)&E4_BG_BLOCK_UNINIT))continue;
        if(ext4_bread(fs,E4GD_BBMP(fs,g),bm)){ E4_FREE(bm); return E4_ERR_IO; } u32 nb=e4_grp_nblocks(fs,g); u32 st=0;
        if(g==g0&&goal>=e4_grp_first(fs,g))st=(u32)(goal-e4_grp_first(fs,g)); if(st>=nb)st=0;
        for(int pass=0;pass<2;pass++){ u32 a=pass?0:st, z=pass?st:nb;
            for(u32 i=a;i<z;i++){ if(bm[i>>3]&(1u<<(i&7)))continue; u64 gf=e4_grp_first(fs,g); if(e4_is_meta(fs,gf+i,1)){ E4_FREE(bm); fs->ro=1; return E4_ERR_CORRUPT; } u32 n=0; while(n<want&&i+n<nb&&!(bm[(i+n)>>3]&(1u<<((i+n)&7)))&&(!n||!e4_is_meta(fs,gf+i+n,1))){ bm[(i+n)>>3]|=(u8)(1u<<((i+n)&7)); n++; }
                e4_bitmap_csum_set(fs,g,bm,0); int r=ext4_bwrite_meta(fs,E4GD_BBMP(fs,g),bm); E4_FREE(bm); if(r)return r;
                e4_gd32s(fs,g,0xC,0x2C,E4GD_FREEB(fs,g)-n); e4_gd_touch(fs,g); fs->free_blocks-=n; *out=e4_grp_first(fs,g)+i; *got=n; fs->last_alloc_group=g; return 0; } } }
    E4_FREE(bm); return E4_ERR_NOSPC; }
static int e4_pw(u32 g,u32 b){ while(g%b==0)g/=b; return g==1; }
int ext4_grp_has_sb(ext4_fs* fs,u32 g){ if(!g)return 1; if(fs->feat_compat&E4_COMPAT_SPARSE_SUPER2)return g==e4_le32(fs->sb+0x24C)||g==e4_le32(fs->sb+0x250);
    if(g==1||!(fs->feat_rocompat&E4_RO_SPARSE_SUPER))return 1; if(!(g&1))return 0; return e4_pw(g,3)||e4_pw(g,5)||e4_pw(g,7); }
u32 ext4_grp_sb_len(ext4_fs* fs,u32 g){ u32 hs=(u32)ext4_grp_has_sb(fs,g), per=fs->bsize/fs->desc_size; int mbg=!!(fs->feat_incompat&E4_INCOMPAT_META_BG); u32 fmb=e4_le32(fs->sb+0x104);
    if(!mbg||g/per<fmb)return hs?1+(mbg?fmb:fs->gdt_blocks+e4_le16(fs->sb+0xCE)):0; u32 i=g%per; return hs+(i==0||i==1||i==per-1); }
u64 ext4_gdt_loc(ext4_fs* fs,u32 i){ u32 per=fs->bsize/fs->desc_size, fmb=e4_le32(fs->sb+0x104); if(!(fs->feat_incompat&E4_INCOMPAT_META_BG)||i<fmb)return fs->first_data_block+1+i;
    u32 g=i*per; return e4_grp_first(fs,g)+(u32)ext4_grp_has_sb(fs,g); }
static int e4_is_meta(ext4_fs* fs,u64 blk,u32 n){ u64 e=blk+n; if(blk<fs->first_data_block||e<blk||e>fs->blocks_count)return 1;
    u64 g0=(blk-fs->first_data_block)/fs->blocks_per_group, g1=(e-1-fs->first_data_block)/fs->blocks_per_group;
    for(u64 g=g0;g<=g1&&g<fs->ngroups;g++){ u64 s=e4_grp_first(fs,(u32)g); u32 l=ext4_grp_sb_len(fs,(u32)g); if(l&&s<e&&s+l>blk)return 1; }
    u64 itb=((u64)fs->inodes_per_group*fs->inode_size+fs->bsize-1)/fs->bsize;
    for(u32 g=0;g<fs->ngroups;g++){ u64 a=E4GD_BBMP(fs,g), b=E4GD_IBMP(fs,g), t=E4GD_ITAB(fs,g); if((a>=blk&&a<e)||(b>=blk&&b<e)||(t<e&&t+itb>blk))return 1; }
    return 0; }
static int e4_bfree(ext4_fs* fs,u64 blk,u32 n){
    if(n&&(blk+n<blk||e4_is_meta(fs,blk,n))){ fs->ro=1; return E4_ERR_CORRUPT; }
    u8* bm=(u8*)E4_ALLOC(fs->bsize); if(!bm)return E4_ERR_NOMEM;
    while(n){ if(blk<fs->first_data_block||blk>=fs->blocks_count){ E4_FREE(bm); return E4_ERR_CORRUPT; } u32 g=(u32)((blk-fs->first_data_block)/fs->blocks_per_group); u32 i=(u32)(blk-e4_grp_first(fs,g));
        if(ext4_bread(fs,E4GD_BBMP(fs,g),bm)){ E4_FREE(bm); return E4_ERR_IO; } u32 c=0; while(n&&i<fs->blocks_per_group){ if(bm[i>>3]&(1u<<(i&7))){ bm[i>>3]&=(u8)~(1u<<(i&7)); c++; } i++; blk++; n--; }
        e4_bitmap_csum_set(fs,g,bm,0); int r=ext4_bwrite_meta(fs,E4GD_BBMP(fs,g),bm); if(r){ E4_FREE(bm); return r; } e4_gd32s(fs,g,0xC,0x2C,E4GD_FREEB(fs,g)+c); e4_gd_touch(fs,g); fs->free_blocks+=c; }
    E4_FREE(bm); return 0; }

static int e4_ialloc(ext4_fs* fs,u32 pg,int isdir,u32* out){
    u8* bm=(u8*)E4_ALLOC(fs->bsize); if(!bm)return E4_ERR_NOMEM;
    for(u32 k=0;k<fs->ngroups;k++){ u32 g=(pg+k)%fs->ngroups; if(!E4GD_FREEI(fs,g)||(e4_gd_flags(fs,g)&E4_BG_INODE_UNINIT))continue;
        if(ext4_bread(fs,E4GD_IBMP(fs,g),bm)){ E4_FREE(bm); return E4_ERR_IO; }
        for(u32 i=0;i<fs->inodes_per_group;i++){ u32 ino=g*fs->inodes_per_group+i+1; if(ino<11&&ino!=0)continue; if(ino<e4_le32(fs->sb+0x54)&&e4_le32(fs->sb+0x4C))continue; if(bm[i>>3]&(1u<<(i&7)))continue;
            bm[i>>3]|=(u8)(1u<<(i&7)); e4_bitmap_csum_set(fs,g,bm,1); int r=ext4_bwrite_meta(fs,E4GD_IBMP(fs,g),bm); E4_FREE(bm); if(r)return r;
            e4_gd32s(fs,g,0xE,0x2E,E4GD_FREEI(fs,g)-1); if(isdir)e4_gd32s(fs,g,0x10,0x30,E4GD_DIRS(fs,g)+1);
            if(fs->csum||fs->gdtcsum){ u32 un=E4GD_UNUSED(fs,g); if(i>=fs->inodes_per_group-un)e4_gd32s(fs,g,0x1C,0x32,fs->inodes_per_group-i-1); }
            e4_gd_touch(fs,g); fs->free_inodes--; *out=ino; return 0; } }
    E4_FREE(bm); return E4_ERR_NOSPC; }
static int e4_ifree(ext4_fs* fs,u32 ino,int isdir){ u32 g=(ino-1)/fs->inodes_per_group, i=(ino-1)%fs->inodes_per_group; u8* bm=(u8*)E4_ALLOC(fs->bsize); if(!bm)return E4_ERR_NOMEM;
    int r=ext4_bread(fs,E4GD_IBMP(fs,g),bm); if(!r){ bm[i>>3]&=(u8)~(1u<<(i&7)); e4_bitmap_csum_set(fs,g,bm,1); r=ext4_bwrite_meta(fs,E4GD_IBMP(fs,g),bm); } E4_FREE(bm); if(r)return r;
    e4_gd32s(fs,g,0xE,0x2E,E4GD_FREEI(fs,g)+1); if(isdir&&E4GD_DIRS(fs,g))e4_gd32s(fs,g,0x10,0x30,E4GD_DIRS(fs,g)-1); e4_gd_touch(fs,g); fs->free_inodes++; return 0; }

static void e4_blocks_add(ext4_fs* fs,e4_inode* in,long d){ in->blocks=(u64)((long)in->blocks+d*(long)fs->spb); }

static int e4_ext_leaf_insert(u8* node,u32 lblk,u64 pblk,u32 len){ u16 n=e4_le16(node+2), mx=e4_le16(node+4); if(n>=mx)return -1; u16 at=0; while(at<n&&e4_le32(node+12+12*at)<lblk)at++;
    for(int i=n;i>at;i--)e4_cpy(node+12+12*i,node+12+12*(i-1),12); u8* e=node+12+12*at; e4_st32(e,lblk); e4_st16(e+4,(u16)len); e4_st16(e+6,(u16)(pblk>>32)); e4_st32(e+8,(u32)pblk); e4_st16(node+2,(u16)(n+1)); return 0; }
static int e4_ext_leaf_extend(u8* node,u32 lblk,u64 pblk){ u16 n=e4_le16(node+2); for(u16 i=0;i<n;i++){ u8* e=node+12+12*i; u32 lb=e4_le32(e); u16 len=e4_le16(e+4); u64 pb=((u64)e4_le16(e+6)<<32)|e4_le32(e+8);
        if(len<32768&&lb+len==lblk&&pb+len==pblk&&(i+1==n||e4_le32(e+12)>lblk)){ e4_st16(e+4,(u16)(len+1)); return 0; } } return -1; }

static int e4_ext_add(ext4_fs* fs,u32 ino,e4_inode* in,u32 lblk,u64 pblk){
    u8* root=(u8*)in->blk; if(!e4_ext_hdr_ok(fs,root,1,-1))return E4_ERR_CORRUPT; u16 depth=e4_le16(root+6);
    if(depth==0){ if(!e4_ext_leaf_extend(root,lblk,pblk)||!e4_ext_leaf_insert(root,lblk,pblk,1))return 0;
        u64 nb; u32 got; int r=e4_balloc(fs,pblk,1,&nb,&got); if(r)return r; u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM;
        e4_st16(b,E4_EXT_MAGIC); e4_st16(b+2,e4_le16(root+2)); e4_st16(b+4,(u16)((fs->bsize-12)/12)); e4_st16(b+6,0); e4_cpy(b+12,root+12,12u*e4_le16(root+2));
        e4_ext_leaf_insert(b,lblk,pblk,1); e4_ext_csum_set(fs,ino,in->gen,b); r=ext4_bwrite_meta(fs,nb,b); E4_FREE(b); if(r)return r; e4_blocks_add(fs,in,1);
        e4_set(root+12,0,48); e4_st16(root+2,1); e4_st16(root+6,1); e4_st32(root+12,0); e4_st32(root+16,(u32)nb); e4_st16(root+20,(u16)(nb>>32)); return 0; }
    if(depth!=1)return E4_ERR_NOSPC;
    u16 n=e4_le16(root+2); int pick=0; for(u16 i=0;i<n;i++) if(e4_le32(root+12+12*i)<=lblk)pick=i;
    u8* ix=root+12+12*pick; u64 leaf=((u64)e4_le16(ix+8)<<32)|e4_le32(ix+4); u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM;
    int r=ext4_bread(fs,leaf,b); if(r){ E4_FREE(b); return r; } if(!e4_ext_hdr_ok(fs,b,0,0)||!e4_ext_csum_ok(fs,ino,in->gen,b)){ E4_FREE(b); return E4_ERR_CORRUPT; }
    if(!e4_ext_leaf_extend(b,lblk,pblk)||!e4_ext_leaf_insert(b,lblk,pblk,1)){ e4_ext_csum_set(fs,ino,in->gen,b); r=ext4_bwrite_meta(fs,leaf,b); E4_FREE(b); return r; }
    if(pick!=n-1||n>=4){ E4_FREE(b); return E4_ERR_NOSPC; }
    u64 nb; u32 got; r=e4_balloc(fs,pblk,1,&nb,&got); if(r){ E4_FREE(b); return r; } e4_set(b,0,fs->bsize); e4_st16(b,E4_EXT_MAGIC); e4_st16(b+4,(u16)((fs->bsize-12)/12));
    e4_ext_leaf_insert(b,lblk,pblk,1); e4_ext_csum_set(fs,ino,in->gen,b); r=ext4_bwrite_meta(fs,nb,b); E4_FREE(b); if(r)return r; e4_blocks_add(fs,in,1);
    u8* ni=root+12+12*n; e4_st32(ni,lblk); e4_st32(ni+4,(u32)nb); e4_st16(ni+8,(u16)(nb>>32)); e4_st16(ni+10,0); e4_st16(root+2,(u16)(n+1)); return 0; }

static int e4_get_block(ext4_fs* fs,u32 ino,e4_inode* in,u64 lblk,int create,u64* pblk){
    int un=0; int r=e4_bmap_u(fs,in,lblk,pblk,&un); if(r||*pblk||!create)return r; if(un)return E4_ERR_ROFS; if(!(in->flags&E4_IF_EXTENTS))return E4_ERR_ROFS; if(lblk>0xFFFFFFFEull)return E4_ERR_INVAL;
    u64 goal=0; if(lblk>0){ u64 pp=0; ext4_bmap(fs,in,lblk-1,&pp); if(pp)goal=pp+1; } if(!goal){ u32 g=(ino-1)/fs->inodes_per_group; goal=e4_grp_first(fs,g); }
    u64 nb; u32 got; r=e4_balloc(fs,goal,1,&nb,&got); if(r)return r; r=e4_ext_add(fs,ino,in,(u32)lblk,nb); if(r){ e4_bfree(fs,nb,1); return r; } e4_blocks_add(fs,in,1); *pblk=nb; return 0; }

static int e4_ext_free_node(ext4_fs* fs,u32 ino,e4_inode* in,u8* node,u64 from,int* empty,int left){
    if(left<=0)return E4_ERR_CORRUPT; u16 n=e4_le16(node+2), depth=e4_le16(node+6); u16 keep=0;
    if(depth==0){ for(u16 i=0;i<n;i++){ u8* e=node+12+12*i; u32 lb=e4_le32(e); u16 len=e4_le16(e+4); int un=len>32768; if(un)len-=32768; u64 pb=((u64)e4_le16(e+6)<<32)|e4_le32(e+8);
            if((u64)lb+len<=from){ if(keep!=i)e4_cpy(node+12+12*keep,e,12); keep++; continue; }
            u32 cut=from>lb?(u32)(from-lb):0; int r=e4_bfree(fs,pb+cut,len-cut); if(r)return r; e4_blocks_add(fs,in,-(long)(len-cut));
            if(cut){ e4_st16(e+4,(u16)(un?cut+32768:cut)); if(keep!=i)e4_cpy(node+12+12*keep,e,12); keep++; } } }
    else { u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM;
        for(u16 i=0;i<n;i++){ u8* ix=node+12+12*i; u64 ch=((u64)e4_le16(ix+8)<<32)|e4_le32(ix+4); u32 lb=e4_le32(ix); u32 nxt=i+1<n?e4_le32(ix+12):0xFFFFFFFFu;
            if(nxt<=from){ if(keep!=i)e4_cpy(node+12+12*keep,ix,12); keep++; continue; }
            int r=ext4_bread(fs,ch,b); if(r){ E4_FREE(b); return r; } if(!e4_ext_hdr_ok(fs,b,0,depth-1)||!e4_ext_csum_ok(fs,ino,in->gen,b)){ E4_FREE(b); return E4_ERR_CORRUPT; } int em=0; r=e4_ext_free_node(fs,ino,in,b,from,&em,left-1); if(r){ E4_FREE(b); return r; }
            if(em||lb>=from){ r=e4_bfree(fs,ch,1); e4_blocks_add(fs,in,-1); } else { e4_ext_csum_set(fs,ino,in->gen,b); r=ext4_bwrite_meta(fs,ch,b); if(keep!=i)e4_cpy(node+12+12*keep,ix,12); keep++; }
            if(r){ E4_FREE(b); return r; } }
        E4_FREE(b); }
    e4_st16(node+2,keep); *empty=keep==0; return 0; }
static int e4_bm_free_tree(ext4_fs* fs,e4_inode* in,u32 blk,int lvl){ if(!blk)return 0; if(lvl>0){ u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM; int r=ext4_bread(fs,blk,b);
        for(u32 i=0;!r&&i<fs->bsize/4;i++)r=e4_bm_free_tree(fs,in,e4_le32(b+4*i),lvl-1); E4_FREE(b); if(r)return r; } e4_blocks_add(fs,in,-1); return e4_bfree(fs,blk,1); }

static int e4_trunc_in(ext4_fs* fs,u32 ino,u64 size,int any){ if(fs->ro)return E4_ERR_ROFS; e4_inode in; int r=ext4_iget(fs,ino,&in); if(r)return r;
    u16 ft=in.mode&E4_S_IFMT; if(!any&&ft!=E4_S_IFREG)return ft==E4_S_IFDIR?E4_ERR_ISDIR:E4_ERR_INVAL;
    u64 from=(size+fs->bsize-1)/fs->bsize;
    if(ft!=E4_S_IFREG&&ft!=E4_S_IFDIR&&ft!=E4_S_IFLNK){ if(size)return E4_ERR_INVAL; }
    else if(in.flags&E4_IF_EXTENTS){ if(!e4_ext_hdr_ok(fs,(u8*)in.blk,1,-1))return E4_ERR_CORRUPT; int em=0; r=e4_ext_free_node(fs,ino,&in,(u8*)in.blk,from,&em,6); if(r)return r; if(em&&e4_le16((u8*)in.blk+6)){ e4_st16((u8*)in.blk+6,0); e4_st16((u8*)in.blk+4,4); } }
    else if((in.mode&E4_S_IFMT)!=E4_S_IFLNK||in.size>=60){ if(from)return E4_ERR_ROFS; for(int k=0;k<12;k++){ if(in.blk[k]){ e4_bfree(fs,in.blk[k],1); e4_blocks_add(fs,&in,-1); in.blk[k]=0; } }
        for(int k=12;k<15;k++){ r=e4_bm_free_tree(fs,&in,in.blk[k],k-11); if(r)return r; in.blk[k]=0; } }
    if((ft==E4_S_IFREG||ft==E4_S_IFDIR)&&size<in.size&&size%fs->bsize){ u64 pb=0; if(!ext4_bmap(fs,&in,size/fs->bsize,&pb)&&pb){ u8* b=(u8*)E4_ALLOC(fs->bsize); if(b){ if(!ext4_bread(fs,pb,b)){ e4_set(b+size%fs->bsize,0,fs->bsize-size%fs->bsize); ext4_bwrite(fs,pb,b); } E4_FREE(b); } } }
    in.size=size; in.mtime=in.ctime=e4_now(); return ext4_iput(fs,ino,&in); }

static int e4_op_begin(ext4_fs* fs);
static int e4_op_end(ext4_fs* fs,int rc);
int ext4_truncate(ext4_fs* fs,u32 ino,u64 size){ int r=e4_op_begin(fs); if(r)return r; return e4_op_end(fs,e4_trunc_in(fs,ino,size,0)); }
static int e4_flush_meta(ext4_fs* fs){ if(fs->ro)return 0; int r=0; for(u32 i=0;i<fs->gdt_blocks;i++) if(fs->gdt_dirty[i]){ u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM;
        u64 at=ext4_gdt_loc(fs,i); int rr=ext4_bread(fs,at,b); if(!rr){ u64 o=(u64)i*fs->bsize; u64 L=(u64)fs->ngroups*fs->desc_size-o; if(L>fs->bsize)L=fs->bsize; e4_cpy(b,fs->gdt+o,L); rr=ext4_bwrite_meta(fs,at,b); } E4_FREE(b); if(rr)r=rr; else fs->gdt_dirty[i]=0; }
    if(fs->dirty){ e4_st32(fs->sb+0xC,(u32)fs->free_blocks); if(fs->has64)e4_st32(fs->sb+0x158,(u32)(fs->free_blocks>>32)); e4_st32(fs->sb+0x10,fs->free_inodes); e4_st32(fs->sb+0x30,e4_now()); e4_sb_csum_set(fs);
        u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM; u64 sbb=fs->bsize==1024?1:0; u32 so=fs->bsize==1024?0:1024; int rr=ext4_bread(fs,sbb,b); if(!rr){ e4_cpy(b+so,fs->sb,1024); rr=ext4_bwrite_meta(fs,sbb,b); }
        E4_FREE(b); if(rr)r=rr; else fs->dirty=0; }
    return r; }
static int e4_op_begin(ext4_fs* fs){ if(!fs->mounted)return E4_ERR_INVAL; if(fs->ro)return E4_ERR_ROFS; return ext4_jnl_begin(fs); }
static int e4_op_end(ext4_fs* fs,int rc){ if(fs->jabort){ fs->jabort=0; ext4_jnl_abort(fs); if(ext4_jnl_reload(fs))fs->ro=1; return E4_ERR_NOSPC; } if(rc==E4_ERR_CORRUPT&&fs->jnl&&fs->in_txn){ ext4_jnl_abort(fs); fs->ro=1; return rc; } int r=e4_flush_meta(fs); int j=ext4_jnl_end(fs); return rc?rc:r?r:j; }

static u16 e4_dirent_need(u32 nl){ return (u16)((8+nl+3)&~3u); }
static int e4_dir_tail(ext4_fs* fs){ return fs->csum?12:0; }
static int e4_dir_tail_ok(ext4_fs* fs,const u8* b){ if(!fs->csum)return 1; const u8* t=b+fs->bsize-12; return !e4_le32(t)&&e4_le16(t+4)==12&&!t[6]&&t[7]==E4_FT_CSUM; }
static void e4_dir_csum_set(ext4_fs* fs,u32 ino,u32 gen,u8* b){ if(!fs->csum)return; u8* t=b+fs->bsize-12; e4_set(t,0,12); e4_st16(t+4,12); t[7]=E4_FT_CSUM; e4_st32(t+8,e4_crc32c(e4_ino_seed(fs,ino,gen),b,fs->bsize-12)); }

typedef int (*e4_dir_cb)(ext4_fs*,u8* blk,u8* de,u8* prev,u64 lblk,void* ctx);
static int e4_dir_iter(ext4_fs* fs,e4_inode* d,e4_dir_cb cb,void* ctx){
    if((d->mode&E4_S_IFMT)!=E4_S_IFDIR)return E4_ERR_NOTDIR; if(d->flags&E4_IF_INLINE_DATA)return E4_ERR_INVAL;
    u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM; u64 nbk=(d->size+fs->bsize-1)/fs->bsize;
    for(u64 l=0;l<nbk;l++){ u64 pb=0; int r=ext4_bmap(fs,d,l,&pb); if(r){ E4_FREE(b); return r; } if(!pb)continue; if(ext4_bread(fs,pb,b)){ E4_FREE(b); return E4_ERR_IO; }
        u32 o=0; u8* prev=0; while(o+8<=fs->bsize){ u8* de=b+o; u16 rl=e4_le16(de+4); if(rl<8||o+rl>fs->bsize||(rl&3))break; if(e4_le32(de)&&8u+de[6]>rl)break; if(de[7]!=E4_FT_CSUM||e4_le32(de)){ r=cb(fs,b,de,prev,l,ctx); if(r){ E4_FREE(b); return r>0?0:r; } } prev=de; o+=rl; } }
    E4_FREE(b); return E4_ERR_NOENT; }

typedef struct { const char* nm; u32 nl; u32 ino; u8 ft; } e4_find_ctx;
static int e4_find_cb(ext4_fs* fs,u8* b,u8* de,u8* prev,u64 l,void* c){ (void)fs; (void)b; (void)prev; (void)l; e4_find_ctx* f=(e4_find_ctx*)c; u32 ino=e4_le32(de); if(!ino||de[6]!=f->nl)return 0; for(u32 i=0;i<f->nl;i++) if(de[8+i]!=(u8)f->nm[i])return 0; f->ino=ino; f->ft=de[7]; return 1; }
static int e4_dir_find(ext4_fs* fs,e4_inode* d,const char* nm,u32 nl,u32* ino){ e4_find_ctx f={nm,nl,0,0}; int r=e4_dir_iter(fs,d,e4_find_cb,&f); if(r&&r!=E4_ERR_NOENT)return r; if(!f.ino)return E4_ERR_NOENT; *ino=f.ino; return 0; }

static int e4_path_walk(ext4_fs* fs,const char* p,u32* ino,int parent,const char** leaf,u32* leaflen){
    u32 cur=E4_ROOT_INO; while(*p=='/')p++; int hops=0;
    for(;;){ const char* s=p; while(*p&&*p!='/')p++; u32 nl=(u32)(p-s); const char* q=p; while(*q=='/')q++;
        if(!nl){ *ino=cur; if(leaf){ *leaf=s; *leaflen=0; } return 0; } if(nl>E4_NAME_MAX)return E4_ERR_NAMETOOLONG;
        if(parent&&!*q){ *ino=cur; *leaf=s; *leaflen=nl; return 0; }
        e4_inode d; int r=ext4_iget(fs,cur,&d); if(r)return r; u32 nx; r=e4_dir_find(fs,&d,s,nl,&nx); if(r)return r; cur=nx; p=q; if(++hops>256)return E4_ERR_INVAL; }
}
int ext4_lookup(ext4_fs* fs,const char* path,u32* ino){ if(!fs->mounted)return E4_ERR_INVAL; return e4_path_walk(fs,path,ino,0,0,0); }
int ext4_stat(ext4_fs* fs,const char* path,e4_inode* in,u32* ino){ u32 i; int r=ext4_lookup(fs,path,&i); if(r)return r; if(ino)*ino=i; return ext4_iget(fs,i,in); }

long ext4_pread(ext4_fs* fs,u32 ino,u64 off,void* buf,long len){ e4_inode in; int r=ext4_iget(fs,ino,&in); if(r)return r; if(off>=in.size||len<=0)return 0; if((u64)len>in.size-off)len=(long)(in.size-off);
    if((in.mode&E4_S_IFMT)==E4_S_IFLNK&&in.size<60&&!(in.flags&E4_IF_EXTENTS)){ e4_cpy(buf,(u8*)in.blk+off,(unsigned long)len); return len; }
    if(in.flags&E4_IF_INLINE_DATA){ if(off+len>60)len=off<60?(long)(60-off):0; e4_cpy(buf,(u8*)in.blk+off,(unsigned long)len); return len; }
    u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM; long done=0; u8* o=(u8*)buf;
    while(done<len){ u64 p=off+done; u64 lb=p/fs->bsize; u32 bo=(u32)(p%fs->bsize); u32 c=fs->bsize-bo; if((long)c>len-done)c=(u32)(len-done); u64 pb=0;
        r=ext4_bmap(fs,&in,lb,&pb); if(r){ E4_FREE(b); return r; } if(!pb)e4_set(o+done,0,c); else { if(ext4_bread(fs,pb,b)){ E4_FREE(b); return E4_ERR_IO; } e4_cpy(o+done,b+bo,c); } done+=c; }
    E4_FREE(b); return done; }

static long e4_pwrite_in(ext4_fs* fs,u32 ino,u64 off,const void* buf,long len){ e4_inode in; int r=ext4_iget(fs,ino,&in); if(r)return r; if((in.mode&E4_S_IFMT)!=E4_S_IFREG)return (in.mode&E4_S_IFMT)==E4_S_IFDIR?E4_ERR_ISDIR:E4_ERR_INVAL;
    if(in.flags&E4_IF_INLINE_DATA)return E4_ERR_ROFS; u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM; long done=0; const u8* s=(const u8*)buf;
    while(done<len){ u64 p=off+done; u64 lb=p/fs->bsize; u32 bo=(u32)(p%fs->bsize); u32 c=fs->bsize-bo; if((long)c>len-done)c=(u32)(len-done); u64 pb=0; u64 had=0;
        ext4_bmap(fs,&in,lb,&had); r=e4_get_block(fs,ino,&in,lb,1,&pb); if(r)break;
        if(c<fs->bsize){ if(had){ if(ext4_bread(fs,pb,b)){ r=E4_ERR_IO; break; } } else e4_set(b,0,fs->bsize); } e4_cpy(b+bo,s+done,c); if(ext4_bwrite(fs,pb,b)){ r=E4_ERR_IO; break; } done+=c; if(p+c>in.size)in.size=p+c; }
    E4_FREE(b); in.mtime=in.ctime=e4_now(); int w=ext4_iput(fs,ino,&in); if(done)return done; return r?r:w; }
long ext4_pwrite(ext4_fs* fs,u32 ino,u64 off,const void* buf,long len){ int r=e4_op_begin(fs); if(r)return r; long n=e4_pwrite_in(fs,ino,off,buf,len); int e=e4_op_end(fs,n<0?(int)n:0); return n<0?n:e?e:n; }

typedef struct { u32 need; u64 lblk; u32 off; int found; } e4_slot_ctx;
static int e4_dir_add(ext4_fs* fs,u32 dino,e4_inode* d,const char* nm,u32 nl,u32 ino,u8 ft){
    if(d->flags&E4_IF_INDEX){ if(fs->csum)return E4_ERR_ROFS; d->flags&=~(u32)E4_IF_INDEX; }
    u16 need=e4_dirent_need(nl); u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM; u64 nbk=(d->size+fs->bsize-1)/fs->bsize; u32 lim=fs->bsize-(u32)e4_dir_tail(fs); int r;
    for(u64 l=0;l<nbk;l++){ u64 pb=0; r=ext4_bmap(fs,d,l,&pb); if(r||!pb)continue; if(ext4_bread(fs,pb,b)){ E4_FREE(b); return E4_ERR_IO; } if(!e4_dir_tail_ok(fs,b)){ E4_FREE(b); return E4_ERR_ROFS; }
        u32 o=0; while(o+8<=lim){ u8* de=b+o; u16 rl=e4_le16(de+4); if(rl<8||o+rl>lim||(rl&3))break; if(e4_le32(de)&&8u+de[6]>rl)break; u32 used=e4_le32(de)?e4_dirent_need(de[6]):0;
            if(rl>=used+need){ u8* ne=de+used; if(used)e4_st16(de+4,(u16)used); e4_st32(ne,ino); e4_st16(ne+4,(u16)(rl-used)); ne[6]=(u8)nl; ne[7]=fs->filetype?ft:0; e4_cpy(ne+8,nm,nl);
                e4_dir_csum_set(fs,dino,d->gen,b); r=ext4_bwrite_meta(fs,pb,b); E4_FREE(b); if(r)return r; d->mtime=d->ctime=e4_now(); return ext4_iput(fs,dino,d); }
            o+=rl; } }
    u64 pb; r=e4_get_block(fs,dino,d,nbk,1,&pb); if(r){ E4_FREE(b); return r; } e4_set(b,0,fs->bsize); e4_st32(b,ino); e4_st16(b+4,(u16)lim); b[6]=(u8)nl; b[7]=fs->filetype?ft:0; e4_cpy(b+8,nm,nl);
    e4_dir_csum_set(fs,dino,d->gen,b); r=ext4_bwrite_meta(fs,pb,b); E4_FREE(b); if(r)return r; d->size=(nbk+1)*fs->bsize; d->mtime=d->ctime=e4_now(); return ext4_iput(fs,dino,d); }

typedef struct { const char* nm; u32 nl; u32 dino; u32 gen; int done; int rc; } e4_del_ctx;
static int e4_del_cb(ext4_fs* fs,u8* b,u8* de,u8* prev,u64 l,void* c){ e4_del_ctx* x=(e4_del_ctx*)c; if(!e4_le32(de)||de[6]!=x->nl)return 0; for(u32 i=0;i<x->nl;i++) if(de[8+i]!=(u8)x->nm[i])return 0;
    if(!e4_dir_tail_ok(fs,b)){ x->rc=E4_ERR_ROFS; return 1; }
    if(prev)e4_st16(prev+4,(u16)(e4_le16(prev+4)+e4_le16(de+4))); else e4_st32(de,0);
    e4_inode d; int r=ext4_iget(fs,x->dino,&d); if(r){ x->rc=r; return 1; } u64 pb=0; r=ext4_bmap(fs,&d,l,&pb); if(r||!pb){ x->rc=r?r:E4_ERR_CORRUPT; return 1; }
    e4_dir_csum_set(fs,x->dino,x->gen,b); x->rc=ext4_bwrite_meta(fs,pb,b); x->done=1; return 1; }
static int e4_dir_del(ext4_fs* fs,u32 dino,e4_inode* d,const char* nm,u32 nl){ if((d->flags&E4_IF_INDEX)&&fs->csum)return E4_ERR_ROFS; e4_del_ctx x={nm,nl,dino,d->gen,0,0}; int r=e4_dir_iter(fs,d,e4_del_cb,&x); if(x.rc)return x.rc; if(!x.done)return r?r:E4_ERR_NOENT;
    e4_inode d2; r=ext4_iget(fs,dino,&d2); if(r)return r; if(d2.flags&E4_IF_INDEX)d2.flags&=~(u32)E4_IF_INDEX; d2.mtime=d2.ctime=e4_now(); *d=d2; return ext4_iput(fs,dino,&d2); }

static int e4_new_inode(ext4_fs* fs,u32 pino,u16 mode,u32* out,e4_inode* in){ u32 ino; int isdir=(mode&E4_S_IFMT)==E4_S_IFDIR; int r=e4_ialloc(fs,(pino-1)/fs->inodes_per_group,isdir,&ino); if(r)return r;
    e4_set(in,0,sizeof(*in)); in->fresh=1; in->mode=mode; in->links=isdir?2:1; u32 t=e4_now(); in->atime=in->ctime=in->mtime=in->crtime=t; in->gen=e4_crc32c(t,&ino,4);
    if(fs->inode_size>128){ u16 ex=(u16)(fs->inode_size-128<32?fs->inode_size-128:32); e4_st16(in->raw+0x80,ex); if(ex>=0x18)e4_st32(in->raw+0x90,t); }
    if(fs->extents){ in->flags=E4_IF_EXTENTS; u8* h=(u8*)in->blk; e4_st16(h,E4_EXT_MAGIC); e4_st16(h+2,0); e4_st16(h+4,4); e4_st16(h+6,0); }
    *out=ino; return 0; }

static int e4_create_in(ext4_fs* fs,const char* path,u16 mode,u32* out){ u32 pino; const char* lf; u32 nl; int r=e4_path_walk(fs,path,&pino,1,&lf,&nl); if(r)return r; if(!nl)return E4_ERR_EXIST;
    e4_inode d; r=ext4_iget(fs,pino,&d); if(r)return r; u32 x; if(!e4_dir_find(fs,&d,lf,nl,&x))return E4_ERR_EXIST; if(!fs->extents||((d.flags&E4_IF_INDEX)&&fs->csum))return E4_ERR_ROFS;
    int isdir=(mode&E4_S_IFMT)==E4_S_IFDIR; u32 ino; e4_inode in; r=e4_new_inode(fs,pino,mode,&ino,&in); if(r)return r;
    if(isdir){ r=ext4_iput(fs,ino,&in); if(r)return r; u64 pb; r=e4_get_block(fs,ino,&in,0,1,&pb); if(r)return r; u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM;
        e4_st32(b,ino); e4_st16(b+4,12); b[6]=1; b[7]=fs->filetype?E4_FT_DIR:0; b[8]='.'; e4_st32(b+12,pino); e4_st16(b+16,(u16)(fs->bsize-12-e4_dir_tail(fs))); b[18]=2; b[19]=fs->filetype?E4_FT_DIR:0; b[20]='.'; b[21]='.';
        e4_dir_csum_set(fs,ino,in.gen,b); r=ext4_bwrite_meta(fs,pb,b); E4_FREE(b); if(r)return r; in.size=fs->bsize; }
    r=ext4_iput(fs,ino,&in); if(r)return r;
    r=e4_dir_add(fs,pino,&d,lf,nl,ino,isdir?E4_FT_DIR:(mode&E4_S_IFMT)==E4_S_IFLNK?E4_FT_SYMLINK:E4_FT_REG); if(r)return r;
    if(isdir){ r=ext4_iget(fs,pino,&d); if(r)return r; if(d.links<65000)d.links++; r=ext4_iput(fs,pino,&d); if(r)return r; }
    if(out)*out=ino; return 0; }
int ext4_create(ext4_fs* fs,const char* path,u16 mode,u32* ino){ int r=e4_op_begin(fs); if(r)return r; if(!(mode&E4_S_IFMT))mode|=E4_S_IFREG; return e4_op_end(fs,e4_create_in(fs,path,mode,ino)); }
static int e4_mkdir_p_in(ext4_fs* fs,const char* path,u16 mode){ u32 L=0; while(path[L])L++; if(L>=E4_PATH_MAX)return E4_ERR_NAMETOOLONG; char* t=(char*)E4_ALLOC(L+1); if(!t)return E4_ERR_NOMEM; e4_cpy(t,path,L+1);
    int r=0, made=0; u32 i=0; while(i<L){ while(i<L&&t[i]=='/')i++; if(i>=L)break; while(i<L&&t[i]!='/')i++; char c=t[i]; t[i]=0; u32 ino;
        r=ext4_lookup(fs,t,&ino); if(r==E4_ERR_NOENT){ r=e4_create_in(fs,t,mode,0); made=1; } else if(!r){ e4_inode d; r=ext4_iget(fs,ino,&d); if(!r&&(d.mode&E4_S_IFMT)!=E4_S_IFDIR)r=E4_ERR_NOTDIR; }
        t[i]=c; if(r)break; }
    E4_FREE(t); (void)made; return r; }
int ext4_mkdir_p(ext4_fs* fs,const char* path,u16 mode){ int r=e4_op_begin(fs); if(r)return r; return e4_op_end(fs,e4_mkdir_p_in(fs,path,(u16)(E4_S_IFDIR|(mode&07777)))); }

static int e4_empty_cb(ext4_fs* fs,u8* b,u8* de,u8* prev,u64 l,void* c){ (void)fs; (void)b; (void)prev; (void)l; (void)c; if(!e4_le32(de))return 0; if(de[6]==1&&de[8]=='.')return 0; if(de[6]==2&&de[8]=='.'&&de[9]=='.')return 0; return 1; }
static int e4_drop_inode(ext4_fs* fs,u32 ino,e4_inode* in){ int isdir=(in->mode&E4_S_IFMT)==E4_S_IFDIR; if(isdir)in->links=0; else if(in->links)in->links--; if(in->links){ in->ctime=e4_now(); return ext4_iput(fs,ino,in); }
    int r=ext4_iput(fs,ino,in); if(r)return r; r=e4_trunc_in(fs,ino,0,1); if(r)return r; r=ext4_iget(fs,ino,in); if(r)return r; in->dtime=e4_now(); in->links=0; r=ext4_iput(fs,ino,in); if(r)return r; return e4_ifree(fs,ino,isdir); }
static int e4_unlink_in(ext4_fs* fs,const char* path,int wantdir){ u32 pino; const char* lf; u32 nl; int r=e4_path_walk(fs,path,&pino,1,&lf,&nl); if(r)return r; if(!nl)return E4_ERR_INVAL;
    if((nl==1&&lf[0]=='.')||(nl==2&&lf[0]=='.'&&lf[1]=='.'))return E4_ERR_INVAL;
    e4_inode d; r=ext4_iget(fs,pino,&d); if(r)return r; u32 ino; r=e4_dir_find(fs,&d,lf,nl,&ino); if(r)return r; e4_inode in; r=ext4_iget(fs,ino,&in); if(r)return r;
    int isdir=(in.mode&E4_S_IFMT)==E4_S_IFDIR; if(wantdir>=0&&isdir!=wantdir)return isdir?E4_ERR_ISDIR:E4_ERR_NOTDIR;
    if(isdir){ int e=e4_dir_iter(fs,&in,e4_empty_cb,0); if(e==0)return E4_ERR_NOTEMPTY; if(e!=E4_ERR_NOENT)return e; }
    r=e4_dir_del(fs,pino,&d,lf,nl); if(r)return r; if(isdir){ r=ext4_iget(fs,pino,&d); if(r)return r; if(d.links>2)d.links--; r=ext4_iput(fs,pino,&d); if(r)return r; }
    return e4_drop_inode(fs,ino,&in); }
int ext4_unlink(ext4_fs* fs,const char* path){ int r=e4_op_begin(fs); if(r)return r; return e4_op_end(fs,e4_unlink_in(fs,path,-1)); }
int ext4_rmdir(ext4_fs* fs,const char* path){ int r=e4_op_begin(fs); if(r)return r; return e4_op_end(fs,e4_unlink_in(fs,path,1)); }

typedef struct { u32 ino; int set; } e4_dotdot_ctx;
static int e4_dotdot_cb(ext4_fs* fs,u8* b,u8* de,u8* prev,u64 l,void* c){ (void)prev; e4_dotdot_ctx* x=(e4_dotdot_ctx*)c; if(de[6]==2&&de[8]=='.'&&de[9]=='.'&&l==0){ e4_st32(de,x->ino); x->set=1; (void)fs; (void)b; return 1; } return 0; }
static int e4_rename_in(ext4_fs* fs,const char* from,const char* to){
    u32 sp,dp; const char* sl; const char* dl; u32 sn,dn; int r=e4_path_walk(fs,from,&sp,1,&sl,&sn); if(r)return r; r=e4_path_walk(fs,to,&dp,1,&dl,&dn); if(r)return r; if(!sn||!dn)return E4_ERR_INVAL;
    if((sn==1&&sl[0]=='.')||(sn==2&&sl[0]=='.'&&sl[1]=='.')||(dn==1&&dl[0]=='.')||(dn==2&&dl[0]=='.'&&dl[1]=='.'))return E4_ERR_INVAL;
    e4_inode sd; r=ext4_iget(fs,sp,&sd); if(r)return r; u32 ino; r=e4_dir_find(fs,&sd,sl,sn,&ino); if(r)return r; e4_inode in; r=ext4_iget(fs,ino,&in); if(r)return r; int isdir=(in.mode&E4_S_IFMT)==E4_S_IFDIR;
    if(isdir){ u32 c=dp; for(int k=0;k<1024&&c!=E4_ROOT_INO;k++){ if(c==ino)return E4_ERR_INVAL; e4_inode cd; if(ext4_iget(fs,c,&cd))return E4_ERR_IO; u32 up; if(e4_dir_find(fs,&cd,"..",2,&up))return E4_ERR_CORRUPT; c=up; } if(c!=E4_ROOT_INO||dp==ino)return E4_ERR_INVAL; }
    e4_inode dd; r=ext4_iget(fs,dp,&dd); if(r)return r; u32 old;
    if(!e4_dir_find(fs,&dd,dl,dn,&old)){ if(old==ino)return 0; e4_inode oi; r=ext4_iget(fs,old,&oi); if(r)return r; int od=(oi.mode&E4_S_IFMT)==E4_S_IFDIR; if(od!=isdir)return od?E4_ERR_ISDIR:E4_ERR_NOTDIR;
        r=e4_unlink_in(fs,to,od); if(r)return r; r=ext4_iget(fs,dp,&dd); if(r)return r; }
    r=e4_dir_add(fs,dp,&dd,dl,dn,ino,isdir?E4_FT_DIR:(in.mode&E4_S_IFMT)==E4_S_IFLNK?E4_FT_SYMLINK:E4_FT_REG); if(r)return r;
    r=ext4_iget(fs,sp,&sd); if(r)return r; r=e4_dir_del(fs,sp,&sd,sl,sn); if(r)return r;
    if(isdir&&sp!=dp){ e4_dotdot_ctx x={dp,0}; e4_inode di; r=ext4_iget(fs,ino,&di); if(r)return r; u8* b=(u8*)E4_ALLOC(fs->bsize); if(!b)return E4_ERR_NOMEM; u64 pb=0; r=ext4_bmap(fs,&di,0,&pb);
        if(!r&&pb&&!ext4_bread(fs,pb,b)){ u16 r0=e4_le16(b+4); if(r0<12||(u32)r0+12>fs->bsize||!e4_dir_tail_ok(fs,b))r=E4_ERR_CORRUPT; else { u8* de=b+r0; if(e4_dotdot_cb(fs,b,de,b,0,&x)){ e4_dir_csum_set(fs,ino,di.gen,b); r=ext4_bwrite_meta(fs,pb,b); } } } E4_FREE(b); if(r)return r;
        r=ext4_iget(fs,sp,&sd); if(!r){ if(sd.links>2)sd.links--; r=ext4_iput(fs,sp,&sd); } if(r)return r; r=ext4_iget(fs,dp,&dd); if(!r){ if(dd.links<65000)dd.links++; r=ext4_iput(fs,dp,&dd); } if(r)return r; }
    r=ext4_iget(fs,ino,&in); if(r)return r; in.ctime=e4_now(); return ext4_iput(fs,ino,&in); }
int ext4_rename(ext4_fs* fs,const char* from,const char* to){ int r=e4_op_begin(fs); if(r)return r; return e4_op_end(fs,e4_rename_in(fs,from,to)); }

typedef struct { e4_ls_fn cb; void* ctx; char nm[256]; } e4_ls_ctx;
static int e4_ls_cb(ext4_fs* fs,u8* b,u8* de,u8* prev,u64 l,void* c){ (void)b; (void)prev; (void)l; e4_ls_ctx* x=(e4_ls_ctx*)c; u32 ino=e4_le32(de); if(!ino)return 0; u32 nl=de[6];
    if((nl==1&&de[8]=='.')||(nl==2&&de[8]=='.'&&de[9]=='.'))return 0; e4_cpy(x->nm,de+8,nl); x->nm[nl]=0; e4_inode in; if(ext4_iget(fs,ino,&in))return 0;
    x->cb(x->nm,(in.mode&E4_S_IFMT)==E4_S_IFDIR,in.size,x->ctx); return 0; }
int ext4_readdir(ext4_fs* fs,const char* path,e4_ls_fn cb,void* ctx){ u32 ino; int r=ext4_lookup(fs,path,&ino); if(r)return r; e4_inode d; r=ext4_iget(fs,ino,&d); if(r)return r;
    e4_ls_ctx x; x.cb=cb; x.ctx=ctx; r=e4_dir_iter(fs,&d,e4_ls_cb,&x); return r==E4_ERR_NOENT?0:r; }

u64 ext4_free_bytes(ext4_fs* fs){ return fs->free_blocks*fs->bsize; }
u64 ext4_total_bytes(ext4_fs* fs){ return fs->blocks_count*fs->bsize; }

static void e4_mount_fail(ext4_fs* fs){ fs->ro=1; fs->mounted=0; if(fs->gdt)E4_FREE(fs->gdt); if(fs->gdt_dirty)E4_FREE(fs->gdt_dirty); fs->gdt=0; fs->gdt_dirty=0; }
void ext4_count_free(ext4_fs* fs){ u64 b=0; u64 i=0; for(u32 g=0;g<fs->ngroups;g++){ b+=E4GD_FREEB(fs,g); i+=E4GD_FREEI(fs,g); } if(b<=fs->blocks_count)fs->free_blocks=b; if(i<=fs->inodes_count)fs->free_inodes=(u32)i; }
int ext4_mount(ext4_fs* fs,e4_rd_fn rd,e4_wr_fn wr,u64 base,u64 nsect,int ro){ return ext4_mount_fl(fs,rd,wr,0,base,nsect,ro); }
int ext4_mount_fl(ext4_fs* fs,e4_rd_fn rd,e4_wr_fn wr,e4_fl_fn fl,u64 base,u64 nsect,int ro){
    e4_set(fs,0,sizeof(*fs)); fs->rd=rd; fs->wr=wr; fs->fl=fl; fs->base=base; fs->nsect=nsect; fs->ro=ro||!wr;
#ifndef EXT4_HOST
    if(!fl)fs->ro=1;
#endif

    if(rd(base+2,2,fs->sb))return E4_ERR_IO; u8* s=fs->sb; if(e4_le16(s+0x38)!=E4_SB_MAGIC)return E4_ERR_INVAL;
    fs->log_bsize=e4_le32(s+0x18); if(fs->log_bsize>6)return E4_ERR_INVAL; fs->bsize=1024u<<fs->log_bsize; fs->spb=fs->bsize/512;
    fs->feat_compat=e4_le32(s+0x5C); fs->feat_incompat=e4_le32(s+0x60); fs->feat_rocompat=e4_le32(s+0x64);
    if(fs->feat_incompat&~(u32)E4_INCOMPAT_RO_OK)return E4_ERR_INVAL; if(fs->feat_rocompat&E4_RO_BIGALLOC)return E4_ERR_INVAL;
    if((fs->feat_rocompat&~(u32)E4_ROCOMPAT_RW_OK)||(fs->feat_incompat&~(u32)E4_INCOMPAT_RW_OK))fs->ro=1;
    fs->csum=!!(fs->feat_rocompat&E4_RO_METADATA_CSUM); fs->gdtcsum=!fs->csum&&(fs->feat_rocompat&E4_RO_GDT_CSUM);
    if(fs->csum&&e4_crc32c(~0u,s,0x3FC)!=e4_le32(s+0x3FC))return E4_ERR_CORRUPT;
    fs->has64=!!(fs->feat_incompat&E4_INCOMPAT_64BIT); fs->extents=!!(fs->feat_incompat&E4_INCOMPAT_EXTENTS); fs->flexbg=!!(fs->feat_incompat&E4_INCOMPAT_FLEX_BG); fs->filetype=!!(fs->feat_incompat&E4_INCOMPAT_FILETYPE); fs->htree=!!(fs->feat_compat&E4_COMPAT_DIR_INDEX);
    fs->blocks_count=e4_le32(s+4)|(fs->has64?(u64)e4_le32(s+0x150)<<32:0); fs->free_blocks=e4_le32(s+0xC)|(fs->has64?(u64)e4_le32(s+0x158)<<32:0); fs->first_data_block=e4_le32(s+0x14);
    fs->inodes_count=e4_le32(s); fs->free_inodes=e4_le32(s+0x10); fs->blocks_per_group=e4_le32(s+0x20); fs->inodes_per_group=e4_le32(s+0x28);
    fs->inode_size=e4_le32(s+0x4C)?e4_le16(s+0x58):128; fs->desc_size=fs->has64?e4_le16(s+0xFE):32; if(fs->desc_size<32)fs->desc_size=32;
    if(!fs->blocks_per_group||!fs->inodes_per_group||fs->inode_size<128||fs->inode_size>1024||fs->desc_size>1024||fs->blocks_per_group>fs->bsize*8||fs->inodes_per_group>fs->bsize*8)return E4_ERR_CORRUPT;
    if((fs->inode_size&(fs->inode_size-1))||fs->inode_size>fs->bsize||(fs->desc_size&(fs->desc_size-1))||fs->first_data_block>=fs->blocks_count)return E4_ERR_CORRUPT;
    if(nsect&&fs->blocks_count*fs->spb>nsect)return E4_ERR_CORRUPT;
    fs->ngroups=(u32)((fs->blocks_count-fs->first_data_block+fs->blocks_per_group-1)/fs->blocks_per_group); if(!fs->ngroups||(u64)fs->inodes_count>(u64)fs->ngroups*fs->inodes_per_group)return E4_ERR_CORRUPT;
    e4_cpy(fs->uuid,s+0x68,16); e4_cpy(fs->label,s+0x78,16); fs->label[16]=0; for(int k=0;k<4;k++)fs->hash_seed[k]=e4_le32(s+0xEC+4*k); fs->hash_version=s[0xFC];
    fs->csum_seed=(fs->feat_incompat&E4_INCOMPAT_CSUM_SEED)?e4_le32(s+0x270):e4_crc32c(~0u,fs->uuid,16);
    fs->journal_ino=(fs->feat_compat&E4_COMPAT_HAS_JOURNAL)?e4_le32(s+0xE0):0; if(fs->feat_incompat&E4_INCOMPAT_JOURNAL_DEV)return E4_ERR_INVAL;
    u64 gbytes=(u64)fs->ngroups*fs->desc_size; fs->gdt_blocks=(u32)((gbytes+fs->bsize-1)/fs->bsize); fs->gdt=(u8*)E4_ALLOC((u64)fs->gdt_blocks*fs->bsize); fs->gdt_dirty=(u8*)E4_ALLOC(fs->gdt_blocks+1);
    if((fs->feat_incompat&E4_INCOMPAT_META_BG)&&e4_le32(s+0x104)>fs->gdt_blocks){ if(fs->gdt)E4_FREE(fs->gdt); if(fs->gdt_dirty)E4_FREE(fs->gdt_dirty); return E4_ERR_CORRUPT; }
    if(!fs->gdt||!fs->gdt_dirty){ if(fs->gdt)E4_FREE(fs->gdt); if(fs->gdt_dirty)E4_FREE(fs->gdt_dirty); return E4_ERR_NOMEM; }
    fs->mounted=1;
    for(u32 i=0;i<fs->gdt_blocks;i++) if(ext4_bread_raw(fs,ext4_gdt_loc(fs,i),fs->gdt+(u64)i*fs->bsize)){ e4_mount_fail(fs); return E4_ERR_IO; }
    for(u32 g=0;g<fs->ngroups;g++){ if((fs->csum||fs->gdtcsum)&&e4_le16(e4_gd(fs,g)+0x1E)!=e4_gd_csum_calc(fs,g)){ e4_mount_fail(fs); return E4_ERR_CORRUPT; }
        if(E4GD_ITAB(fs,g)>=fs->blocks_count||E4GD_BBMP(fs,g)>=fs->blocks_count||E4GD_IBMP(fs,g)>=fs->blocks_count){ e4_mount_fail(fs); return E4_ERR_CORRUPT; } }
    ext4_count_free(fs);
    if(!fs->extents||fs->bsize>4096)fs->ro=1;
    fs->was_clean=e4_le16(s+0x3A)&1;
    int jr=ext4_jnl_open(fs); if(jr){ fs->mounted=0; E4_FREE(fs->gdt); E4_FREE(fs->gdt_dirty); fs->gdt=0; fs->gdt_dirty=0; ext4_jnl_close(fs); return jr; }
    if(!fs->ro&&(fs->feat_incompat&E4_INCOMPAT_RECOVER))fs->ro=1;
    if(!fs->ro){ if(!fs->was_clean||(e4_le16(s+0x3A)&2))fs->ro=1; }
    if(!fs->ro&&e4_le32(s+0xE8))fs->ro=1;
    if(!fs->ro){ e4_st16(s+0x3A,(u16)(e4_le16(s+0x3A)&~1u)); e4_st16(s+0x34,(u16)(e4_le16(s+0x34)+1)); e4_st32(s+0x2C,e4_now()); fs->dirty=1; int r=e4_op_begin(fs); if(!r)r=e4_op_end(fs,0); if(r){ fs->ro=1; } }
    return 0; }

int ext4_sync(ext4_fs* fs){ if(!fs->mounted||fs->ro)return 0; int r=e4_op_begin(fs); if(r)return r; return e4_op_end(fs,0); }
int ext4_umount(ext4_fs* fs){ if(!fs->mounted)return 0; int r=0;
    if(!fs->ro&&fs->gdt){ if(!ext4_jnl_begin(fs)){ e4_st16(fs->sb+0x3A,(u16)(e4_le16(fs->sb+0x3A)|1u)); fs->dirty=1; r=e4_op_end(fs,0); } }
    if(!r&&!fs->ro&&fs->fl&&fs->fl())r=E4_ERR_IO;
    ext4_jnl_close(fs); if(fs->gdt)E4_FREE(fs->gdt); if(fs->gdt_dirty)E4_FREE(fs->gdt_dirty); fs->gdt=0; fs->gdt_dirty=0; fs->mounted=0; return r; }
