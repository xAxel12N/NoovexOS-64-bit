#include "config.h"
#include "types.h"
#include "device.h"
#include "volume.h"
#include "inode.h"
#include "attrib.h"
#include "dir.h"
#include "unistr.h"
#include "layout.h"
#include "logging.h"
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "nvxntfs.h"

typedef struct { nvxblk_rd rd; nvxblk_wr wr; unsigned long long base, nsect; s64 pos; } nvxdev;
static nvxdev g_dev; static ntfs_volume* g_vol; static unsigned char g_bounce[4096];

static nvxdev* DP(struct ntfs_device* d){ return d->d_private?(nvxdev*)d->d_private:&g_dev; }
static int nd_open(struct ntfs_device* d,int flags){ (void)flags; DP(d)->pos=0; NDevSetOpen(d); return 0; }
static int nd_close(struct ntfs_device* d){ NDevClearOpen(d); return 0; }
static s64 nd_size(nvxdev* p){ return (s64)(p->nsect*512ULL); }
static s64 nd_seek(struct ntfs_device* d,s64 off,int whence){ nvxdev* p=DP(d); s64 np=whence==SEEK_SET?off:whence==SEEK_CUR?p->pos+off:nd_size(p)+off; if(np<0){ errno=EINVAL; return -1; } p->pos=np; return np; }
static s64 nd_pread(struct ntfs_device* d,void* buf,s64 count,s64 off){
    nvxdev* p=DP(d); unsigned char* out=buf; s64 done=0;
    if(off<0||off+count>nd_size(p)){ if(off>=nd_size(p))return 0; count=nd_size(p)-off; }
    while(done<count){ unsigned long long lba=(unsigned long long)(off+done)/512; int so=(int)((off+done)%512);
        if(so==0&&count-done>=512){ int n=(int)((count-done)/512); if(n>8)n=8; if(p->rd(p->base+lba,n,out+done)){ errno=EIO; return done?done:-1; } done+=(s64)n*512; continue; }
        if(p->rd(p->base+lba,1,g_bounce)){ errno=EIO; return done?done:-1; }
        int k=512-so; if(k>count-done)k=(int)(count-done); memcpy(out+done,g_bounce+so,k); done+=k; }
    return done;
}
static s64 nd_pwrite(struct ntfs_device* d,const void* buf,s64 count,s64 off){
    nvxdev* p=DP(d); const unsigned char* in=buf; s64 done=0;
    if(!p->wr||NDevReadOnly(d)){ errno=EROFS; return -1; }
    if(off<0||off+count>nd_size(p)){ errno=ENOSPC; return -1; }
    while(done<count){ unsigned long long lba=(unsigned long long)(off+done)/512; int so=(int)((off+done)%512);
        if(so==0&&count-done>=512){ int n=(int)((count-done)/512); if(n>8)n=8; if(p->wr(p->base+lba,n,in+done)){ errno=EIO; return done?done:-1; } done+=(s64)n*512; continue; }
        if(p->rd(p->base+lba,1,g_bounce)){ errno=EIO; return done?done:-1; }
        int k=512-so; if(k>count-done)k=(int)(count-done); memcpy(g_bounce+so,in+done,k);
        if(p->wr(p->base+lba,1,g_bounce)){ errno=EIO; return done?done:-1; } done+=k; }
    NDevSetDirty(d); return done;
}
static s64 nd_read(struct ntfs_device* d,void* buf,s64 count){ nvxdev* p=DP(d); s64 r=nd_pread(d,buf,count,p->pos); if(r>0)p->pos+=r; return r; }
static s64 nd_write(struct ntfs_device* d,const void* buf,s64 count){ nvxdev* p=DP(d); s64 r=nd_pwrite(d,buf,count,p->pos); if(r>0)p->pos+=r; return r; }
static int nd_sync(struct ntfs_device* d){ NDevClearDirty(d); return 0; }
static int nd_stat(struct ntfs_device* d,struct stat* st){ memset(st,0,sizeof(*st)); st->st_mode=S_IFBLK; st->st_size=nd_size(DP(d)); return 0; }
static int nd_ioctl(struct ntfs_device* d,unsigned long req,void* arg){ (void)d;(void)req;(void)arg; errno=ENOTTY; return -1; }
struct ntfs_device_operations ntfs_device_unix_io_ops={ .open=nd_open,.close=nd_close,.seek=nd_seek,.read=nd_read,.write=nd_write,.pread=nd_pread,.pwrite=nd_pwrite,.sync=nd_sync,.stat=nd_stat,.ioctl=nd_ioctl };

static void (*g_logfn)(const char*);
static int nvx_loghandler(const char* fn,const char* file,int line,u32 level,void* data,const char* fmt,va_list ap){
    (void)fn;(void)file;(void)line;(void)level;(void)data; char b[200]; int n=vsnprintf(b,sizeof(b),fmt,ap);
    int k=(int)strlen(b); while(k>0&&(b[k-1]=='\n'||b[k-1]=='\r'))b[--k]=0; if(g_logfn&&k)g_logfn(b); return n; }
void nvxntfs_set_logger(void (*fn)(const char*)){ g_logfn=fn; ntfs_log_set_handler(nvx_loghandler); }
int nvxntfs_mounted(void){ return g_vol!=0; }
const char* nvxntfs_label(void){ return (g_vol&&g_vol->vol_name)?g_vol->vol_name:""; }
int nvxntfs_mount(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,int ro){
    if(g_vol)nvxntfs_umount();
    g_dev.rd=rd; g_dev.wr=ro?0:wr; g_dev.base=base; g_dev.nsect=nsect; g_dev.pos=0;
    struct ntfs_device* d=ntfs_device_alloc("nvxdisk",0,&ntfs_device_unix_io_ops,&g_dev); if(!d)return -1;
    g_vol=ntfs_device_mount(d,ro?NTFS_MNT_RDONLY:0);
    if(!g_vol){ ntfs_device_free(d); return -1; }
    return 0;
}
extern int mkntfs_main(int argc,char** argv);
extern void nvx_getopt_reset(void);
static char* u2s(char* p,unsigned long long v){ char t[24]; int n=0; if(!v)t[n++]='0'; while(v){ t[n++]=(char)('0'+v%10); v/=10; } while(n)*p++=t[--n]; *p=0; return p; }
int nvxntfs_format(nvxblk_rd rd,nvxblk_wr wr,unsigned long long base,unsigned long long nsect,const char* label){
    if(g_vol)nvxntfs_umount();
    g_dev.rd=rd; g_dev.wr=wr; g_dev.base=base; g_dev.nsect=nsect; g_dev.pos=0;
    static char lab[64], pstart[24], cnt[24];
    int i=0; for(;label&&label[i]&&i<32;i++)lab[i]=label[i]; lab[i]=0; if(!lab[0]){ lab[0]='N'; lab[1]='O'; lab[2]='O'; lab[3]='V'; lab[4]='E'; lab[5]='X'; lab[6]=0; }
    u2s(pstart,base); u2s(cnt,nsect);
    char* argv[]={"mkntfs","-F","-f","-q","-s","512","-p",pstart,"-H","255","-S","63","-L",lab,"nvxdisk",cnt,0};
    nvx_getopt_reset();
    return mkntfs_main(16,argv);
}
void nvxntfs_umount(void){ if(g_vol){ ntfs_umount(g_vol,FALSE); g_vol=0; } }

static void split(const char* path,char* dir,char* leaf){
    int n=(int)strlen(path), s=-1; for(int i=0;i<n;i++) if(path[i]=='/')s=i;
    if(s<=0){ strcpy(dir,"/"); strcpy(leaf,path+(s==0?1:0)); }
    else { memcpy(dir,path,s); dir[s]=0; strcpy(leaf,path+s+1); }
}
static ntfs_inode* open_path(const char* path){ return ntfs_pathname_to_inode(g_vol,NULL,path); }

typedef struct { nvxntfs_lscb cb; void* ctx; } lsctx;
static int filldir(void* dirent,const ntfschar* name,const int len,const int type,const s64 pos,const MFT_REF mref,const unsigned dt){
    (void)pos; lsctx* c=dirent; if(type==FILE_NAME_DOS)return 0;
    if(MREF(mref)<FILE_first_user&&MREF(mref)!=FILE_root)return 0;
    char* u=NULL; if(ntfs_ucstombs(name,len,&u,0)<0||!u)return 0;
    if(strcmp(u,".")&&strcmp(u,"..")){ unsigned long long sz=0; int isdir=(dt==NTFS_DT_DIR);
        if(!isdir){ ntfs_inode* ni=ntfs_inode_open(g_vol,mref); if(ni){ sz=(unsigned long long)ni->data_size; ntfs_inode_close(ni); } }
        c->cb(u,isdir,sz,c->ctx); }
    free(u); return 0;
}
int nvxntfs_ls(const char* path,nvxntfs_lscb cb,void* ctx){
    if(!g_vol)return -1; ntfs_inode* ni=open_path(path); if(!ni)return -1;
    lsctx c={cb,ctx}; s64 pos=0; int r=ntfs_readdir(ni,&pos,&c,filldir); ntfs_inode_close(ni); return r;
}
long nvxntfs_read(const char* path,char* buf,long cap){
    if(!g_vol)return -1; ntfs_inode* ni=open_path(path); if(!ni)return -1;
    ntfs_attr* na=ntfs_attr_open(ni,AT_DATA,AT_UNNAMED,0); if(!na){ ntfs_inode_close(ni); return -1; }
    s64 n=na->data_size; if(n>cap)n=cap; s64 r=ntfs_attr_pread(na,0,n,buf);
    ntfs_attr_close(na); ntfs_inode_close(ni); return (long)r;
}
long nvxntfs_write(const char* path,const char* buf,long len){
    if(!g_vol||NVolReadOnly(g_vol))return -1;
    ntfs_inode* ni=open_path(path);
    if(!ni){ char dir[256],leaf[256]; split(path,dir,leaf); ntfs_inode* dn=open_path(dir); if(!dn)return -1;
        ntfschar* u=NULL; int ul=ntfs_mbstoucs(leaf,&u); if(ul<0){ ntfs_inode_close(dn); return -1; }
        ni=ntfs_create(dn,0,u,ul,S_IFREG); free(u); ntfs_inode_close(dn); if(!ni)return -1; }
    ntfs_attr* na=ntfs_attr_open(ni,AT_DATA,AT_UNNAMED,0); if(!na){ ntfs_inode_close(ni); return -1; }
    if(ntfs_attr_truncate(na,len)){ ntfs_attr_close(na); ntfs_inode_close(ni); return -1; }
    s64 w=len?ntfs_attr_pwrite(na,0,len,buf):0;
    ntfs_attr_close(na); ntfs_inode_close(ni); return (long)w;
}
int nvxntfs_exists(const char* path){ if(!g_vol)return 0; ntfs_inode* ni=open_path(path); if(!ni)return 0; ntfs_inode_close(ni); return 1; }
int nvxntfs_mkdir(const char* path){
    if(!g_vol||NVolReadOnly(g_vol))return -1; char dir[256],leaf[256]; split(path,dir,leaf);
    ntfs_inode* dn=open_path(dir); if(!dn)return -1; ntfschar* u=NULL; int ul=ntfs_mbstoucs(leaf,&u); if(ul<0){ ntfs_inode_close(dn); return -1; }
    ntfs_inode* ni=ntfs_create(dn,0,u,ul,S_IFDIR); free(u); ntfs_inode_close(dn); if(!ni)return -1; ntfs_inode_close(ni); return 0;
}
static int unlink_name(const char* path){
    char dir[256],leaf[256]; split(path,dir,leaf);
    ntfs_inode* ni=open_path(path); if(!ni)return -1; ntfs_inode* dn=open_path(dir); if(!dn){ ntfs_inode_close(ni); return -1; }
    ntfschar* u=NULL; int ul=ntfs_mbstoucs(leaf,&u); if(ul<0){ ntfs_inode_close(ni); ntfs_inode_close(dn); return -1; }
    int r=ntfs_delete(g_vol,path,ni,dn,u,ul); free(u); return r;
}
int nvxntfs_delete(const char* path){ if(!g_vol||NVolReadOnly(g_vol))return -1; return unlink_name(path); }
int nvxntfs_rename(const char* oldpath,const char* newpath){
    if(!g_vol||NVolReadOnly(g_vol))return -1;
    ntfs_inode* ex=open_path(newpath); if(ex){ ntfs_inode_close(ex); errno=EEXIST; return -1; }
    ntfs_inode* ni=open_path(oldpath); if(!ni)return -1;
    char dir[256],leaf[256]; split(newpath,dir,leaf); ntfs_inode* dn=open_path(dir); if(!dn){ ntfs_inode_close(ni); return -1; }
    ntfschar* u=NULL; int ul=ntfs_mbstoucs(leaf,&u); if(ul<0){ ntfs_inode_close(ni); ntfs_inode_close(dn); return -1; }
    int r=ntfs_link(ni,dn,u,ul); free(u); ntfs_inode_close(dn); ntfs_inode_close(ni); if(r)return -1;
    return unlink_name(oldpath);
}
