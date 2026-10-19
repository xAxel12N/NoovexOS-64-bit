#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/drivers/storage/ata.h"
#include "include/nvx/drivers/storage/nvme.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/fs/ntfs/ntfsk.h"
/* ---- NVXFS: persistent filesystem on the attached ATA disk (ported from Noovex9) ---- */
int g_rootfs=0;    
u32 g_nvx_base=2048, g_nvx_secs=0; int g_nvx_generic=0; const char* g_nvx_note="";

#define NVX_MAGIC    0x4E565849u

#define NVX3_MAGIC_K 0x3358564Eu

nvxfs_t g_nvx; u8 nvx_sec[512]; int disk_ok=0; int g_nvx_have_tree=0;
       
int nvx_rd(u32 lba,u8* b){ if(g_rootfs)return -1; return g_nvx_generic?gdisk_read_cached(lba,1,b):ata_read(lba,1,b); }
int nvx_rdn(u32 lba,int n,u8* b){ if(g_rootfs)return -1; return g_nvx_generic?gdisk_readn(lba,n,b):ata_read(lba,n,b); }
static int nvx_mount_old(void){ u8* p=(u8*)&g_nvx; int total=(int)sizeof(g_nvx),secs=(total+511)/512;
    for(int se=0;se<secs;se++){ if(nvx_rd(NVX_DIR_LBA+se,nvx_sec))return -1; for(int i=0;i<512;i++){ int off=se*512+i; if(off<total)p[off]=nvx_sec[i]; } }
    if(g_nvx.magic!=NVX_MAGIC)return -1; if(g_nvx.count>NVX_MAX)g_nvx.count=NVX_MAX; if(g_nvx.dcount>NVX_DN_MAX)g_nvx.dcount=NVX_DN_MAX; return 0; }
int nvx_write(const char*name,const u8*buf,int len,int folder){ (void)buf; (void)len; (void)folder; if(!disk_ok)return -1;
    for(int k=0;k<FS_N;k++) if(FS[k].used&&!FS[k].orphan&&seq(FS[k].name,(char*)name))return fs_sync(k);
    return -1; }
void nvx_delete_name(const char*name){ (void)name; nvx3_flush_meta(); }
static int nvx_is_zero(const u8* b){ for(int i=0;i<512;i++) if(b[i])return 0; return 1; }
static int nvx_locate(void){
    u8 mbr[512];
    if(g_ata_drv<0) ata_detect();
    if(g_ata_drv>=0){ int use_ide=1; if(!ata_read(0,1,mbr)){ int has=0; if(mbr[510]==0x55&&mbr[511]==0xAA) for(int e=0;e<4;e++){ if(mbr[446+e*16+4]==NVX_PTYPE)has=1; }
            if(!has){ gdisk_ensure(); if(g_dktype!=DK_IDE&&g_dktype!=DK_NONE&&g_dksectors&&!(g_dktype==DK_NVME&&!g_nvme_bsz)){ u8 m2[512]; if(!gdisk_read(0,1,m2)&&m2[510]==0x55&&m2[511]==0xAA) for(int e=0;e<4;e++){ if(m2[446+e*16+4]==NVX_PTYPE)use_ide=0; } } } }
        if(use_ide){ g_nvx_generic=0; if(ata_read(0,1,mbr))return 0; }
        else { g_nvx_generic=1; if(gdisk_read(0,1,mbr))return 0; } }
    else { gdisk_ensure(); if(g_dktype==DK_NONE||!g_dksectors)return 0; if(g_dktype==DK_NVME&&!g_nvme_bsz)return 0; g_nvx_generic=1; if(gdisk_read(0,1,mbr))return 0; }
    int valid=(mbr[510]==0x55&&mbr[511]==0xAA), anyp=0;
    if(valid) for(int e=0;e<4;e++){ u8* pe=mbr+446+e*16; u32 st=*(u32*)(pe+8)*(g_nvx_generic?dk_ratio():1u), sz=*(u32*)(pe+12)*(g_nvx_generic?dk_ratio():1u);
        if(pe[4]&&sz){ anyp=1; if(pe[4]==NVX_PTYPE&&sz>=2048u){ g_nvx_base=st; g_nvx_secs=sz; g_nvx_note="NoovexOS data partition"; return 2; } } }
    if(anyp){ g_nvx_note="Disk belongs to another system - not touched"; return 0; }
    if(!nvx_is_zero(mbr)&&!valid){ g_nvx_note="Unrecognised disk - not touched"; return 0; }
    g_nvx_base=2048; { u32 tot=g_nvx_generic?g_dksectors:g_disk_sectors; g_nvx_secs=tot>2048u?tot-2048u:0; }
    u8 d[512]; if(nvx_rd(g_nvx_base,d))return 0;
    if(*(u32*)d==NVX_MAGIC||*(u32*)d==NVX3_MAGIC_K||*(u32*)d==0x3458564Eu){ g_nvx_note="Scratch disk"; return 2; }
    if(nvx_is_zero(d)){ g_nvx_note="Blank scratch disk"; return 1; }
    g_nvx_note="Unrecognised data at LBA 2048 - not touched"; return 0;
}
int disk_init(void){
    g_n4_nres=0; g_n4=0;
    int how=nvx_locate();
    if(!how){ if(nk_root_try()){ disk_ok=1; return 1; } disk_ok=0; return 0; }
    u8 d[512]; if(nvx_rd(NVX_DIR_LBA,d)){ disk_ok=0; return 0; }
    u32 magic=*(u32*)d;
    disk_ok=1;
    int restored=0;
    if(magic!=0x3458564Eu&&n4_conv_restore(g_nvx_secs)==1){ restored=1; if(nvx_rdn(NVX_DIR_LBA,1,d)){ disk_ok=0; return 0; } magic=*(u32*)d; klog("nvx4: an interrupted conversion was undone from its backup"); }
    if(magic==0x3458564Eu){
        if(n4_mount(g_nvx_secs)){
            if(!restored&&n4_conv_restore(g_nvx_secs)==1&&!nvx_rdn(NVX_DIR_LBA,1,d)&&*(u32*)d!=0x3458564Eu){ magic=*(u32*)d; klog("nvx4: an interrupted conversion was undone from its backup"); goto conv; }
            disk_ok=0; g_nvx_note=g_n4_err; klog2("nvx4: mount failed: ",g_n4_err); return 0; }
        n4_conv_done(g_nvx_secs);
        { int nf=0; for(int k=0;k<FS_N;k++) if(FS[k].used)nf++; char t[48],*p=t; const char* a2="nvx4: mounted, files="; while(*a2)*p++=*a2++; p=udec(p,(u32)nf); *p=0; klog(t); }
        g_nvx_note="NoovexOS data partition (NVX4)"; return 1; }
conv:
    if(magic==NVX3_MAGIC_K){
        if(nvx3_try_load()){ disk_ok=0; g_nvx_note="Old NVXFS disk could not be read - not touched"; return 0; }
        g_nvx_have_tree=(g_nvx.dcount>0); g_dtree_init=0; dt_init();
    } else if(magic==NVX_MAGIC){
        if(nvx_mount_old()){ disk_ok=0; return 0; }
        static char tb[2097152];
        for(u32 i=0;i<g_nvx.count;i++){
            int len=(int)g_nvx.e[i].len; if(len>(int)sizeof(tb)-1)len=(int)sizeof(tb)-1; if(len<0)len=0;
            if(g_nvx.e[i].start<g_nvx_base+8)continue;
            if(len&&nvx_rdn(g_nvx.e[i].start,(len+511)/512,(u8*)tb)){ disk_ok=0; g_nvx_note="Old NVXFS disk could not be read - not touched"; return 0; }
            int slot=-1; for(int j=0;j<FS_N;j++) if(FS[j].used&&FS[j].dir==(int)g_nvx.e[i].dir&&seq(FS[j].name,g_nvx.e[i].name)){slot=j;break;}
            if(slot<0){ for(int j=0;j<FS_N;j++) if(!FS[j].used){slot=j;break;} if(slot<0)break; fs_newslot(slot); }
            if(fs_set(slot,tb,len)){ disk_ok=0; return 0; }
            int k=0; while(g_nvx.e[i].name[k]&&k<63){FS[slot].name[k]=g_nvx.e[i].name[k];k++;} FS[slot].name[k]=0;
            FS[slot].used=1; FS[slot].folder=(int)g_nvx.e[i].folder;
            FS[slot].dir=(int)g_nvx.e[i].dir; if(FS[slot].dir<0||FS[slot].dir>=NVX_DN_MAX)FS[slot].dir=1;
            FS[slot].mode=g_nvx.e[i].mode?g_nvx.e[i].mode:0100644u; FS[slot].uid=(int)g_nvx.e[i].uid; FS[slot].gid=(int)g_nvx.e[i].gid; FS[slot].nlink=1;
            n4_reserve_secs(g_nvx.e[i].start-g_nvx_base,(u32)(len+511)/512);
        }
        g_nvx_have_tree=(g_nvx.dcount>0); g_dtree_init=0; dt_init();
    } else if(how!=1&&!nvx_is_zero(d)){ disk_ok=0; g_nvx_note="Unrecognised data - not touched"; return 0; }
    else dt_init();
    int guarded=0; if(magic){ guarded=!n4_conv_backup(g_nvx_secs); if(!guarded)klog("nvx4: no room for a conversion backup - converting without one"); }
    if(n4_format(g_nvx_secs)){ if(guarded&&n4_conv_restore(g_nvx_secs)==1)n4_conv_done(g_nvx_secs); disk_ok=0; g_n4_nres=0; g_nvx_note=magic?"Old disk could not be converted - left unchanged":"Disk could not be formatted"; klog2("nvx4: format failed: ",g_n4_err); return 0; }
    if(guarded)n4_conv_done(g_nvx_secs);
    g_nvx_note=magic?"Converted to the NVX4 format":"Formatted as NVX4";
    klog2("nvx4: ",g_nvx_note);
    return 1;
}
