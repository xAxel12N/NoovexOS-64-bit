#include "include/nvx/core/sysfeat.h"
#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/fs/ntfs/ntfsk.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "fs/ext4/ext4.h"

static int e4k_is_ext(unsigned long long lba){ u8 b[1024]; if(nk_blk_rd(lba+2,2,b))return 0; return e4_le16(b+0x38)==E4_SB_MAGIC; }
static int e4k_find(unsigned long long* base,unsigned long long* nsect){
    gdisk_ensure(); u32 r=dk_ratio(); if(!r)r=1;
    if(e4k_is_ext(0)){ *base=0; *nsect=0; return 0; }
    u8 m[512]; if(nk_blk_rd(0,1,m))return -1; if(m[510]!=0x55||m[511]!=0xAA)return -1; int gpt=0;
    for(int e=0;e<4;e++){ u8* pe=m+446+e*16; u8 t=pe[4]; u32 st=e4_le32(pe+8), sz=e4_le32(pe+12); if(t==0xEE)gpt=1;
        if(t==0x83&&sz){ unsigned long long b=(unsigned long long)st*r; if(e4k_is_ext(b)){ *base=b; *nsect=(unsigned long long)sz*r; return 0; } } }
    if(!gpt)return -1;
    static const u8 lnx[16]={0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4};
    u8 h[512]; if(nk_blk_rd(1ULL*r,1,h))return -1; if(h[0]!='E'||h[1]!='F'||h[2]!='I'||h[3]!=' ')return -1;
    unsigned long long elba=e4_le64(h+72); u32 ne=e4_le32(h+80), esz=e4_le32(h+84); if(ne>256)ne=256; if(esz<128||esz>512||(esz&(esz-1)))return -1;
    u8 s[512]; unsigned long long last=~0ULL;
    for(u32 i=0;i<ne;i++){ unsigned long long byte=(unsigned long long)i*esz; unsigned long long l=elba*r+byte/512; int off=(int)(byte%512);
        if(l!=last){ if(nk_blk_rd(l,1,s))return -1; last=l; }
        int match=1; for(int k=0;k<16;k++) if(s[off+k]!=lnx[k]){ match=0; break; }
        if(match){ unsigned long long f=e4_le64(s+off+32), lz=e4_le64(s+off+40); if(lz>=f&&e4k_is_ext(f*r)){ *base=f*r; *nsect=(lz-f+1)*r; return 0; } } }
    return -1; }

int ext4k_mounted(void){ return g_ext4.mounted; }
int ext4k_is_ro(void){ return !g_ext4.mounted||g_ext4.ro; }
int ext4k_mount(int ro){ if(g_ext4.mounted)return 0; unsigned long long b=0,n=0; if(e4k_find(&b,&n))return -2; if(!EXT4_RW_VERIFIED)ro=1;
    int r=ext4_mount_fl(&g_ext4,nk_blk_rd,ro?0:nk_blk_wr,gdisk_flush,b,n,ro); if(r){ klog("ext4: mount failed"); return r; }
    klog(g_ext4.ro?"ext4: mounted read-only":"ext4: mounted read-write"); return 0; }
void ext4k_umount(void){ if(g_ext4.mounted&&ext4_umount(&g_ext4))klog("ext4: unmount flush failed"); }
int ext4k_exists(const char* path){ u32 i; return g_ext4.mounted&&!ext4_lookup(&g_ext4,path,&i); }
int ext4k_mkdir(const char* path){ return g_ext4.mounted?ext4_mkdir_p(&g_ext4,path,0755):-1; }
long ext4k_size(const char* path){ e4_inode in; if(!g_ext4.mounted||ext4_stat(&g_ext4,path,&in,0))return -1; return (long)in.size; }
long ext4k_read(const char* path,char* buf,long cap){ u32 i; if(!g_ext4.mounted||ext4_lookup(&g_ext4,path,&i))return -1; return ext4_pread(&g_ext4,i,0,buf,cap); }
long ext4k_write(const char* path,const char* buf,long len){ if(!g_ext4.mounted)return -1; u32 i; int r=ext4_lookup(&g_ext4,path,&i);
    if(r==E4_ERR_NOENT)r=ext4_create(&g_ext4,path,0644,&i); else if(!r)r=ext4_truncate(&g_ext4,i,0);
    if(r)return r; return len?ext4_pwrite(&g_ext4,i,0,buf,len):0; }
int ext4k_delete(const char* path){ if(!g_ext4.mounted)return -1; e4_inode in; if(ext4_stat(&g_ext4,path,&in,0))return -1; return (in.mode&E4_S_IFMT)==E4_S_IFDIR?ext4_rmdir(&g_ext4,path):ext4_unlink(&g_ext4,path); }
int ext4k_ls(const char* path,e4_ls_fn cb,void* ctx){ return g_ext4.mounted?ext4_readdir(&g_ext4,path,cb,ctx):-1; }
int ext4k_rename(const char* from,const char* to){ return g_ext4.mounted?ext4_rename(&g_ext4,from,to):-1; }

static int g_e4k_disk=-1;
static int e4k_drd(unsigned long long lba,int n,void* buf){ return gdisk_read_on(g_e4k_disk,lba,n,(u8*)buf); }
static int e4k_dwr(unsigned long long lba,int n,const void* buf){ return gdisk_writen_on(g_e4k_disk,lba,n,(const u8*)buf); }
static int e4k_dfl(void){ return gdisk_flush_on(g_e4k_disk); }
static int e4k_cur_fl(void){ return gdisk_flush_on(gdisk_current()); }
int ext4k_mount_disk(int disk,unsigned long long base,unsigned long long nsect,int ro){
    if(g_ext4.mounted)ext4_umount(&g_ext4); if(disk<0||gdisk_ratio_of(disk)!=1)return -1; g_e4k_disk=disk;
    if(!EXT4_RW_VERIFIED||sysfeat_get(SYSF_EXT4_RW)!=SYSF_EXT4_FLUSH||sysfeat_state(SYSF_EXT4_RW)==SYSF_ST_FAILED)ro=1; if(!ro&&!gdisk_flush_cap_of(disk)){ klog("ext4: disk reports no cache flush, mounting read-only"); ro=1; }
    int r=ext4_mount_fl(&g_ext4,e4k_drd,ro?0:e4k_dwr,ro?0:e4k_dfl,base,nsect,ro); if(r&&!ro){ sysfeat_fail(SYSF_EXT4_RW,"read-write mount failed"); r=ext4_mount_fl(&g_ext4,e4k_drd,0,0,base,nsect,1); } if(r){ klog("ext4: mount failed"); return r; }
    klog(g_ext4.ro?"ext4: volume mounted read-only":"ext4: volume mounted read-write"); return 0; }
int ext4k_mount_blk(e4_rd_fn rd,e4_wr_fn wr,e4_fl_fn fl,unsigned long long base,unsigned long long nsect,int ro){
    if(g_ext4.mounted)ext4_umount(&g_ext4); if(!rd)return -1; if(!EXT4_RW_VERIFIED||!wr||!fl||sysfeat_get(SYSF_EXT4_RW)!=SYSF_EXT4_FLUSH||sysfeat_state(SYSF_EXT4_RW)==SYSF_ST_FAILED)ro=1;
    int r=ext4_mount_fl(&g_ext4,rd,ro?0:wr,ro?0:fl,base,nsect,ro); if(r&&!ro){ sysfeat_fail(SYSF_EXT4_RW,"read-write mount failed"); r=ext4_mount_fl(&g_ext4,rd,0,0,base,nsect,1); } if(r){ klog("ext4: mount failed"); return r; }
    klog(g_ext4.ro?"ext4: removable volume mounted read-only":"ext4: removable volume mounted read-write"); return 0; }
static int e4k_mount_rfs(nvxblk_rd rd,nvxblk_wr wr,unsigned long long b,unsigned long long n,int ro){ if(g_ext4.mounted)ext4_umount(&g_ext4); int nk=rd==nk_blk_rd;
    if(!EXT4_RW_VERIFIED||!nk||(!ro&&!gdisk_flush_cap_of(gdisk_current())))ro=1; return ext4_mount_fl(&g_ext4,rd,ro?0:wr,ro?0:e4k_cur_fl,b,n,ro); }
static int e4k_format(nvxblk_rd rd,nvxblk_wr wr,unsigned long long b,unsigned long long n,const char* l){ (void)rd; (void)wr; (void)b; (void)n; (void)l; return -1; }
const rfs_ops g_rfs_ext4={"ext4",e4k_mount_rfs,ext4k_umount,ext4k_exists,ext4k_mkdir,ext4k_write,ext4k_read,ext4k_delete,ext4k_ls,e4k_format};
