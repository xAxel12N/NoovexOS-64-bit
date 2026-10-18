#include "include/nvx/fs/sysroot.h"
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/percpu.h"
#include "include/nvx/mm/heap.h"
#include "include/nvx/arch/x86_64/smp.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/lib/js.h"
#include "include/nvx/drivers/usb/xhci.h"
#include "include/nvx/net/http.h"
#include "include/nvx/arch/x86_64/interrupts.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/fs/vfs.h"
#include "include/nvx/fs/nvxfs.h"
#include "include/nvx/fs/dirtree.h"
#include "include/nvx/core/syscall.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/fs/nvx4.h"
#include "include/nvx/fs/unixfs.h"
#include "include/nvx/apps/files_classic.h"
#include "include/nvx/lib/crypto/nvxroots.h"
#define NVX3_MAGIC 0x3358564Eu

#define N3_FILES 24
#define N3_LINKS 64
#define S_IFLNK 0120000u
typedef struct { u32 used, dir, mode, uid, gid, nlink, len, mtime, ctime, atime, folder; char name[64]; } nvx3_ino;
typedef struct { u32 used, dir, ino; char name[52]; } nvx3_link;
typedef struct { u32 magic, version, ninode, nlinks, ndn; nvx3_ino ino[N3_FILES]; nvx3_link lk[N3_LINKS]; nvx_dnode d[NVX_DN_MAX]; } nvx3_t;
static nvx3_t g_n3;

struct ulink g_lk[NLINKS];
int g_n3_conv=0;
u32 ux_now(void){ return (u32)(k_realtime_ms()/1000ULL); }
void ux_touch(int k,int data){ if(k<0||k>=FS_N)return; u32 t=ux_now(); FS[k].ctime=t; if(data)FS[k].mtime=t; }

static u32 nvx3_slot(int k){ return NVX_DATA0+(u32)(k+1)*NVX_SECPF; }
int nvx3_try_load(void){
    u8* p=(u8*)&g_n3; int total=(int)sizeof(g_n3), secs=(total+511)/512;
    if(nvx_rd(NVX_DIR_LBA,nvx_sec))return -1; if(*(u32*)nvx_sec!=NVX3_MAGIC)return 1;
    for(int se=0;se<secs;se++){ if(nvx_rd(NVX_DIR_LBA+se,nvx_sec))return -1; for(int i=0;i<512;i++){ int off=se*512+i; if(off<total)p[off]=nvx_sec[i]; } }
    if(g_n3.ninode!=N3_FILES||g_n3.nlinks!=N3_LINKS)return -1;
    for(int k=0;k<FS_N;k++){ fs_free(k); FS[k].used=0; }
    static char tb[FS_OLDCAP];
    for(int k=0;k<N3_FILES;k++){ nvx3_ino* e=&g_n3.ino[k]; if(!e->used)continue;
        int len=(int)e->len; if(len>FS_OLDCAP-1)len=FS_OLDCAP-1;
        if(len&&nvx_rdn(nvx3_slot(k),(len+511)/512,(u8*)tb))return -1;
        fs_newslot(k); if(fs_set(k,tb,len))return -1;
        int q=0; while(e->name[q]&&q<63){ FS[k].name[q]=e->name[q]; q++; } FS[k].name[q]=0; FS[k].used=1;
        FS[k].dir=(int)e->dir; if(FS[k].dir<0||FS[k].dir>=NVX_DN_MAX)FS[k].dir=1; FS[k].mode=e->mode; FS[k].uid=(int)e->uid; FS[k].gid=(int)e->gid;
        FS[k].nlink=e->nlink?e->nlink:1; FS[k].mtime=e->mtime; FS[k].ctime=e->ctime; FS[k].atime=e->atime; FS[k].folder=(int)e->folder;
        n4_reserve_secs(nvx3_slot(k)-g_nvx_base,(u32)(len+511)/512); }
    for(int i=0;i<NLINKS;i++)g_lk[i].used=0;
    for(int i=0;i<N3_LINKS;i++){ nvx3_link* l=&g_n3.lk[i]; g_lk[i].used=(int)l->used&&l->ino<N3_FILES&&FS[l->ino].used; g_lk[i].dir=(int)l->dir; g_lk[i].ino=(int)l->ino; int q=0; while(l->name[q]&&q<51){ g_lk[i].name[q]=l->name[q]; q++; } g_lk[i].name[q]=0; }
    int c=0; for(int i=0;i<NVX_DN_MAX;i++){ g_nvx.d[i]=g_n3.d[i]; if(g_nvx.d[i].used)c=i+1; } g_nvx.dcount=(u32)c;
    return 0;
}

static int ux_split(const char* p,int* dir,char* base){ return k_split(p,dir,base); }
static int ux_lookup_nofollow(const char* p,int* isdir){
    dt_init(); *isdir=0; if(!p||!p[0])return -1;
    int d=dt_resolve_dir(p); if(d>=0){ *isdir=1; return d; }
    int dd; char base[64]; if(ux_split(p,&dd,base)==0&&base[0]){
        for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==dd&&seq(FS[k].name,base))return k;
        for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==dd&&seq(g_lk[i].name,base))return g_lk[i].ino; }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&!dt_dir_ok(FS[k].dir)&&seq(FS[k].name,(char*)p))return k;
    return -1;
}

int ux_is_admin(int uid){
    if(uid==0||uid==1000)return 1;
    char me[32]; if(!ux_uid_name(uid,me,32))return 0;
    int isd=0; int k=ux_lookup("/etc/group",&isd,1); if(k<0||isd)return 0;
    const char* s=fsd(k); const char* e=s+FS[k].len;
    while(s<e){ const char* l=s; while(s<e&&*s!='\n')s++; const char* le=s; if(s<e)s++;
        if(le-l<6||l[0]!='a'||l[1]!='d'||l[2]!='m'||l[3]!='i'||l[4]!='n'||l[5]!=':')continue;
        int col=0; const char* m=l; while(m<le&&col<3){ if(*m==':')col++; m++; }
        while(m<le){ const char* a=m; while(m<le&&*m!=',')m++; int L=(int)(m-a), i=0; while(i<L&&me[i]&&a[i]==me[i])i++; if(i==L&&!me[i])return 1; if(m<le)m++; } }
    return 0;
}
int ux_lookup(const char* p,int* isdir,int follow){
    char cur[192]; int n=0; while(p[n]&&n<191){ cur[n]=p[n]; n++; } if(p[n])return -1; cur[n]=0;
    for(int hop=0;hop<8;hop++){
        int k=ux_lookup_nofollow(cur,isdir); if(k<0||*isdir)return k;
        if(!follow||(FS[k].mode&0170000u)!=S_IFLNK)return k;
        char t[192]; if(FS[k].len>191)return -1; int tl=FS[k].len; const char* sd=fsd(k); for(int i=0;i<tl;i++)t[i]=sd[i]; t[tl]=0;
        if(t[0]=='/'){ for(int i=0;i<=tl;i++)cur[i]=t[i]; }
        else { char dp[192]; dt_path(FS[k].dir,dp); int o=0; for(int i=0;dp[i];i++){ if(o>=190)return -1; cur[o++]=dp[i]; } if(o>1)cur[o++]='/'; for(int i=0;t[i];i++){ if(o>=191)return -1; cur[o++]=t[i]; } cur[o]=0; }
    }
    return -1;
}
static int ux_link_count(int k){ int n=FS[k].used?1:0; for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].ino==k)n++; return n; }
static int ux_name_taken(int dir,const char* nm){
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==dir&&seq(FS[k].name,(char*)nm))return 1;
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==dir&&seq(g_lk[i].name,(char*)nm))return 1;
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==dir&&seq(DT[i].name,(char*)nm))return 1;
    return 0;
}
int ux_link(const char* from,const char* to){
    int isd; int k=ux_lookup(from,&isd,0); if(k<0||isd)return -1;
    if(g_uid!=0&&FS[k].uid!=g_uid&&!(ux_perm_ok(k,4)&&ux_perm_ok(k,2)))return -1;
    int d; char b[64]; if(ux_split(to,&d,b)<0||!b[0]||ux_name_taken(d,b))return -1;
    if(!perm_dir(d))return -1;
    { int L=0; while(b[L])L++; if(L>51)return -1; }
    for(int i=0;i<NLINKS;i++) if(!g_lk[i].used){ g_lk[i].used=1; g_lk[i].dir=d; g_lk[i].ino=k; int q=0; while(b[q]&&q<51){ g_lk[i].name[q]=b[q]; q++; } g_lk[i].name[q]=0;
        FS[k].nlink=ux_link_count(k); ux_touch(k,0); DT[d].mtime=DT[d].ctime=ux_now(); nvx3_flush_meta(); return 0; }
    return -1;
}
int ux_symlink(const char* target,const char* path){
    int d; char b[64]; if(ux_split(path,&d,b)<0||!b[0]||ux_name_taken(d,b))return -1;
    if(!perm_dir(d))return -1;
    for(int k=0;k<FS_N;k++) if(!FS[k].used){ fs_newslot(k); int q=0; while(b[q]&&q<63){ FS[k].name[q]=b[q]; q++; } FS[k].name[q]=0;
        int L=0; while(target[L]&&L<4095)L++; if(fs_set(k,target,L))return -1;
        FS[k].used=1; FS[k].dir=d; FS[k].folder=0; FS[k].mode=S_IFLNK|0777u; FS[k].uid=g_uid; FS[k].gid=g_uid; FS[k].nlink=1; ux_touch(k,1); FS[k].atime=FS[k].mtime; DT[d].mtime=DT[d].ctime=FS[k].mtime;
        nvx3_sync(k); return 0; }
    return -1;
}
int ux_readlink(const char* p,char* out,int cap){
    int isd; int k=ux_lookup(p,&isd,0); if(k<0&&!isd)return sysroot_readlink(p,out,cap); if(k<0||isd||(FS[k].mode&0170000u)!=S_IFLNK)return -1;
    int n=FS[k].len<cap?FS[k].len:cap; const char* sd=fsd(k); for(int i=0;i<n;i++)out[i]=sd[i]; return n;
}
int ux_unlink(const char* p){
    int d; char b[64]; if(ux_split(p,&d,b)<0||!b[0])return k_unlink(p);
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==d&&seq(g_lk[i].name,b)){ int k=g_lk[i].ino; if(!ux_can_unlink(d,k))return -1; g_lk[i].used=0; FS[k].nlink=ux_link_count(k); ux_touch(k,0); DT[d].mtime=DT[d].ctime=ux_now(); nvx3_flush_meta(); return 0; }
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==d&&seq(FS[k].name,b)){
        if(!ux_can_unlink(d,k))return -1;
        fs_drop_name(k); return 0; }
    return k_unlink(p);
}
int ux_chown(const char* p,int uid,int gid,int follow){
    int isd; int k=ux_lookup(p,&isd,follow); if(k<0)return -1;
    int cu=isd?DT[k].uid:FS[k].uid; if(g_uid!=0&&(uid!=-1&&uid!=cu))return -1; if(g_uid!=0&&g_uid!=cu)return -1;
    if(isd){ if(uid!=-1)DT[k].uid=uid; if(gid!=-1)DT[k].gid=gid; dt_save(); return 0; }
    if(uid!=-1)FS[k].uid=uid; if(gid!=-1)FS[k].gid=gid; ux_touch(k,0); nvx3_flush_meta(); return 0;
}
int ux_chmod(const char* p,unsigned mode){
    int isd; int k=ux_lookup(p,&isd,1); if(k<0)return -1;
    int cu=isd?DT[k].uid:FS[k].uid; if(g_uid!=0&&g_uid!=cu)return -1;
    if(isd){ DT[k].mode=(DT[k].mode&0170000u)|(mode&07777u); if(!(DT[k].mode&0170000u))DT[k].mode|=0040000u; dt_save(); return 0; }
    FS[k].mode=(FS[k].mode&0170000u)|(mode&07777u); if(!(FS[k].mode&0170000u))FS[k].mode|=0100000u; ux_touch(k,0); nvx3_flush_meta(); return 0;
}
int ux_utime(const char* p,u32 at,u32 mt){ int isd; int k=ux_lookup(p,&isd,1); if(k<0)return -1; if(isd)return 0; u32 t=ux_now(); FS[k].atime=at?at:t; FS[k].mtime=mt?mt:t; FS[k].ctime=t; nvx3_flush_meta(); return 0; }

static int g_usb_mounted=0;
static int ux_usb_idx(const char* p){
    if(!g_usb_mounted||!msd_ok||!ufat_ok)return -1; const char* m="/mnt/usb/"; int i=0; while(m[i]&&p[i]==m[i])i++; if(m[i])return -1;
    const char* n=p+i; for(int f=0;f<ufs_n;f++){ int eq=1; for(int k=0;k<16;k++){ char x=ufs[f].name[k],y=n[k]; if(x>='a'&&x<='z')x-=32; if(y>='a'&&y<='z')y-=32; if(x!=y){eq=0;break;} if(!x&&!y)break; } if(eq)return f; }
    return -1;
}
static int usbfs_match(const char* p){ return ux_usb_idx(p)>=0; }
static char* g_usbbuf=0; static int g_usbbuf_fi=-1, g_usbbuf_n=0;
static int usbfs_load(const char* p){ int fi=ux_usb_idx(p); if(fi<0)return -1; if(fi==g_usbbuf_fi&&g_usbbuf)return g_usbbuf_n;
    if(!g_usbbuf)g_usbbuf=(char*)kmalloc(1048576); if(!g_usbbuf)return -1; g_usbbuf_n=ufat_read(fi,g_usbbuf,1048575); if(g_usbbuf_n<0)g_usbbuf_n=0; g_usbbuf_fi=fi; return g_usbbuf_n; }
static int usbfs_read(const char* p,char* buf,int off,int len){ int n=usbfs_load(p); if(n<0)return -1; if(off<0||off>=n)return 0; int c=n-off; if(c>len)c=len; for(int i=0;i<c;i++)buf[i]=g_usbbuf[off+i]; return c; }
static int usbfs_size(const char* p){ return usbfs_load(p); }
static const vfs_fs_ops USBFS={ "usbfs /mnt/usb", usbfs_match, usbfs_read, usbfs_size };
int ux_mount(const char* src,const char* target){
    (void)src; if(g_uid!=0)return -1; if(!seq((char*)target,"/mnt/usb"))return -1;
    if(!msd_ok||!ufat_ok){ if(!msd_autoinit())return -1; ufat_mount(); if(!ufat_ok)return -1; }
    dt_init(); int mn=dt_mkdir_at(0,"mnt"); dt_mkdir_at(mn,"usb");
    if(!g_usb_mounted){ vfs_register(&USBFS); g_usb_mounted=1; } g_usbbuf_fi=-1; return 0;
}
int ux_umount(const char* target){ if(g_uid!=0||!seq((char*)target,"/mnt/usb")||!g_usb_mounted)return -1; g_usb_mounted=0; g_usbbuf_fi=-1; return 0; }

int g_fb0_open=0, g_fb0_dirty=0;
static u8 g_mq[256][3]; int g_mq_h=0,g_mq_t=0, g_mouse_open=0;
void ux_mouse_push(int dx,int dy,int btn){ if(!g_mouse_open)return; while(dx||dy||1){ int sx=dx>127?127:dx<-127?-127:dx, sy=dy>127?127:dy<-127?-127:dy;
        int nt=(g_mq_t+1)&255; if(nt==g_mq_h)return; g_mq[g_mq_t][0]=(u8)(0x08|(btn&7)|(sx<0?0x10:0)|(sy<0?0x20:0)); g_mq[g_mq_t][1]=(u8)sx; g_mq[g_mq_t][2]=(u8)sy; g_mq_t=nt; dx-=sx; dy-=sy; if(!dx&&!dy)break; } }
int ux_dev_read(struct vfile* v,char* buf,int len){
    if(v->dev==16){ extern int xin_read(char*,int); return xin_read(buf,len); }
    if(v->dev==9){ u64 tot=(u64)SW*H*4; if((u64)v->off>=tot)return 0; int c=len; if((u64)v->off+(u64)c>tot)c=(int)(tot-(u64)v->off); __builtin_memcpy(buf,(u8*)SCENE+v->off,(unsigned long)c); v->off+=c; return c; }
    if(v->dev==10){ int c=0; while(c+3<=len&&g_mq_h!=g_mq_t){ buf[c]=(char)g_mq[g_mq_h][0]; buf[c+1]=(char)g_mq[g_mq_h][1]; buf[c+2]=(char)g_mq[g_mq_h][2]; g_mq_h=(g_mq_h+1)&255; c+=3; } if(!c&&g_js_running)return JS_BLOCK; return c; }
    return 0;
}
int ux_dev_write(struct vfile* v,const char* buf,int len){
    if(v->dev==9){ u64 tot=(u64)SW*H*4; if((u64)v->off>=tot)return 0; int c=len; if((u64)v->off+(u64)c>tot)c=(int)(tot-(u64)v->off); __builtin_memcpy((u8*)SCENE+v->off,buf,(unsigned long)c); v->off+=c; g_fb0_dirty=1; return c; }
    return -1;
}

char g_hostname[32]="noovex";
int ux_perm_ok(int k,int want){
    if(k<0||k>=FS_N||!FS[k].used)return 0;
    if(g_uid==0)return 1;
    unsigned m=FS[k].mode;
    if(FS[k].uid==g_uid) return (m & (want<<6)) ? 1 : 0;
    return (m & want) ? 1 : 0;
}
int perm_file(int k,int want){ return ux_perm_ok(k,want); }
int perm_dir(int d){
    if(d<0||d>=DN_MAX||!DT[d].used)return 0;
    if(g_uid==0||DT[d].uid==g_uid)return 1;
    if((DT[d].mode?DT[d].mode:0040755u)&0002)return 1;
    return ux_is_admin(g_uid);
}
int ux_links_in(int d){ int n=0; for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==d)n++; return n; }
int fs_is_open(int k){ for(int i=0;i<VMAXOPEN;i++) if(g_vf[i].used&&g_vf[i].dev==0&&g_vf[i].fs_idx==k)return 1; return 0; }
void fs_newslot(int k){
    if(k<0||k>=FS_N)return; fs_free(k); u32 t=ux_now();
    FS[k].name[0]=0; FS[k].len=0; FS[k].used=0; FS[k].folder=0; FS[k].dir=1; FS[k].mode=0100644u; FS[k].uid=g_uid; FS[k].gid=g_gid; FS[k].nlink=1;
    FS[k].mtime=FS[k].ctime=FS[k].atime=t; g_fs_dirty[k]=0;
}
int pd_touch(int k){ int d=FS[k].dir; if(d>=0&&d<DN_MAX&&DT[d].used){ DT[d].mtime=DT[d].ctime=ux_now(); } return 0; }
int ux_can_unlink(int d,int k){
    if(d<0||d>=DN_MAX||!DT[d].used)return perm_file(k,2);
    if(g_uid==0)return 1;
    unsigned dm=DT[d].mode?DT[d].mode:0040755u;
    int real=DT[d].uid==g_uid||(dm&0002u);
    if(!real&&!(FS[k].uid==g_uid&&ux_is_admin(g_uid)))return 0;
    if((dm&01000u)&&FS[k].uid!=g_uid&&DT[d].uid!=g_uid)return 0;
    return 1;
}
void fs_slot_freed(int k){
    if(k<0||k>=FS_N)return;
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].ino==k)g_lk[i].used=0;
    g_fs_dirty[k]=0;
    if(fs_is_open(k)&&fs_ensure(k)==0){ FS[k].used=1; FS[k].orphan=1; FS[k].clean=0; FS[k].name[0]=0; FS[k].dir=-2; FS[k].folder=-1; FS[k].nlink=0; return; }
    for(int i=0;i<VMAXOPEN;i++) if(g_vf[i].used&&g_vf[i].dev==0&&g_vf[i].fs_idx==k){ g_vf[i].dev=1; g_vf[i].fs_idx=-1; }
    FS[k].used=0; fs_free(k);
}
void fs_orphan_close(int k){ if(k<0||k>=FS_N||!FS[k].used||!FS[k].orphan||fs_is_open(k))return; FS[k].used=0; fs_free(k); }
void fs_drop_name(int k){
    if(k<0||k>=FS_N||!FS[k].used)return;
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].ino==k){ int q=0; while(g_lk[i].name[q]&&q<51){ FS[k].name[q]=g_lk[i].name[q]; q++; } FS[k].name[q]=0; FS[k].dir=g_lk[i].dir; g_lk[i].used=0; FS[k].nlink=ux_link_count(k); ux_touch(k,0); nvx3_flush_meta(); return; }
    pd_touch(k); FS[k].used=0; fs_slot_freed(k); nvx3_flush_meta();
}
int ux_rename_link(const char* from,const char* to){
    int d; char b[64]; if(ux_split(from,&d,b)<0||!b[0])return -2;
    int li=-1; for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==d&&seq(g_lk[i].name,b)){ li=i; break; }
    if(li<0)return -2;
    if(!ux_perm_ok(g_lk[li].ino,2))return -1;
    int nd; char nb[64]; int td=dt_resolve_dir(to);
    if(td>=0){ nd=td; int q=0; while(b[q]&&q<63){ nb[q]=b[q]; q++; } nb[q]=0; } else if(ux_split(to,&nd,nb)<0||!nb[0])return -1;
    int L=0; while(nb[L])L++; if(L>51)return -1;
    if(nd==d&&seq(b,nb))return 0;
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==nd&&seq(DT[i].name,nb))return -1;
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==nd&&seq(FS[k].name,nb)){ if(k==g_lk[li].ino)return -1; if(!ux_perm_ok(k,2))return -1; fs_drop_name(k); }
    for(int i=0;i<NLINKS;i++) if(i!=li&&g_lk[i].used&&g_lk[i].dir==nd&&seq(g_lk[i].name,nb)){ if(!ux_perm_ok(g_lk[i].ino,2))return -1; int k=g_lk[i].ino; g_lk[i].used=0; FS[k].nlink=ux_link_count(k); }
    g_lk[li].dir=nd; int q=0; while(nb[q]&&q<51){ g_lk[li].name[q]=nb[q]; q++; } g_lk[li].name[q]=0;
    nvx3_flush_meta(); return 0;
}
static char* pc_(char* o,const char* x){ while(*x)*o++=*x++; *o=0; return o; }
static char* pn_(char* o,long v){ char t[24]; int n=0; if(!v)t[n++]='0'; while(v){t[n++]='0'+v%10;v/=10;} while(n)*o++=t[--n]; *o=0; return o; }
int proc_gen(const char* p,char* out,int cap){ (void)cap;
    if(seq((char*)p,"/proc/cpuinfo")){ char* o=out; o=pc_(o,"processor\t: 0\nvendor_id\t: NoovexOS\nmodel name\t: x86-64 CPU\nflags\t\t: fpu tsc msr pae cpuid\n"); return (int)(o-out); }
    if(seq((char*)p,"/proc/version")){ char* o=out; o=pc_(o,"NoovexOS 64 (x86-64)\n"); return (int)(o-out); }
    if(seq((char*)p,"/proc/hostname")){ char* o=out; o=pc_(o,g_hostname); o=pc_(o,"\n"); return (int)(o-out); }
    if(seq((char*)p,"/proc/uptime")){ extern volatile u64 g_ticks; char* o=out; o=pn_(o,(long)(g_ticks/100)); o=pc_(o,".00 0.00\n"); return (int)(o-out); }
    if(seq((char*)p,"/proc/meminfo")){ extern u32 g_ram_mb; char* o=out;
        o=pc_(o,"MemTotal:       "); o=pn_(o,(long)g_ram_mb*1024); o=pc_(o," kB\n");
        o=pc_(o,"MemFree:        "); o=pn_(o,(long)g_ram_mb*1024*3/4); o=pc_(o," kB\n");
        o=pc_(o,"MemAvailable:   "); o=pn_(o,(long)g_ram_mb*1024*3/4); o=pc_(o," kB\n"); return (int)(o-out); }
    if(seq((char*)p,"/proc/filesystems")){ char* o=out; o=pc_(o,"nodev\tproc\nnodev\tsysfs\n\tnvxfs\n\tramfs\n\tvfat\n"); return (int)(o-out); }
    if(seq((char*)p,"/proc/loadavg")){ char* o=out; o=pc_(o,"0.00 0.00 0.00 1/"); { int n=0; for(int i=0;i<FS_N;i++)if(FS[i].used)n++; o=pn_(o,n); } o=pc_(o," 1\n"); return (int)(o-out); }
    if(seq((char*)p,"/proc/mounts")){ char* o=out;
        o=pc_(o,"nvxfs / nvxfs rw 0 0\n");
        o=pc_(o,"proc /proc proc rw 0 0\n");
        o=pc_(o,"sysfs /sys sysfs rw 0 0\n");
        if(g_usb_mounted) o=pc_(o,"/dev/usb /mnt/usb vfat rw 0 0\n");
        return (int)(o-out); }
    if(seq((char*)p,"/proc/self/status")||seq((char*)p,"/proc/self")){ char* o=out;
        o=pc_(o,"Name:\tnsh\nState:\tR (running)\nUid:\t"); o=pn_(o,g_uid); o=pc_(o,"\nGid:\t"); o=pn_(o,g_uid); o=pc_(o,"\n"); return (int)(o-out); }
    /* ---- DevFS: synthetic device files ---- */
    if(seq((char*)p,"/dev/null")){ return 0; }                                   /* empty */
    if(seq((char*)p,"/dev/zero")){ int n=cap-1>4096?4096:cap-1; for(int i=0;i<n;i++)out[i]=0; return n; }
    if(seq((char*)p,"/dev/random")||seq((char*)p,"/dev/urandom")){ extern volatile u64 g_ticks; u64 x=g_ticks*2654435761ULL+0x9E3779B97F4A7C15ULL; int n=cap-1>64?64:cap-1; for(int i=0;i<n;i++){ x^=x<<13; x^=x>>7; x^=x<<17; out[i]="0123456789abcdef"[x&15]; } out[n]=0; return n; }
    if(seq((char*)p,"/dev/console")||seq((char*)p,"/dev/tty")){ char* o=out; o=pc_(o,"console\n"); return (int)(o-out); }
    /* ---- SysFS ---- */
    if(seq((char*)p,"/sys/kernel/ostype")){ char* o=out; o=pc_(o,"NoovexOS\n"); return (int)(o-out); }
    if(seq((char*)p,"/sys/kernel/osrelease")){ char* o=out; o=pc_(o,"9.82\n"); return (int)(o-out); }
    if(seq((char*)p,"/sys/kernel/hostname")){ char* o=out; o=pc_(o,g_hostname); o=pc_(o,"\n"); return (int)(o-out); }
    if(seq((char*)p,"/sys/kernel/arch")){ char* o=out; o=pc_(o,"x86_64\n"); return (int)(o-out); }
    if(seq((char*)p,"/sys/devices/system/cpu/online")){ extern volatile int g_cores_online; char* o=out; int n=g_smp_on?g_cores_online:1; if(n<=1)o=pc_(o,"0\n"); else { o=pc_(o,"0-"); o=pn_(o,n-1); o=pc_(o,"\n"); } return (int)(o-out); }
    if(seq((char*)p,"/sys/class/net/eth0/operstate")){ char* o=out; o=pc_(o,"down\n"); return (int)(o-out); }
    if(seq((char*)p,"/sys/power/state")){ char* o=out; o=pc_(o,"mem disk\n"); return (int)(o-out); }
    return -1;
}
int ux_statfs(const char* p,unsigned long* o){
    (void)p; extern u32 g_ram_mb;
    int files=0,dirs=0; for(int i=0;i<FS_N;i++)if(FS[i].used)files++; for(int i=0;i<DN_MAX;i++)if(DT[i].used)dirs++;
    o[0]=0x4e5658;              /* f_type = "NVX" magic */
    o[1]=4096;                  /* f_bsize */
    o[2]=(unsigned long)g_ram_mb*256;   /* f_blocks (4K blocks in RAM) */
    o[3]=o[2]-(unsigned long)files;     /* f_bfree */
    o[4]=o[3];                  /* f_bavail */
    o[5]=FS_N;                  /* f_files (inode capacity) */
    o[6]=FS_N-files;            /* f_ffree */
    o[7]=255;                   /* f_namelen */
    return 0;
}
int ux_read_file(const char* p,char* out,int cap){ { int pg=proc_gen(p,out,cap); if(pg>=0)return pg; } int isd; int k=ux_lookup(p,&isd,1); if(k<0||isd)return -1; if(!ux_perm_ok(k,4))return -1; int n=FS[k].len<cap-1?FS[k].len:cap-1; const char* sd=fsd(k); for(int i=0;i<n;i++)out[i]=sd[i]; out[n]=0; return n; }
static int ux_mkfile(int dir,const char* nm,const char* body,unsigned mode){
    for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==dir&&seq(FS[k].name,(char*)nm))return k;
    for(int k=0;k<FS_N;k++) if(!FS[k].used){ fs_newslot(k); int q=0; while(nm[q]&&q<63){ FS[k].name[q]=nm[q]; q++; } FS[k].name[q]=0; int L=0; while(body[L])L++; if(fs_set(k,body,L))return -1;
        FS[k].used=1; FS[k].dir=dir; FS[k].folder=0; FS[k].mode=mode; FS[k].uid=0; FS[k].gid=0; FS[k].nlink=1; ux_touch(k,1); FS[k].atime=FS[k].mtime; if(disk_ok)nvx3_sync(k); return k; }
    return -1;
}
static int ux_pw_field(const char* line,int idx,char* out,int cap){ int f=0,o=0; for(int i=0;line[i]&&line[i]!='\n';i++){ if(line[i]==':'){ f++; continue; } if(f==idx&&o<cap-1)out[o++]=line[i]; } out[o]=0; return f>=idx; }
int ux_uid_name(int uid,char* out,int cap){ char buf[1024]; int n=ux_read_file("/etc/passwd",buf,sizeof(buf)); if(n<=0)return 0;
    for(char* l=buf;*l;){ char u[16]; ux_pw_field(l,2,u,16); int v=0; for(int i=0;u[i];i++)v=v*10+(u[i]-'0'); if(u[0]&&v==uid){ ux_pw_field(l,0,out,cap); return 1; } while(*l&&*l!='\n')l++; if(*l)l++; }
    return 0; }
static int ux_rewrite_names(const char* path,const char* from,const char* to,int field0_only){
    int isd=0; int k=ux_lookup(path,&isd,1); if(k<0||isd)return 0;
    static char out[4096]; int o=0, ch=0; const char* p=fsd(k); const char* e=p+FS[k].len; int fl=0; while(from[fl])fl++;
    while(p<e&&o<4000){
        const char* l=p; while(p<e&&*p!='\n')p++; const char* le=p; if(p<e)p++;
        if(field0_only){
            int m=(le-l>fl&&l[fl]==':'); for(int i=0;m&&i<fl;i++) if(l[i]!=from[i])m=0;
            if(m){ for(const char* t=to;*t&&o<4000;t++)out[o++]=*t; for(const char* x=l+fl;x<le&&o<4000;x++)out[o++]=*x; ch=1; }
            else for(const char* x=l;x<le&&o<4000;x++)out[o++]=*x;
        } else {
            int col=0; const char* x=l; while(x<le&&col<3){ out[o++]=*x; if(*x==':')col++; x++; }
            while(x<le&&o<4000){ const char* a=x; while(x<le&&*x!=',')x++; int L=(int)(x-a), eq=(L==fl); for(int i=0;eq&&i<L;i++) if(a[i]!=from[i])eq=0;
                if(eq){ for(const char* t=to;*t&&o<4000;t++)out[o++]=*t; ch=1; } else for(const char* y=a;y<x&&o<4000;y++)out[o++]=*y;
                if(x<le){ out[o++]=','; x++; } }
        }
        out[o++]='\n'; }
    if(!ch)return 0;
    if(fs_set(k,out,o))return 0; ux_touch(k,1); if(disk_ok)nvx3_sync(k); return 1;
}
static void ux_ensure_account(const char* name){
    if(!name||!name[0]||seq((char*)name,"root"))return;
    char cur[32]; if(!ux_uid_name(1000,cur,32))return;
    if(seq(cur,(char*)name))return;
    char tmp[1024]; int n=ux_read_file("/etc/passwd",tmp,sizeof(tmp)); if(n>0){ tmp[n]=0; for(char* l=tmp;*l;){ char nm[32]; ux_pw_field(l,0,nm,32); if(seq(nm,(char*)name))return; while(*l&&*l!='\n')l++; if(*l)l++; } }
    ux_rewrite_names("/etc/passwd",cur,name,1);
    ux_rewrite_names("/etc/group",cur,name,0);
    klog2("accounts: first account is now named ",name);
}
static int ux_login_user(const char* name){
    char buf[1024]; int n=ux_read_file("/etc/passwd",buf,sizeof(buf)); if(n<=0)return -1;
    for(char* l=buf;*l;){ char nm[32]; ux_pw_field(l,0,nm,32); if(seq(nm,(char*)name)){ char u[16],g[16],home[64]; ux_pw_field(l,2,u,16); ux_pw_field(l,3,g,16); ux_pw_field(l,5,home,64);
            int uv=0; for(int i=0;u[i];i++)uv=uv*10+(u[i]-'0'); int gv=0; for(int i=0;g[i];i++)gv=gv*10+(g[i]-'0');
            g_uid=uv; g_gid=gv; int d=dt_resolve_dir(home); if(d>=0)g_dcwd=d; env_set("HOME",home); env_set("USER",nm); return uv; }
        while(*l&&*l!='\n')l++; if(*l)l++; }
    return -1;
}
  
void ux_save_localpw(void){ char body[128]; char* o=body; const char* u=g_user; while(*u)*o++=*u++; *o++=':';
    if(g_has_pw){ unsigned h=0; { const char* p=g_user_pw; h=2166136261u; while(*p){h^=(unsigned char)*p++;h*=16777619u;} } char t[12]; int n=0; if(!h)t[n++]='0'; while(h){t[n++]="0123456789abcdef"[h&15];h>>=4;} while(n)*o++=t[--n]; } else *o++='*';
    *o++='\n'; *o=0;
    int etc=dt_resolve_dir("/etc"); if(etc<0)etc=dt_mkdir_at(0,"etc"); ux_mkfile(etc,"shadow",body,0100600u); }
void ux_load_localpw(void){ char b[128]; int n=ux_read_file("/etc/shadow",b,sizeof(b)); if(n<=0)return; b[n]=0;
    char* c=b; while(*c&&*c!=':')c++; if(*c==':'){ c++; if(*c&&*c!='*'&&*c!='\n')g_has_pw=1; } }
static char* ux_b64(char* o,const u8* d,int n){
    static const char T[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int col=0;
    for(int i=0;i<n;i+=3){ u32 v=(u32)d[i]<<16; if(i+1<n)v|=(u32)d[i+1]<<8; if(i+2<n)v|=d[i+2];
        *o++=T[(v>>18)&63]; *o++=T[(v>>12)&63]; *o++=(i+1<n)?T[(v>>6)&63]:'='; *o++=(i+2<n)?T[v&63]:'=';
        col+=4; if(col>=64){ *o++='\n'; col=0; } }
    if(col)*o++='\n';
    return o;
}
static void ux_write_anchors(int etc){
    int ssl=-1; { int d=dt_resolve_dir("/etc/ssl"); ssl=d>=0?d:dt_mkdir_at(etc,"ssl"); } if(ssl<0)return;
    if(!DT[ssl].mode)DT[ssl].mode=0040755u;
    int cap=200000; char* buf=(char*)kmalloc((unsigned long)cap); if(!buf)return; char* o=buf;
    o=pc_(o,"# NoovexOS trust anchors, written at boot from the kernel's built-in root store.\n# Each root is two PEM objects: the subject name (DER) and the public key (SubjectPublicKeyInfo).\n# The kernel's https command uses the same roots. Put extra CA certificates in /etc/ssl/local-ca.pem.\n");
    x509_anchor a; int n=0;
    for(int i=0;nvx_root_get(i,&a)&&o-buf<cap-4096;i++){
        o=pc_(o,"\n# "); o=pc_(o,nvx_root_names[i]); o=pc_(o,"\n-----BEGIN TRUST ANCHOR NAME-----\n"); o=ux_b64(o,a.subj,a.subjlen); o=pc_(o,"-----END TRUST ANCHOR NAME-----\n-----BEGIN PUBLIC KEY-----\n"); o=ux_b64(o,a.spki,a.spkilen); o=pc_(o,"-----END PUBLIC KEY-----\n"); n++; }
    int L=(int)(o-buf);
    int k=-1; for(int q=0;q<FS_N;q++) if(FS[q].used&&FS[q].dir==ssl&&seq(FS[q].name,"nvx-anchors.pem")){ k=q; break; }
    if(k>=0&&FS[k].len==L){ const char* sd=fsd(k); int same=1; for(int i=0;i<L;i++) if(sd[i]!=buf[i]){ same=0; break; } if(same){ kfree(buf); return; } }
    if(k<0){ *o=0; ux_mkfile(ssl,"nvx-anchors.pem",buf,0100644u); }
    else { if(!fs_set(k,buf,L)){ ux_touch(k,1); if(disk_ok)nvx3_sync(k); } }
    kfree(buf); (void)n;
}
static void ux_boot_layout(void){
    dt_init();
    int etc=dt_mkdir_at(0,"etc"), usr=dt_mkdir_at(0,"usr"), var=dt_mkdir_at(0,"var"), tmp=dt_mkdir_at(0,"tmp"), mnt=dt_mkdir_at(0,"mnt");
    dt_mkdir_at(usr,"bin"); dt_mkdir_at(var,"log"); dt_mkdir_at(0,"sbin"); dt_mkdir_at(0,"root"); dt_mkdir_at(0,"home"); dt_mkdir_at(0,"bin"); dt_mkdir_at(0,"dev"); { int dvs=dt_child(0,"dev"); if(dvs>=0&&dt_child(dvs,"shm")<0)dt_mkdir_at(dvs,"shm"); } dt_mkdir_at(mnt,"usb"); { int sysd=dt_mkdir_at(0,"sys"); dt_mkdir_at(0,"proc"); dt_mkdir_at(sysd,"kernel"); dt_mkdir_at(sysd,"devices"); dt_mkdir_at(sysd,"class"); dt_mkdir_at(sysd,"power"); }
    for(int i=0;i<DN_MAX;i++) if(DT[i].used&&!DT[i].mode)DT[i].mode=0040755u;
    if(tmp>=0){ DT[tmp].mode=0041777u; for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==tmp)FS[k].used=0; for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==tmp)g_lk[i].used=0; }
    if(etc>=0){
        ux_mkfile(etc,"passwd","root:x:0:0:root:/root:/bin/nsh\nuser:x:1000:1000:user:/home:/bin/nsh\n",0100644u);
        ux_mkfile(etc,"group","root:x:0:\nadmin:x:10:user\nusers:x:1000:user\n",0100644u);
        ux_mkfile(etc,"hostname","noovex\n",0100644u);
        ux_mkfile(etc,"rc","# /etc/rc - run by init (PID 1) at boot\necho NoovexOS: init started\n",0100644u);
        ux_write_anchors(etc);
        char hb[40]; int n=ux_read_file("/etc/hostname",hb,sizeof(hb)); if(n>0){ int o=0; for(int i=0;hb[i]&&hb[i]!='\n'&&o<31;i++)g_hostname[o++]=hb[i]; g_hostname[o]=0; }
    }
    { extern void apt_boot(void); apt_boot(); }
    sysroot_mkdirs();
    dt_save();
}
int ux_stat2(const char* p,u32* o,int follow){
    for(int i=0;i<10;i++)o[i]=0; dt_init();
    int dv=dev_id(p); if(dv){ o[0]=0020666u; o[3]=1; o[5]=20000u+(u32)dv; return 0; }
    int fi=ux_usb_idx(p); if(fi>=0){ o[0]=0100644u; o[3]=1; o[4]=ufs[fi].size; o[5]=21000u+(u32)fi; return 0; }
    int isd; int k=ux_lookup(p,&isd,follow);
    if(k<0){ int sx=0; u32 SL=0; u32 h=2166136261u; for(int q=0;p[q];q++)h=(h^(u8)p[q])*16777619u; o[6]=o[7]=o[8]=1760000000u;
        if(!follow&&sysroot_islink(p)){ char lb[256]; int ln=sysroot_readlink(p,lb,255); o[0]=0120777u; o[3]=1; o[4]=(u32)(ln>0?ln:0); o[5]=0x10000000u|(h&0x0FFFFFFFu); return 0; }
        if(sysroot_isdir(p)){ o[0]=0040755u; o[3]=2; o[4]=4096; o[5]=0x10000000u|(h&0x0FFFFFFFu); o[9]=1; return 0; }
        if(sysroot_get(p,&SL,&sx)){ o[0]=sx?0100755u:0100644u; o[3]=1; o[4]=SL; o[5]=0x10000000u|(h&0x0FFFFFFFu); return 0; }
        o[6]=o[7]=o[8]=0; }
    if(k<0){ u32 L; const char* b=p; if(b[0]=='/')b++; if(jc_find_prog(p,&L)&&(p[0]!='/'||(b[0]=='b'&&b[1]=='i'&&b[2]=='n')||(b[0]=='s'&&b[1]=='b')||(b[0]=='u'&&b[1]=='s'&&b[2]=='r'))){ o[0]=0100755u; o[3]=1; o[4]=L; o[5]=22000u; return 0; } return -1; }
    if(isd){ u32 nl=2; for(int i=0;i<DN_MAX;i++) if(DT[i].used&&i!=k&&DT[i].parent==k)nl++; o[0]=DT[k].mode?DT[k].mode:0040755u; o[1]=(u32)DT[k].uid; o[2]=(u32)DT[k].gid; o[3]=nl; o[4]=4096; o[5]=10000u+(u32)k; o[6]=DT[k].mtime; o[7]=DT[k].atime; o[8]=DT[k].ctime; o[9]=1; return 0; }
    o[0]=FS[k].mode?FS[k].mode:0100644u; o[1]=(u32)FS[k].uid; o[2]=(u32)FS[k].gid; o[3]=FS[k].nlink?FS[k].nlink:1; o[4]=(u32)FS[k].len; o[5]=(u32)k+1; o[6]=FS[k].mtime; o[7]=FS[k].atime; o[8]=FS[k].ctime;
    return 0;
}
int k_readdir_extra(int d,int n,char* name,int* type){
    int c=0;
    for(int i=0;i<NLINKS;i++) if(g_lk[i].used&&g_lk[i].dir==d){ if(c==n){ int j=0; while(g_lk[i].name[j]&&j<51){ name[j]=g_lk[i].name[j]; j++; } name[j]=0; if(type)*type=8; return 1; } c++; }
    if(d==dt_resolve_dir("/bin")) for(int i=0;JS_BIN[i].name;i++){ if(JS_BIN[i].name[0]=='+')continue; if(c==n){ int j=0; while(JS_BIN[i].name[j]&&j<63){ name[j]=JS_BIN[i].name[j]; j++; } name[j]=0; if(type)*type=8; return 1; } c++; }
    if(d==dt_resolve_dir("/sbin")){ if(c==n){ name[0]='i';name[1]='n';name[2]='i';name[3]='t';name[4]=0; if(type)*type=8; return 1; } c++; }
    if(g_usb_mounted&&d==dt_resolve_dir("/mnt/usb")) for(int f=0;f<ufs_n;f++){ if(c==n){ int j=0; while(ufs[f].name[j]&&j<15){ name[j]=ufs[f].name[j]; j++; } name[j]=0; if(type)*type=8; return 1; } c++; }
    if(d==dt_resolve_dir("/dev")){ static const char* DV[]={"null","zero","random","urandom","full","tty","console","fb0","dsp","kmsg",0}; for(int i=0;DV[i];i++){ if(c==n){ int j=0; while(DV[i][j]){ name[j]=DV[i][j]; j++; } name[j]=0; if(type)*type=2; return 1; } c++; } }
    return 0;
}
static void jc_start_init(void){
    if(jc_idx(1)>=0)return;
    int i=jc_create("init",0,0); if(i<0)return;
    g_jp[i].pid=1; g_jp[i].tgid=1; g_jp[i].pgid=1; g_jp[i].ppid=0; g_jp[i].uid=0; g_jp[i].gid=0; if(g_next_pid<2)g_next_pid=2;
}
static int g_ux_core=0;
void ux_core_start(const char* user){ if(g_ux_core)return; g_ux_core=1;
    klog("unix: layout"); ux_boot_layout(); klog("unix: load pw"); ux_load_localpw();
    if(user&&user[0]){ klog2("unix: login ",user); ux_ensure_account(user); ux_login_user(user); } klog("unix: ready"); }
void ux_desktop_start(const char* user){
    static int done=0; if(done)return; done=1;
    if(g_ux_core){ if(user&&user[0]){ ux_ensure_account(user); ux_login_user(user); } } else ux_core_start(user);
    klog("unix: starting init (desktop)"); jc_start_init();
}
