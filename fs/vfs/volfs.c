#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/core/sysfeat.h"
#include "include/nvx/fs/vfs/volumes.h"
#include "include/nvx/fs/vfs/volfs.h"
#include "include/nvx/fs/fat/vfat.h"
#include "include/nvx/fs/fat/exfat.h"
#include "drivers/mmc/mmc_blk.h"

extern int gdisk_read_on(int i,u64 lba,int cnt,u8* buf); extern int gdisk_writen_on(int i,u64 lba,int cnt,const u8* buf); extern int gdisk_flush_on(int i); extern int gdisk_flush_cap_of(int i); extern u32 gdisk_ratio_of(int i);
extern int usb_msd_rdn(u32 lba,int cnt,u8* out); extern int usb_msd_wrn(u32 lba,int cnt,const u8* in); extern int usb_msd_sync(void); extern void ufat_sync(void); extern void ufat_mount(void);
int ext4k_mounted(void); void ext4k_umount(void); int ext4k_mkdir(const char*); long ext4k_write(const char*,const char*,long); long ext4k_read(const char*,char*,long);
int ext4k_delete(const char*); int ext4k_ls(const char*,volfs_lscb,void*); int ext4k_rename(const char*,const char*); long ext4k_size(const char*); int ext4k_is_ro(void);
int ext4k_mount_blk(int (*rd)(unsigned long long,int,void*),int (*wr)(unsigned long long,int,const void*),int (*fl)(void),unsigned long long base,unsigned long long nsect,int ro);
void sdhci_adma_settle(void);

enum { VFS_FIX=0, VFS_SD=1, VFS_USB=2 };
static int g_vk=VOLFS_NONE, g_vv=-1, g_vro=1, g_vdisk=-1, g_vdev=VFS_FIX, g_vunclean=0, g_vesp=0, g_vbusy=0, g_vioerr=0; static unsigned long long g_vstart;
static struct nvx_g_vfat_t g_vfat_mine, g_vfat_save;
static int vfs_sd(void){ return g_vdev==VFS_SD; }
static int vfs_drd(unsigned long long lba,int n,void* b){
    if(g_vdev==VFS_SD)return mmcblk_read(VOL_SD_DISK0-g_vdisk,lba,n,(u8*)b);
    if(g_vdev==VFS_USB)return lba+(unsigned long long)n>0x100000000ULL?-1:usb_msd_rdn((u32)lba,n,(u8*)b);
    return gdisk_read_on(g_vdisk,lba,n,(u8*)b); }
static int vfs_dwr(unsigned long long lba,int n,const void* b){
    if(g_vdev==VFS_SD)return mmcblk_write(VOL_SD_DISK0-g_vdisk,lba,n,(const u8*)b)?-1:0;
    if(g_vdev==VFS_USB)return lba+(unsigned long long)n>0x100000000ULL?-1:usb_msd_wrn((u32)lba,n,(const u8*)b);
    return gdisk_writen_on(g_vdisk,lba,n,(const u8*)b)?-1:0; }
static int vfs_dfl(void){ if(g_vdev==VFS_SD)return 0; if(g_vdev==VFS_USB)return usb_msd_sync()?-1:0; return gdisk_flush_on(g_vdisk)?-1:0; }
static void vfs_in(void){ vf_swap_out(&g_vfat_save); vf_swap_in(&g_vfat_mine); }
static void vfs_out(void){ vf_swap_out(&g_vfat_mine); vf_swap_in(&g_vfat_save); }
static void vfs_post(void){ if(vfs_sd())sdhci_adma_settle(); }
static int vfs_dots(const char* s,int n){ return (n==1&&s[0]=='.')||(n==2&&s[0]=='.'&&s[1]=='.'); }
static int vfs_pok(const char* p){ if(!p||p[0]!='/')return 0; int i=0; for(;p[i];){ while(p[i]=='/')i++; int k=i; while(p[i]&&p[i]!='/')i++; if(vfs_dots(p+k,i-k))return 0; if(i>=1024)return 0; } return 1; }
static u32 vfs_r32(const u8* b){ return (u32)b[0]|((u32)b[1]<<8)|((u32)b[2]<<16)|((u32)b[3]<<24); }
static int vfs_isesp(const nvol* v){
    static const u8 esp[16]={0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
    if(v->part<1)return 0; u8 b[512]; if(vfs_drd(0,1,b))return 1; if(b[510]!=0x55||b[511]!=0xAA)return 0;
    int gpt=0; for(int e=0;e<4;e++) if(b[446+e*16+4]==0xEE)gpt=1;
    if(!gpt)return v->part<=4&&b[446+(v->part-1)*16+4]==0xEF;
    if(vfs_drd(1,1,b))return 1; if(b[0]!='E'||b[1]!='F'||b[2]!='I'||b[3]!=' '||b[4]!='P'||b[5]!='A'||b[6]!='R'||b[7]!='T')return 0;
    unsigned long long el=(unsigned long long)vfs_r32(b+72)|((unsigned long long)vfs_r32(b+76)<<32); u32 n=vfs_r32(b+80), es=vfs_r32(b+84);
    if((u32)v->part>n||es<128||es>512||(es&(es-1)))return 1;
    unsigned long long o=(unsigned long long)(v->part-1)*es; if(vfs_drd(el+o/512,1,b))return 1;
    for(int k=0;k<16;k++) if(b[o%512+k]!=esp[k])return 0; return 1; }
static void vfs_nocb(const char* n,int d,unsigned long long z,void* c){ (void)n; (void)d; (void)z; (void)c; }
static int vfs_isfat(const char* f){ return f[0]=='F'&&f[1]=='A'&&f[2]=='T'&&((f[3]=='1'&&f[4]=='6')||(f[3]=='3'&&f[4]=='2')); }
static int vfs_isexfat(const char* f){ return f[0]=='e'&&f[1]=='x'&&f[2]=='F'&&f[3]=='A'&&f[4]=='T'; }
static int vfs_isext(const char* f){ return f[0]=='e'&&f[1]=='x'&&f[2]=='t'; }
static int vfs_devof(const nvol* v){ return v->kind==VOL_USB?VFS_USB:v->disk<=VOL_SD_DISK0?VFS_SD:VFS_FIX; }
static int vfs_canwrite(const nvol* v,int dev){
    if(dev==VFS_SD)return g_mmcblk[VOL_SD_DISK0-v->disk].ro==0;
    if(!sysfeat_get(SYSF_VOLWRITE)||sysfeat_state(SYSF_VOLWRITE)==SYSF_ST_FAILED)return 0;
    if(dev==VFS_USB)return usb_msd_sync()==0;
    return v->disk>=0&&gdisk_ratio_of(v->disk)==1&&gdisk_flush_cap_of(v->disk); }
int volfs_kind(void){ return g_vk; }
int volfs_vol(void){ if(!g_vk||g_vv<0||g_vv>=g_vol_n||g_vol[g_vv].disk!=g_vdisk||g_vol[g_vv].start!=g_vstart)return -1; return g_vv; }
int volfs_ro(void){ if(g_vesp)return 1; return g_vk==VOLFS_EXT4?ext4k_is_ro():g_vro; }
int volfs_esp(void){ return g_vk?g_vesp:0; }
int volfs_unclean(void){ return g_vk?g_vunclean:0; }
void volfs_close(void){
    if(g_vk==VOLFS_FAT){ vfs_in(); vf_umount(); vfs_out(); if(!g_vro)vfs_dfl(); vfs_post(); }
    else if(g_vk==VOLFS_EXFAT){ xf_umount(); if(!g_vro)vfs_dfl(); vfs_post(); }
    else if(g_vk==VOLFS_EXT4&&ext4k_mounted()){ ext4k_umount(); vfs_post(); }
    g_vk=VOLFS_NONE; g_vv=-1; g_vro=1; g_vdisk=-1; g_vdev=VFS_FIX; g_vunclean=0; g_vesp=0; g_vioerr=0; g_vstart=0; }
static int g_vusbown;
int volfs_usb_yield(void){ if(g_vusbown)return -1; if(!g_vk||g_vdev!=VFS_USB)return 0; if(g_vbusy)return -1; klog("volfs: USB stick closed in Files so another part of the system can write to it"); volfs_close(); return 0; }
int volfs_usb_claim(int on){ if(!on){ g_vusbown=0; return 0; } if(g_vusbown)return 0; if(volfs_usb_yield())return -1; g_vusbown=1; return 0; }
int volfs_usb_owned(void){ return g_vusbown; }
int volfs_open(int vi){
    if(vi<0||vi>=g_vol_n)return VOLFS_E_ARG; nvol* v=&g_vol[vi]; if(v->kind==VOL_SYS)return VOLFS_E_ARG;
    if(g_vk&&g_vv==vi&&v->disk==g_vdisk&&v->start==g_vstart)return VOLFS_OK;
    if(!sysfeat_get(SYSF_VOLFILES)||sysfeat_state(SYSF_VOLFILES)==SYSF_ST_FAILED)return VOLFS_E_OFF;
    volfs_close();
    int dev=vfs_devof(v); if(dev==VFS_FIX&&v->disk<0)return VOLFS_E_FS; if(dev==VFS_USB&&g_vusbown)return VOLFS_E_BUSY;
    int ext=vfs_isext(v->fs), xf=vfs_isexfat(v->fs);
    if(!ext&&!xf&&!vfs_isfat(v->fs))return VOLFS_E_FS;
    if(xf&&(!sysfeat_get(SYSF_EXFAT)||sysfeat_state(SYSF_EXFAT)==SYSF_ST_FAILED))return VOLFS_E_OFF;
    if(dev==VFS_SD)sdhci_adma_settle(); if(!sysfeat_begin(SYSF_VOLFILES))return VOLFS_E_OFF;
    g_vdisk=v->disk; g_vdev=dev; g_vunclean=0; g_vesp=0; g_vstart=v->start;
    int esp=!ext&&vfs_isesp(v); int ro=esp||!vfs_canwrite(v,dev), r; if(dev==VFS_USB&&!ro)ufat_sync();
    if(ext){
        if(dev==VFS_FIX){ r=vol_mount_ext4(vi); sysfeat_ok(SYSF_VOLFILES); if(r){ g_vdisk=-1; return VOLFS_E_MOUNT; } }
        else { r=ext4k_mount_blk(vfs_drd,ro?0:vfs_dwr,ro?0:vfs_dfl,v->start,v->psz,ro); vfs_post(); sysfeat_ok(SYSF_VOLFILES); if(r){ g_vdisk=-1; return VOLFS_E_MOUNT; } }
        g_vk=VOLFS_EXT4; g_vv=vi; g_vro=ext4k_is_ro(); return VOLFS_OK; }
    if(xf){
        if(!sysfeat_begin(SYSF_EXFAT)){ sysfeat_ok(SYSF_VOLFILES); g_vdisk=-1; return VOLFS_E_OFF; }
        r=xf_mount(vfs_drd,ro?0:vfs_dwr,v->start,v->psz,ro); vfs_post(); sysfeat_ok(SYSF_EXFAT); sysfeat_ok(SYSF_VOLFILES);
        if(r){ g_vdisk=-1; klog("volfs: exFAT volume did not mount"); return VOLFS_E_MOUNT; }
        if(!ro&&dev==VFS_FIX&&!xf_ls("/EFI",vfs_nocb,0)){ xf_umount(); esp=1; ro=1; r=xf_mount(vfs_drd,0,v->start,v->psz,1); vfs_post(); if(r){ g_vdisk=-1; return VOLFS_E_MOUNT; } }
        g_vesp=esp; g_vunclean=xf_unclean(); g_vk=VOLFS_EXFAT; g_vv=vi; g_vro=xf_ro();
        klog(g_vesp?"volfs: EFI system partition - opened read-only":g_vunclean?"volfs: exFAT volume was not ejected safely - opened read-only":g_vro?"volfs: exFAT volume opened read-only":"volfs: exFAT volume opened read-write"); return VOLFS_OK; }
    vf_swap_out(&g_vfat_save); r=vf_mount(vfs_drd,ro?0:vfs_dwr,v->start,v->psz,ro);
    if(!r&&vf_dirty(-1)>0){ g_vunclean=1; if(!ro){ g_vfat.ro=1; g_vfat.wr=0; ro=1; } }
    if(!r&&!ro&&dev==VFS_FIX&&vf_exists("/EFI")){ g_vfat.ro=1; g_vfat.wr=0; ro=1; esp=1; }
    vf_swap_out(&g_vfat_mine); vf_swap_in(&g_vfat_save); vfs_post();
    sysfeat_ok(SYSF_VOLFILES);
    if(r){ g_vdisk=-1; klog("volfs: FAT volume did not mount"); return VOLFS_E_MOUNT; }
    g_vesp=esp; g_vk=VOLFS_FAT; g_vv=vi; g_vro=ro; klog(g_vesp?"volfs: EFI system partition - opened read-only":g_vunclean?"volfs: FAT volume was not ejected safely - opened read-only":ro?"volfs: FAT volume opened read-only":"volfs: FAT volume opened read-write"); return VOLFS_OK; }
static int vfs_wbegin(void){
    if(g_vk==VOLFS_FAT){ int r=vf_dirty(1); if(r<0||vfs_dfl())return -1; return 0; }
    if(g_vk==VOLFS_EXFAT){ if(xf_mark(1)||vfs_dfl())return -1; return 0; }
    return 0; }
static int vfs_ioerr(void){
    if(!g_vioerr)klog("volfs: the disk reported a write error - the volume is now read-only to protect its data; eject it and check it (fsck or chkdsk)");
    g_vioerr=1; g_vro=1; if(g_vk==VOLFS_FAT){ g_vfat.ro=1; g_vfat.wr=0; }
    if(g_vdev!=VFS_SD){ sysfeat_fail(SYSF_VOLWRITE,"the disk reported a write error"); }
    return VOLFS_E_IO; }
static long vfs_wend(long r){
    int bad=0;
    if(g_vk==VOLFS_FAT){ if(vfs_dfl()||vf_dirty(0)<0||vfs_dfl())bad=1; }
    else if(g_vk==VOLFS_EXFAT){ if(vfs_dfl()||xf_mark(0)||vfs_dfl())bad=1; }
    if(bad)return vfs_ioerr();
    return r; }
int volfs_ls(const char* path,volfs_lscb cb,void* ctx){
    if(!vfs_pok(path)||!cb)return VOLFS_E_ARG; int r; g_vbusy++;
    if(g_vk==VOLFS_EXT4)r=ext4k_ls(path,cb,ctx);
    else if(g_vk==VOLFS_EXFAT)r=xf_ls(path,cb,ctx);
    else if(g_vk==VOLFS_FAT){ vfs_in(); r=vf_ls(path,cb,ctx); vfs_out(); }
    else { g_vbusy--; return VOLFS_E_ARG; }
    g_vbusy--; vfs_post(); return r?VOLFS_E_IO:VOLFS_OK; }
long volfs_size(const char* path){
    if(!vfs_pok(path))return VOLFS_E_ARG; long r; g_vbusy++; if(g_vk==VOLFS_EXT4)r=ext4k_size(path); else if(g_vk==VOLFS_EXFAT)r=xf_size(path); else if(g_vk==VOLFS_FAT){ vfs_in(); r=vf_size(path); vfs_out(); } else { g_vbusy--; return VOLFS_E_ARG; }
    g_vbusy--; vfs_post(); return r; }
long volfs_read(const char* path,char* buf,long cap){
    if(!vfs_pok(path)||!buf||cap<0)return VOLFS_E_ARG; long r; g_vbusy++;
    if(g_vk==VOLFS_EXT4)r=ext4k_read(path,buf,cap); else if(g_vk==VOLFS_EXFAT)r=xf_read(path,buf,cap); else if(g_vk==VOLFS_FAT){ vfs_in(); r=vf_read(path,buf,cap); vfs_out(); } else { g_vbusy--; return VOLFS_E_ARG; }
    g_vbusy--; vfs_post(); return r<0?VOLFS_E_IO:r; }
static int vfs_wok(void){ return g_vk&&!volfs_ro(); }
static long vfs_e4write(const char* path,const char* buf,long len){
    if(ext4k_size(path)<0){ long r=ext4k_write(path,buf,len); if(r==len)return len; ext4k_delete(path); return VOLFS_E_IO; }
    int n=0; while(path[n])n++; int sl=n; while(sl>0&&path[sl-1]!='/')sl--; char t[1100]; if(n+8>(int)sizeof t||n-sl+6>255)return VOLFS_E_IO;
    for(int i=0;i<sl;i++)t[i]=path[i]; const char* pre=".~nvx-"; int o=sl; for(int i=0;pre[i];i++)t[o++]=pre[i]; for(int i=sl;i<n;i++)t[o++]=path[i]; t[o]=0;
    if(ext4k_size(t)>=0&&ext4k_delete(t))return VOLFS_E_IO;
    long r=ext4k_write(t,buf,len); if(r!=len){ ext4k_delete(t); return VOLFS_E_IO; }
    if(ext4k_rename(t,path)){ if(ext4k_size(path)>=0)ext4k_delete(t); return VOLFS_E_IO; } return len; }
enum { VW_WRITE, VW_MKDIR, VW_DEL, VW_REN };
static long vfs_wop2(int op,const char* a,const char* b,long len){
    long r;
    if(g_vk==VOLFS_EXT4){
        if(op==VW_WRITE)r=vfs_e4write(a,b,len);
        else r=(op==VW_MKDIR?ext4k_mkdir(a):op==VW_DEL?ext4k_delete(a):ext4k_rename(a,b))?VOLFS_E_IO:VOLFS_OK;
        vfs_post(); return r; }
    int usb=g_vdev==VFS_USB; if(usb)ufat_sync(); if(g_vk==VOLFS_FAT)vfs_in();
    if(vfs_wbegin()){ vfs_ioerr(); if(g_vk==VOLFS_FAT)vfs_out(); vfs_post(); if(usb)ufat_mount(); return VOLFS_E_IO; }
    if(g_vk==VOLFS_EXFAT){
        if(op==VW_WRITE){ r=xf_write(a,b,len); r=r==len?len:VOLFS_E_IO; }
        else r=(op==VW_MKDIR?xf_mkdir(a):op==VW_DEL?xf_delete(a):xf_rename(a,b))?VOLFS_E_IO:VOLFS_OK; }
    else {
        if(op==VW_WRITE){ r=vf_write(a,b,len); r=r==len?len:VOLFS_E_IO; }
        else r=(op==VW_MKDIR?vf_mkdir(a):op==VW_DEL?vf_delete(a):vf_rename(a,b))?VOLFS_E_IO:VOLFS_OK; }
    r=vfs_wend(r); if(g_vk==VOLFS_FAT)vfs_out(); vfs_post(); if(usb)ufat_mount(); return r; }
static long vfs_wop(int op,const char* a,const char* b,long len){
    if(!g_vk||!vfs_pok(a)||(op==VW_REN&&!vfs_pok(b)))return VOLFS_E_ARG; if(!vfs_wok())return VOLFS_E_RO;
    g_vbusy++; long r=vfs_wop2(op,a,b,len); g_vbusy--; return r; }
long volfs_write(const char* path,const char* buf,long len){ if(!g_vk||!path||len<0||(len&&!buf))return VOLFS_E_ARG; return vfs_wop(VW_WRITE,path,buf,len); }
int volfs_mkdir(const char* path){ if(!path)return VOLFS_E_ARG; return (int)vfs_wop(VW_MKDIR,path,0,0); }
int volfs_delete(const char* path){ if(!path)return VOLFS_E_ARG; return (int)vfs_wop(VW_DEL,path,0,0); }
int volfs_rename(const char* from,const char* to){ if(!from||!to)return VOLFS_E_ARG; return (int)vfs_wop(VW_REN,from,to,0); }
int volfs_eject(int vi){
    if(vi>=0&&vi<g_vol_n&&g_vol[vi].kind==VOL_USB){ if(g_vusbown||(g_vk&&g_vdev==VFS_USB&&g_vbusy))return VOLFS_E_BUSY; if(g_vk&&g_vdev==VFS_USB)volfs_close(); ufat_sync(); return usb_msd_sync()?VOLFS_E_IO:VOLFS_OK; }
    if(vi<0||vi>=g_vol_n||g_vol[vi].kind!=VOL_SD)return VOLFS_E_ARG; int d=g_vol[vi].disk;
    if(g_vk&&g_vdisk==d)volfs_close();
    if(mmcblk_eject(VOL_SD_DISK0-d))return VOLFS_E_IO; vol_scan(1); return VOLFS_OK; }
int volfs_join(char* out,int cap,const char* dir,const char* name){
    int n=0; if(!out||cap<3||!dir||!name)return -1;
    for(const char* s=dir;*s;s++){ if(n>=cap-1)return -1; out[n++]=*s; }
    if(!n||out[n-1]!='/'){ if(n>=cap-1)return -1; out[n++]='/'; }
    int nl=0; while(name[nl])nl++; if(!nl||vfs_dots(name,nl))return -1;
    for(const char* s=name;*s;s++){ if(*s=='/'||(unsigned char)*s<32||n>=cap-1)return -1; out[n++]=*s; }
    out[n]=0; return n; }
const char* volfs_why(int e){
    switch(e){ case VOLFS_OK: return "OK"; case VOLFS_E_MOUNT: return "The disk could not be opened"; case VOLFS_E_FS: return "This file system cannot be browsed yet";
    case VOLFS_E_RO: return g_vesp?"This is an EFI system partition (the computer's boot files) - it is read-only in Files":g_vioerr?"The disk reported a write error - it is read-only until it is ejected and checked (fsck or chkdsk)":g_vunclean?"This disk was not ejected safely last time - it is read-only until it is checked (fsck or chkdsk)":"This disk is read-only"; case VOLFS_E_OFF: return "Browsing this disk is turned off in Settings"; case VOLFS_E_IO: return "The disk reported an error"; case VOLFS_E_BUSY: return "The USB stick is in use by the system updater - try again when it has finished"; default: return "Not possible"; } }
