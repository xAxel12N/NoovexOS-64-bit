#include "kernel64.c"
u32 fk_disk_secs;
void fk_heap(void){ percpu_init_bsp(); heap_base=0x10000000ULL; heap_cur=heap_base; heap_top=heap_base+0x40000000ULL; heap_head=0; g_ram_mb=2048; }
static void fk_reset(void){
    for(int i=0;i<VMAXOPEN;i++)g_vf[i].used=0; g_fd_init=0; fd_init();
    for(int k=0;k<FS_N;k++){ fs_free(k); FS[k].used=0; FS[k].len=0; g_fs_dirty[k]=0; }
    for(int k=0;k<FS_N;k++){ if(g_n4xx[k]){ kfree(g_n4xx[k]); g_n4xx[k]=0; } g_n4nx[k]=0; g_n4ind[k]=0; g_n4indd[k]=0; }
    for(int i=0;i<DN_MAX;i++)DT[i].used=0; g_dtree_init=0; g_dcwd=0; g_nvx_have_tree=0;
    for(int i=0;i<NLINKS;i++)g_lk[i].used=0;
    disk_ok=0; g_n4=0; g_rootfs=0; g_n4pfn=0; g_n4_nres=0; g_nvx_note="";
    g_ata_drv=-1; g_gdisk_probed=1; g_dktype=DK_VIRTIO; g_dksectors=fk_disk_secs;
    bc_invalidate(0,0x7FFFFFFF);
    g_uid=0; g_gid=0; g_umask=022; g_jcur=-1;
}
int fk_boot(int with_defaults){
    fk_reset();
    if(with_defaults)fs_init();
    disk_init(); dt_init();
    return disk_ok;
}
const char* fk_note(void){ return g_nvx_note; }
const char* fk_err(void){ return g_n4_err; }
unsigned fk_metab(void){ return (unsigned)N4_METAB; }
int fk_n4(void){ return g_n4; }
int fk_open(const char* p,int fl){ return vfs_open(p,fl); }
int fk_write(int fd,const void* b,int n){ return vfs_write(fd,(const char*)b,n); }
int fk_read(int fd,void* b,int n){ return vfs_read(fd,(char*)b,n); }
int fk_close(int fd){ return vfs_close(fd); }
int fk_lseek(int fd,int off,int wh){ return vfs_lseek(fd,off,wh); }
int fk_mkdir(const char* p){ int d; char b[64]; if(k_split(p,&d,b)<0||!b[0])return -1; if(!perm_dir(d))return -1; if(dt_child(d,b)>=0)return -1; int n=dt_mkdir_at(d,b); if(n<0)return -1; dt_save(); return 0; }
int fk_rmdir(const char* p){ return k_rmdir(p); }
int fk_unlink(const char* p){ return ux_unlink(p); }
int fk_rename(const char* a,const char* b){ return k_rename(a,b); }
int fk_link(const char* a,const char* b){ return ux_link(a,b); }
int fk_symlink(const char* a,const char* b){ return ux_symlink(a,b); }
int fk_readlink(const char* p,char* o,int c){ return ux_readlink(p,o,c); }
int fk_truncate(const char* p,long n){ return k_truncate(p,n); }
int fk_stat(const char* p,u32* o){ return ux_stat2(p,o,1); }
int fk_chmod(const char* p,unsigned m){ return ux_chmod(p,m); }
int fk_sync(void){ nvx3_sync_all(); return g_n4?0:-1; }
int fk_fsync(int fd){ return k_fsync(fd); }
int fk_used_files(void){ int n=0; for(int k=0;k<FS_N;k++) if(FS[k].used)n++; return n; }
int fk_loaded_files(void){ int n=0; for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].data)n++; return n; }
unsigned long long fk_free_bytes(void){ u64 t,f; n4_usage(&t,&f); return f; }
unsigned long long fk_total_bytes(void){ u64 t,f; n4_usage(&t,&f); return t; }
int fk_readdir(const char* p,int n,char* name,int* type){ return k_readdir_at(p,n,name,type); }
void fk_setuid(int u){ g_uid=u; g_gid=u; }
int fk_free_vfiles(void){ int n=0; for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used)n++; return n; }
long fk_evict(long want){ g_fs_tick+=1000; return fs_evict(want,-1); }
int fk_bitmap_check(void){
    if(!g_n4)return -1; static u8 bm[1<<21]; u32 nb=g_n4_nb; if(nb>(1u<<24))return -2;
    for(u32 i=0;i<(nb+7)/8;i++)bm[i]=0;
    for(u32 b=0;b<g_n4_d0;b++)bm[b>>3]|=(u8)(1u<<(b&7));
    for(int k=0;k<FS_N;k++){ if(!FS[k].used)continue; for(u32 i=0;i<g_n4nx[k];i++){ n4ext* e=n4_ext(k,i); for(u32 b=e->start;b<e->start+e->count;b++){ if(bm[b>>3]&(1u<<(b&7)))return -3; bm[b>>3]|=(u8)(1u<<(b&7)); } }
        if(g_n4ind[k]){ u32 b=g_n4ind[k]; if(bm[b>>3]&(1u<<(b&7)))return -4; bm[b>>3]|=(u8)(1u<<(b&7)); } }
    for(int i=0;i<g_n4pfn;i++) for(u32 b=g_n4pf[i].start;b<g_n4pf[i].start+g_n4pf[i].count;b++)bm[b>>3]|=(u8)(1u<<(b&7));
    u32 fr=0; for(u32 b=0;b<nb;b++){ int a=(bm[b>>3]>>(b&7))&1, c=bm_get(b); if(a!=c)return -5; if(!a)fr++; }
    if(fr!=g_n4_free)return -6;
    return 0;
}
void fk_layout(void){ ux_boot_layout(); }
