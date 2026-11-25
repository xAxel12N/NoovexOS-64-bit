#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/nvx/fs/vfs/volumes.h"
#include "include/nvx/fs/vfs/volfs.h"
#include "include/nvx/core/sysfeat.h"
#include "drivers/mmc/mmc_blk.h"
#include "drivers/mmc/host/sdhci_adma.h"
#include "include/nvx/fs/fat/exfat.h"
#include "include/nvx/fs/fat/vfat.h"
struct mmc_host* g_mmc_hosts[MMC_MAX_HOSTS]; int g_mmc_nhosts=0;
nvol g_vol[VOL_MAX]; int g_vol_n=0;
static u8* img; static u64 nsec; static int disk_fd_ro=0, scans=0, settles=0, feat_on=1, exfat_on=1, volwr_on=1, flushes=0, usb_syncs=0, usb_sync_fail=0, flush_cap=1;
void* kmalloc(u64 n){ return malloc(n?n:1); } void kfree(void* p){ free(p); } void klog(const char* s){ (void)s; }
static int fon(int f){ return f==SYSF_EXFAT?exfat_on:f==SYSF_VOLWRITE?volwr_on:feat_on; }
int sysfeat_get(int f){ return fon(f); } int sysfeat_state(int f){ return fon(f)?SYSF_ST_ON:SYSF_ST_OFF; } int sysfeat_begin(int f){ return fon(f); } void sysfeat_ok(int f){ (void)f; } void sysfeat_fail(int f,const char* w){ (void)f; (void)w; }
void sdhci_adma_settle(void){ settles++; } void vol_scan(int f){ (void)f; scans++; }
void read_clock(int* hh,int* mm,int* ss,int* yr,int* mo,int* dy){ *hh=12; *mm=0; *ss=0; *yr=2026; *mo=11; *dy=19; }
int mmc_read_blocks(struct mmc_host* h,u64 lba,u32 n,void* buf){ (void)h; if(lba+n>nsec)return -5; memcpy(buf,img+lba*512,(size_t)n*512); return 0; }
int mmc_write_blocks(struct mmc_host* h,u64 lba,u32 n,const void* buf){ (void)h; if(lba+n>nsec)return -5; memcpy(img+lba*512,buf,(size_t)n*512); return 0; }
static long long fail_rd=-1, fail_wr=-1; static int fail_data=0;
static int s_rd(void* c,u64 lba,int n,u8* b){ (void)c; if(fail_rd>=0&&(u64)fail_rd>=lba&&(u64)fail_rd<lba+(u64)n)return -5; return mmc_read_blocks(0,lba,(u32)n,b); }
static int s_wr(void* c,u64 lba,int n,const u8* b){ (void)c; if(fail_data&&lba!=2048)return -5; if(fail_wr>=0&&(u64)fail_wr>=lba&&(u64)fail_wr<lba+(u64)n)return -5; return mmc_write_blocks(0,lba,(u32)n,b); }
static long long find_ent(const char* n83){ for(u64 l=2048;l<nsec;l++) for(int e=0;e<16;e++) if(!memcmp(img+l*512+e*32,n83,11))return (long long)l; return -1; }
static int r_rd(unsigned long long l,int n,void* b){ return mmc_read_blocks(0,l,(u32)n,b); } static int r_wr(unsigned long long l,int n,const void* b){ return mmc_write_blocks(0,l,(u32)n,b); }
static int vf_rename_raw(const char* a,const char* b){ if(vf_mount(r_rd,r_wr,2048,nsec-2048,0))return -9; int r=vf_rename(a,b); vf_umount(); return r; }
static int vf_mkdir_raw(const char* a){ if(vf_mount(r_rd,r_wr,2048,nsec-2048,0))return -9; int r=vf_mkdir(a); vf_umount(); return r; }
static int ylast=1; static void ccb(const char* n,int d,unsigned long long sz,void* c){ (void)n; (void)d; (void)sz; (void)c; ylast=volfs_usb_claim(1); }
static void ycb(const char* n,int d,unsigned long long sz,void* c){ (void)n; (void)d; (void)sz; (void)c; ylast=volfs_usb_yield(); }
int gdisk_read_on(int i,u64 lba,int cnt,u8* buf){ (void)i; return mmc_read_blocks(0,lba,(u32)cnt,buf); }
int gdisk_writen_on(int i,u64 lba,int cnt,const u8* buf){ (void)i; if(disk_fd_ro)return -1; return mmc_write_blocks(0,lba,(u32)cnt,buf); }
int gdisk_flush_on(int i){ (void)i; flushes++; return 0; } int gdisk_flush_cap_of(int i){ (void)i; return flush_cap; }
int usb_msd_rdn(u32 l,int n,u8* b){ return mmc_read_blocks(0,l,(u32)n,b); } int usb_msd_wrn(u32 l,int n,const u8* b){ return mmc_write_blocks(0,l,(u32)n,b); } int usb_msd_sync(void){ usb_syncs++; return usb_sync_fail?-1:0; } static int ufat_syncs=0, ufat_mounts=0; void ufat_sync(void){ ufat_syncs++; } void ufat_mount(void){ ufat_mounts++; } int gdisk_ratio_of(int i){ (void)i; return 1; } int gdisk_current(void){ return 0; }
int nk_blk_rd(unsigned long long l,int n,void* b){ return mmc_read_blocks(0,l,(u32)n,b); } int nk_blk_wr(unsigned long long l,int n,const void* b){ (void)l; (void)n; (void)b; return -1; }
void gdisk_ensure(void){} u32 dk_ratio(void){ return 1; } int gdisk_flush(void){ return 0; }
int vol_mount_ext4(int i){ extern int ext4k_mount_disk(int,unsigned long long,unsigned long long,int); return ext4k_mount_disk(g_vol[i].disk,g_vol[i].start,g_vol[i].psz,0); }
void vol_name(const nvol* v,char* out,int cap){ (void)v; snprintf(out,cap,"vol"); }
int ext4k_rename(const char*,const char*);
static int fails=0, checks=0;
#define CK(c,m) do{ checks++; if(!(c)){ fails++; printf("FAIL %s\n",m); } }while(0)
static char seen[4096];
static void lscb(const char* n,int d,unsigned long long sz,void* c){ (void)c; char t[128]; snprintf(t,sizeof t,"%s%s:%llu;",n,d?"/":"",d?0ULL:sz); strncat(seen,t,sizeof seen-strlen(seen)-1); }
static int load(const char* p){ FILE* f=fopen(p,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); nsec=(u64)ftell(f)/512; fseek(f,0,SEEK_SET); img=malloc(nsec*512); if(fread(img,512,nsec,f)!=nsec)return -1; fclose(f); return 0; }
static int save(const char* p){ FILE* f=fopen(p,"wb"); if(!f)return -1; fwrite(img,512,nsec,f); fclose(f); return 0; }
static void t_adma(void){ u32 t[16];
    CK(sdhci_adma2_fill(t,8,0x100000,4096)==1&&t[0]==((4096u<<16)|0x23)&&t[1]==0x100000,"one 4 KiB descriptor with END");
    CK(sdhci_adma2_fill(t,8,0x200000,131072)==2&&t[0]==0x21&&t[1]==0x200000&&t[2]==0x23&&t[3]==0x210000,"128 KiB split into two 64 KiB descriptors (length 0 = 65536), END on the last");
    CK(sdhci_adma2_fill(t,8,0x200000,65536+512)==2&&(t[2]>>16)==512&&(t[2]&2),"tail descriptor length");
    CK(sdhci_adma2_fill(t,8,0x200002,512)==-1,"unaligned address refused"); CK(sdhci_adma2_fill(t,8,0x200000,510)==-1,"length not a multiple of 4 refused");
    CK(sdhci_adma2_fill(t,8,0xFFFFF000ULL,8192)==-1,"buffer crossing 4 GiB refused"); CK(sdhci_adma2_fill(t,1,0x1000,65540)==-1,"table overflow refused"); CK(sdhci_adma2_fill(t,8,0x1000,0)==-1,"empty transfer refused"); }
static void t_fat(const char* in,const char* out){
    CK(!load(in),"load card image"); int k=mmcblk_add(MMCBLK_SDHC,"SIM",nsec,0,0,s_rd,s_wr); CK(k==0,"card registered");
    nvol* v=&g_vol[0]; memset(v,0,sizeof *v); v->kind=VOL_SD; v->disk=VOL_SD_DISK0; v->start=2048; v->psz=nsec-2048; strcpy(v->fs,"FAT32"); g_vol_n=1;
    feat_on=0; CK(volfs_open(0)==VOLFS_E_OFF,"feature off refuses"); feat_on=1;
    { int r=volfs_open(0); if(r)printf("open %d vf %d\n",r,(int)g_mmcblk[0].nerr); CK(r==VOLFS_OK&&volfs_kind()==VOLFS_FAT&&!volfs_ro(),"FAT32 card opens read-write"); }
    seen[0]=0; CK(!volfs_ls("/",lscb,0)&&strstr(seen,"README.TXT:12;")&&strstr(seen,"PHOTOS/"),"root listing shows mkfs files");
    char b[70000]; CK(volfs_read("/README.TXT",b,sizeof b)==12&&!memcmp(b,"hello card!\n",12),"read file");
    for(int i=0;i<(int)sizeof b;i++)b[i]=(char)(i*13);
    CK(volfs_write("/Long name copied in.bin",b,sizeof b)==(long)sizeof b,"copy a 70 KB file in (long name)");
    CK(volfs_size("/Long name copied in.bin")==(long)sizeof b,"size of copied file");
    CK(volfs_mkdir("/New folder")==VOLFS_OK,"mkdir");
    CK(volfs_rename("/Long name copied in.bin","/renamed.bin")==VOLFS_OK,"rename in place");
    CK(volfs_rename("/renamed.bin","/PHOTOS/moved.bin")==VOLFS_OK,"move into folder");
    CK(volfs_rename("/README.TXT","/PHOTOS/moved.bin")!=VOLFS_OK,"rename onto existing refused");
    char r[70000]; CK(volfs_read("/PHOTOS/moved.bin",r,sizeof r)==(long)sizeof b&&!memcmp(r,b,sizeof b),"moved file content intact");
    CK(volfs_delete("/README.TXT")==VOLFS_OK&&volfs_size("/README.TXT")<0,"delete");
    CK(volfs_delete("/PHOTOS")!=VOLFS_OK,"non-empty folder delete refused");
    CK(volfs_mkdir("/PHOTOS/Inner")==VOLFS_OK&&volfs_write("/PHOTOS/Inner/f.txt","inner",5)==5,"folder with a file inside");
    CK(volfs_rename("/PHOTOS/Inner","/New folder/Inner")==VOLFS_OK,"move a folder to another parent");
    CK(volfs_read("/New folder/Inner/f.txt",b,64)==5&&!memcmp(b,"inner",5),"file inside the moved folder");
    CK(volfs_rename("/New folder","/New folder/Inner/loop")!=VOLFS_OK,"moving a folder into itself refused");
    { long long l=find_ent("F       TXT"); CK(l>0,"found the inner folder's sector"); fail_rd=l; CK(volfs_delete("/New folder/Inner")!=VOLFS_OK,"FAT: a read error while checking that a folder is empty refuses the delete"); fail_rd=-1;
      seen[0]=0; CK(!volfs_ls("/New folder",lscb,0)&&strstr(seen,"Inner/")&&volfs_read("/New folder/Inner/f.txt",b,64)==5,"FAT: folder and its file intact after the failed delete"); }
    { CK(volfs_write("/PHOTOS/KILLME.TXT","kill",4)==4,"file for the rename fault"); long long l=find_ent("KILLME  TXT"); CK(l>0,"found its directory sector"); fail_wr=l;
      CK(volfs_rename("/PHOTOS/KILLME.TXT","/New folder/KILLME.TXT")!=VOLFS_OK,"FAT: rename fails when the old entry cannot be removed"); fail_wr=-1;
      CK(volfs_size("/New folder/KILLME.TXT")<0&&volfs_read("/PHOTOS/KILLME.TXT",b,64)==4&&!memcmp(b,"kill",4),"FAT: the new entry is removed again, so no two entries share one cluster chain"); }
    CK(vf_rename_raw("/PHOTOS","/..")!=0&&vf_mkdir_raw("/..")!=0&&vf_mkdir_raw("/.")!=0,"FAT core: '..' and '.' leaves refused in the root (direct vf_rename/vf_mkdir)");
    { u8* bs=img+2048*512; u32 f1; memcpy(&f1,img+(2048+bs[14]+bs[15]*256)*512+4,4); CK((f1&0x08000000u)&&!(bs[65]&1),"FAT clean after each write (FAT[1] bit 27 set, boot dirty bit clear)"); }
    char j[256]; CK(volfs_join(j,256,"/PHOTOS","x")>0&&!strcmp(j,"/PHOTOS/x")&&volfs_join(j,256,"/","a/b")<0,"path join");
    CK(volfs_join(j,256,"/","..")<0&&volfs_join(j,256,"/",".")<0&&volfs_join(j,256,"/","")<0&&volfs_join(j,256,"/","a\nb")<0,"join refuses empty, '.', '..' and control characters");
    CK(volfs_rename("/New folder","/..")==VOLFS_E_ARG&&volfs_rename("/New folder","/.")==VOLFS_E_ARG&&volfs_mkdir("/..")==VOLFS_E_ARG&&volfs_mkdir("/PHOTOS/../evil")==VOLFS_E_ARG&&volfs_delete("/PHOTOS/..")==VOLFS_E_ARG&&volfs_write("/../x",b,1)==VOLFS_E_ARG&&volfs_write("rel.txt",b,1)==VOLFS_E_ARG,"FAT: '.'/'..' components and relative paths refused");
    seen[0]=0; CK(!volfs_ls("/",lscb,0)&&strstr(seen,"New folder/")&&!strstr(seen,"evil"),"FAT: listing intact after refused dot paths");
    CK(volfs_vol()==0,"open volume index reported");
    { u64 s0=v->start; v->start=s0+8; CK(volfs_vol()==-1,"after a rescan moves another partition into the same slot the open volume is no longer reported as that slot"); v->start=s0; CK(volfs_vol()==0,"slot matches again"); }
    CK(settles>0,"ADMA gate settled after card I/O");
    volfs_close(); CK(!save(out),"save image");
    g_mmcblk[0].ro=1; CK(volfs_open(0)==VOLFS_OK&&volfs_ro(),"write-protected card opens read-only");
    CK(volfs_write("/x.txt","x",1)==VOLFS_E_RO&&volfs_delete("/New folder")==VOLFS_E_RO&&volfs_rename("/New folder","/y")==VOLFS_E_RO&&volfs_mkdir("/z")==VOLFS_E_RO,"read-only card refuses changes");
    seen[0]=0; CK(!volfs_ls("/",lscb,0)&&strstr(seen,"New folder/")&&!strstr(seen,"README"),"listing after reopen");
    volfs_close(); g_mmcblk[0].ro=0;
    img[2048*512+65]|=1; CK(volfs_open(0)==VOLFS_OK&&volfs_ro()&&volfs_unclean(),"FAT card with the dirty bit opens read-only (unclean eject)");
    CK(volfs_write("/x.txt","x",1)==VOLFS_E_RO,"unclean FAT card refuses writes"); volfs_close(); img[2048*512+65]&=~1;
    CK(volfs_open(0)==VOLFS_OK&&!volfs_ro()&&!volfs_unclean(),"clean card read-write again"); volfs_close();
    CK(volfs_eject(0)==VOLFS_OK&&!mmcblk_present(0)&&scans==1,"safe eject"); CK(volfs_open(0)!=VOLFS_OK,"ejected card cannot be opened");
    strcpy(v->fs,"exFAT"); g_mmcblk[0].ejected=0; CK(volfs_open(0)==VOLFS_E_MOUNT,"FAT volume labelled exFAT does not mount as exFAT"); free(img); }
static void t_ext4(const char* in,const char* out){
    CK(!load(in),"load ext4 image"); nvol* v=&g_vol[0]; memset(v,0,sizeof *v); v->kind=VOL_FIXED; v->disk=0; v->start=0; v->psz=nsec; strcpy(v->fs,"ext4"); g_vol_n=1;
    CK(volfs_open(0)==VOLFS_OK&&volfs_kind()==VOLFS_EXT4,"ext4 disk opens through the Files path");
    seen[0]=0; CK(!volfs_ls("/",lscb,0)&&strstr(seen,"hello.txt:6;")&&strstr(seen,"docs/"),"ext4 root listing");
    seen[0]=0; CK(!volfs_ls("/docs",lscb,0)&&strstr(seen,"a.txt:3;"),"ext4 subfolder listing");
    char b[64]; CK(volfs_read("/hello.txt",b,64)==6&&!memcmp(b,"hello\n",6),"ext4 read");
    if(!volfs_ro()){ CK(volfs_write("/docs/new.txt","new file",8)==8,"ext4 copy in"); CK(volfs_rename("/docs/new.txt","/moved.txt")==VOLFS_OK,"ext4 rename"); CK(volfs_delete("/hello.txt")==VOLFS_OK,"ext4 delete"); CK(volfs_mkdir("/made")==VOLFS_OK,"ext4 mkdir");
        CK(volfs_rename("/docs/..","/docs/x")==VOLFS_E_ARG&&ext4k_rename("/docs/..","/docs/x")!=0&&ext4k_rename("/docs","/docs/..")!=0&&ext4k_rename("/made","/docs/.")!=0,"ext4: renaming '.'/'..' entries refused (volfs and ext4 core)");
        CK(volfs_write("/docs/a.txt","replaced",8)==8&&volfs_read("/docs/a.txt",b,64)==8&&!memcmp(b,"replaced",8),"ext4 overwrite goes through a temp file and rename");
        seen[0]=0; CK(!volfs_ls("/docs",lscb,0)&&!strstr(seen,".~nvx"),"ext4: no temp file left after overwrite");
        { long big=40L<<20; char* z=malloc((size_t)big); memset(z,7,(size_t)big);
          CK(volfs_write("/big.bin",z,big)==VOLFS_E_IO&&volfs_size("/big.bin")<0,"ext4: a copy that runs out of space leaves no partial file");
          CK(volfs_write("/docs/a.txt",z,big)==VOLFS_E_IO&&volfs_read("/docs/a.txt",b,64)==8&&!memcmp(b,"replaced",8),"ext4: a failed overwrite keeps the old content");
          seen[0]=0; CK(!volfs_ls("/docs",lscb,0)&&!strstr(seen,".~nvx"),"ext4: no temp file left after a failed overwrite"); free(z); } }
    else printf("ext4 opened read-only (EXT4_RW_VERIFIED 0)\n");
    volfs_close(); CK(!save(out),"save ext4 image");
    disk_fd_ro=1; free(img); }
static u8* xfboot(void){ return img+2048*512; }
static void t_exfat(const char* in,const char* out){
    CK(!load(in),"load exFAT card"); CK(mmcblk_add(MMCBLK_SDHC,"SIM",nsec,0,0,s_rd,s_wr)==0,"exFAT card registered");
    nvol* v=&g_vol[0]; memset(v,0,sizeof *v); v->kind=VOL_SD; v->disk=VOL_SD_DISK0; v->start=2048; v->psz=nsec-2048; strcpy(v->fs,"exFAT"); g_vol_n=1;
    exfat_on=0; CK(volfs_open(0)==VOLFS_E_OFF,"exFAT feature off refuses"); exfat_on=1;
    CK(volfs_open(0)==VOLFS_OK&&volfs_kind()==VOLFS_EXFAT&&!volfs_ro()&&!volfs_unclean(),"exFAT card opens read-write");
    seen[0]=0; CK(!volfs_ls("/",lscb,0)&&!seen[0],"fresh exFAT root is empty");
    CK(volfs_write("/hello.txt","hello exfat\n",12)==12,"write small file"); CK(!(xfboot()[106]&2),"VolumeDirty cleared after the write");
    char b[200000], r[200000]; CK(volfs_read("/hello.txt",b,64)==12&&!memcmp(b,"hello exfat\n",12),"read small file");
    for(int i=0;i<70000;i++)b[i]=(char)(i*7+3);
    const char* ln="/L\xc3\xa5ngt namn \xc3\x85\xc3\x84\xc3\x96 with a name longer than fifteen chars.bin";
    CK(volfs_write(ln,b,70000)==70000&&volfs_size(ln)==70000,"write 70 KB file with a long UTF-8 name");
    CK(volfs_read(ln,r,sizeof r)==70000&&!memcmp(r,b,70000),"read it back");
    CK(volfs_size("/L\xc3\xa5NGT NAMN \xc3\xa5\xc3\xa4\xc3\xb6 WITH A NAME LONGER THAN FIFTEEN CHARS.BIN")==70000,"case-insensitive lookup through the up-case table");
    CK(volfs_mkdir("/Photos")==VOLFS_OK&&volfs_mkdir("/Photos/2026")==VOLFS_OK,"mkdir nested"); CK(volfs_mkdir("/Photos")!=VOLFS_OK,"mkdir existing refused");
    int okw=1; for(int i=0;i<60;i++){ char p[64]; snprintf(p,64,"/Photos/2026/picture number %02d.jpg",i); char d[32]; int n=snprintf(d,32,"pic %d",i); if(volfs_write(p,d,n)!=n)okw=0; }
    CK(okw,"60 files in one folder (the folder grows past one cluster)");
    seen[0]=0; volfs_ls("/Photos/2026",lscb,0); int cnt=0; for(char* q=seen;(q=strstr(q,"picture number"));q++)cnt++; CK(cnt==60,"all 60 listed");
    CK(volfs_read("/Photos/2026/picture number 59.jpg",r,64)==6&&!memcmp(r,"pic 59",6),"last file in the grown folder");
    for(int i=0;i<5000;i++)b[i]=(char)(i^0x5A); CK(volfs_write("/HELLO.TXT",b,5000)==5000&&volfs_size("/hello.txt")==5000,"overwrite through another case");
    CK(volfs_rename("/hello.txt","/Photos/hi.txt")==VOLFS_OK&&volfs_read("/Photos/hi.txt",r,8000)==5000&&!memcmp(r,b,5000),"move file to a folder");
    CK(volfs_rename(ln,"/Photos/hi.txt")!=VOLFS_OK,"rename onto existing refused");
    CK(volfs_rename("/Photos/2026","/2026 moved")==VOLFS_OK,"move folder to another parent");
    seen[0]=0; volfs_ls("/2026 moved",lscb,0); cnt=0; for(char* q=seen;(q=strstr(q,"picture number"));q++)cnt++; CK(cnt==60,"moved folder keeps its files");
    CK(volfs_rename("/Photos","/Photos/inside")!=VOLFS_OK,"folder into itself refused");
    CK(volfs_write("/bad:name.txt","x",1)<0,"illegal name refused");
    for(int i=0;i<40960;i++)b[i]=(char)(i*3);
    CK(volfs_write("/A.bin",b,40960)==40960&&volfs_write("/B.bin",b,40960)==40960&&volfs_write("/C.bin",b,40960)==40960,"three 40 KB files");
    long fill=(long)xf_free_bytes()-8192; char* big=calloc(1,fill); CK(big&&volfs_write("/fill.bin",big,fill)==fill,"fill the card"); free(big);
    CK(volfs_delete("/B.bin")==VOLFS_OK,"delete middle file (hole)");
    for(int i=0;i<45056;i++)b[i]=(char)(i*11+1); CK(volfs_write("/D.bin",b,45056)==45056,"fragmented write (FAT chain)");
    CK(volfs_read("/D.bin",r,sizeof r)==45056&&!memcmp(r,b,45056),"fragmented file reads back");
    CK(volfs_write("/E.bin",b,65536)<0,"write larger than free space refused");
    CK(volfs_delete("/fill.bin")==VOLFS_OK,"delete big file");
    CK(volfs_delete("/2026 moved")!=VOLFS_OK,"non-empty folder delete refused");
    CK(volfs_mkdir("/empty")==VOLFS_OK&&volfs_delete("/empty")==VOLFS_OK,"delete empty folder");
    CK(volfs_write("/zero.txt","",0)==0&&volfs_size("/zero.txt")==0,"empty file");
    volfs_close(); CK(!(xfboot()[106]&2),"VolumeDirty clear after close"); CK(!save(out),"save exFAT image");
    CK(volfs_open(0)==VOLFS_OK,"reopen"); CK(volfs_read("/D.bin",r,sizeof r)==45056&&!memcmp(r,b,45056),"data persists across mounts");
    seen[0]=0; CK(!volfs_ls("/",lscb,0)&&strstr(seen,"Photos/")&&strstr(seen,"2026 moved/")&&strstr(seen,"zero.txt:0;")&&!strstr(seen,"fill.bin"),"root listing after reopen"); volfs_close();
    xfboot()[106]|=2; CK(volfs_open(0)==VOLFS_OK&&((!volfs_ro()&&!volfs_unclean())||(volfs_ro()&&volfs_unclean())),"exFAT with VolumeDirty is repaired at mount, or opens read-only if repair is in doubt");
    CK(volfs_ro()?volfs_write("/x","x",1)==VOLFS_E_RO:volfs_write("/x","x",1)==1,"unclean exFAT: writes refused while read-only, accepted after repair"); volfs_close(); xfboot()[106]&=~2;
    g_mmcblk[0].ro=1; CK(volfs_open(0)==VOLFS_OK&&volfs_ro()&&volfs_mkdir("/q")==VOLFS_E_RO,"write-protected exFAT card is read-only"); volfs_close(); g_mmcblk[0].ro=0;
    CK(volfs_open(0)==VOLFS_OK&&volfs_mkdir("/..")==VOLFS_E_ARG&&volfs_rename("/Photos","/Photos/../x")==VOLFS_E_ARG&&volfs_write("/./a","a",1)==VOLFS_E_ARG,"exFAT: '.'/'..' components refused");
    fail_data=1; CK(volfs_write("/werr.bin",b,40960)==VOLFS_E_IO&&volfs_ro(),"exFAT: a device write error makes the volume read-only");
    fail_data=0; CK(volfs_mkdir("/after")==VOLFS_E_RO&&strstr(volfs_why(VOLFS_E_RO),"write error")&&(xfboot()[106]&2),"exFAT: later writes refused with a clear reason, VolumeDirty kept");
    volfs_close(); CK(volfs_open(0)==VOLFS_OK&&((!volfs_ro()&&!volfs_unclean())||(volfs_ro()&&volfs_unclean()))&&!strstr(volfs_why(VOLFS_E_RO),"write error"),"exFAT: next mount repairs the volume or stays read-only, write-error state reset"); volfs_close(); xfboot()[106]&=~2;
    CK(volfs_open(0)==VOLFS_OK&&!volfs_ro(),"exFAT: writable again after the check");
    CK(volfs_mkdir("/EFI")==VOLFS_OK,"exFAT EFI folder"); volfs_close();
    v->kind=VOL_FIXED; v->disk=0; CK(volfs_open(0)==VOLFS_OK&&volfs_kind()==VOLFS_EXFAT&&volfs_ro()&&volfs_esp(),"fixed-disk exFAT volume with an EFI folder opens read-only"); volfs_close(); v->kind=VOL_SD; v->disk=VOL_SD_DISK0;
    free(img); }
static void t_fixed(const char* in,int usb){
    CK(!load(in),"load FAT disk"); nvol* v=&g_vol[0]; memset(v,0,sizeof *v); v->kind=usb?VOL_USB:VOL_FIXED; v->disk=usb?-1:0; v->start=2048; v->psz=nsec-2048; strcpy(v->fs,"FAT32"); g_vol_n=1;
    const char* w=usb?"USB stick":"fixed disk"; char m[128];
    volwr_on=0; snprintf(m,128,"%s read-only when writing is off",w); CK(volfs_open(0)==VOLFS_OK&&volfs_ro(),m); volfs_close(); volwr_on=1;
    if(usb){ usb_sync_fail=1; CK(volfs_open(0)==VOLFS_OK&&volfs_ro(),"USB stick without SYNCHRONIZE CACHE stays read-only"); volfs_close(); usb_sync_fail=0; }
    else { flush_cap=0; CK(volfs_open(0)==VOLFS_OK&&volfs_ro(),"fixed disk without cache flush stays read-only"); volfs_close(); flush_cap=1; }
    snprintf(m,128,"%s opens read-write",w); CK(volfs_open(0)==VOLFS_OK&&!volfs_ro(),m);
    int f0=usb?usb_syncs:flushes; snprintf(m,128,"%s write + mkdir",w); CK(volfs_write("/from os.txt","noovex",6)==6&&volfs_mkdir("/dir")==VOLFS_OK&&volfs_rename("/from os.txt","/dir/moved.txt")==VOLFS_OK,m);
    snprintf(m,128,"%s cache flushed after every change",w); CK((usb?usb_syncs:flushes)-f0>=6,m);
    if(usb)CK(ufat_syncs>=4&&ufat_mounts>=3,"USB view (ufat) synced before and reloaded after each change");
    char b[16]; CK(volfs_read("/dir/moved.txt",b,16)==6&&!memcmp(b,"noovex",6),"read back");
    if(usb){ ylast=1; CK(!volfs_ls("/",ycb,0)&&ylast==-1&&volfs_kind()==VOLFS_FAT,"USB stick: a direct ufat write is refused while volfs is in the middle of an operation");
        CK(volfs_usb_yield()==0&&volfs_kind()==VOLFS_NONE&&volfs_vol()==-1,"USB stick: volfs closes the stick before a direct ufat write (one writer at a time)");
        CK(volfs_open(0)==VOLFS_OK&&!volfs_ro()&&volfs_read("/dir/moved.txt",b,16)==6,"USB stick reopens after the direct write");
        CK(volfs_usb_claim(1)==0&&volfs_usb_owned()&&volfs_kind()==VOLFS_NONE,"updater claims the USB stick: volfs closes it first");
        CK(volfs_open(0)==VOLFS_E_BUSY&&volfs_kind()==VOLFS_NONE&&strstr(volfs_why(VOLFS_E_BUSY),"updater"),"Files cannot open the USB stick while the updater writes to it");
        CK(volfs_usb_yield()==-1,"direct ufat writes are refused while the updater owns the stick");
        CK(volfs_usb_claim(1)==0,"claim is idempotent for the updater's repeated mounts");
        CK(volfs_usb_claim(0)==0&&!volfs_usb_owned()&&volfs_open(0)==VOLFS_OK&&!volfs_ro(),"after the updater releases it, Files opens the stick read-write again");
        ylast=1; CK(!volfs_ls("/",ccb,0)&&ylast==-1&&!volfs_usb_owned()&&volfs_kind()==VOLFS_FAT,"updater claim is refused while a volfs operation is running"); }
    else CK(volfs_usb_yield()==0&&volfs_kind()==VOLFS_FAT,"fixed disk is not closed by a USB write");
    volfs_close();
    { u8 m0[1536]; memcpy(m0,img,1536); v->part=1;
      img[446+4]=0xEF; CK(volfs_open(0)==VOLFS_OK&&volfs_ro()&&volfs_esp(),"MBR EFI system partition (type 0xEF) opens read-only");
      CK(volfs_write("/esp.txt","x",1)==VOLFS_E_RO&&volfs_mkdir("/espd")==VOLFS_E_RO,"EFI system partition refuses changes"); volfs_close();
      img[446+4]=0xEE; memset(img+512,0,1024); memcpy(img+512,"EFI PART",8); img[512+72]=2; img[512+80]=128; img[512+84]=128;
      static const u8 eg[16]={0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B}; memcpy(img+1024,eg,16);
      CK(volfs_open(0)==VOLFS_OK&&volfs_ro()&&volfs_esp(),"GPT EFI system partition opens read-only"); volfs_close();
      img[1024]=0xA2; img[1025]=0xA0; img[1026]=0xD0; img[1027]=0xEB; CK(volfs_open(0)==VOLFS_OK&&!volfs_ro()&&!volfs_esp(),"GPT basic data partition stays read-write"); volfs_close();
      memcpy(img,m0,1536); v->part=0; }
    CK(volfs_open(0)==VOLFS_OK&&!volfs_ro()&&volfs_mkdir("/EFI")==VOLFS_OK,"make an EFI folder"); volfs_close();
    if(usb)CK(volfs_open(0)==VOLFS_OK&&!volfs_ro()&&!volfs_esp(),"USB stick with an EFI folder stays read-write");
    else CK(volfs_open(0)==VOLFS_OK&&volfs_ro()&&volfs_esp()&&volfs_write("/x.txt","x",1)==VOLFS_E_RO,"fixed-disk FAT volume with an EFI folder (ESP with another partition type) opens read-only");
    volfs_close(); free(img); }
static void t_ext4sd(const char* in,const char* out){
    CK(!load(in),"load ext4 card"); CK(mmcblk_add(MMCBLK_SDHC,"SIM",nsec,0,0,s_rd,s_wr)==0,"ext4 card registered");
    nvol* v=&g_vol[0]; memset(v,0,sizeof *v); v->kind=VOL_SD; v->disk=VOL_SD_DISK0; v->start=0; v->psz=nsec; strcpy(v->fs,"ext4"); g_vol_n=1;
    CK(volfs_open(0)==VOLFS_OK&&volfs_kind()==VOLFS_EXT4,"ext4 SD card opens");
    seen[0]=0; CK(!volfs_ls("/",lscb,0)&&strstr(seen,"hello.txt:6;"),"ext4 SD listing"); char b[16]; CK(volfs_read("/hello.txt",b,16)==6,"ext4 SD read");
    if(!volfs_ro())CK(volfs_write("/sd.txt","sd",2)==2&&volfs_mkdir("/sdd")==VOLFS_OK,"ext4 SD write"); else printf("ext4 SD opened read-only (EXT4_RW_VERIFIED 0)\n");
    volfs_close(); CK(settles>0,"ADMA gate settled after ext4 SD I/O"); CK(!save(out),"save ext4 card"); free(img); }
int main(int argc,char** argv){
    t_adma();
    if(argc>=4&&!strcmp(argv[1],"fat"))t_fat(argv[2],argv[3]);
    if(argc>=4&&!strcmp(argv[1],"ext4"))t_ext4(argv[2],argv[3]);
    if(argc>=4&&!strcmp(argv[1],"exfat"))t_exfat(argv[2],argv[3]);
    if(argc>=3&&!strcmp(argv[1],"fixed"))t_fixed(argv[2],0);
    if(argc>=3&&!strcmp(argv[1],"usb"))t_fixed(argv[2],1);
    if(argc>=4&&!strcmp(argv[1],"ext4sd"))t_ext4sd(argv[2],argv[3]);
    printf("%d checks, %d failures\n%s\n",checks,fails,fails?"FAILED":"ALL PASS"); return fails?1:0; }
