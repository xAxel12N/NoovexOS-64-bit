#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/mem/pmm_vmm.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/core/proc/sched.h"
#include "include/nvx/arch/x86_64/smp/smp.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/lib/js/js.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/shell/terminal.h"
#include "include/nvx/fs/nvx/nvxfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/arch/x86_64/time/hrtimer.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/shell/vt.h"
#include "include/nvx/arch/x86_64/smp/smp_sched.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/sys/linux/linux.h"
#include "include/nvx/apps/system/files_classic.h"
#define LX_EPERM 1
#define LX_ENOENT 2
#define LX_ESRCH 3
#define LX_EINTR 4
#define LX_EIO 5
#define LX_E2BIG 7
#define LX_ENOEXEC 8
#define LX_EBADF 9
#define LX_ECHILD 10
#define LX_EAGAIN 11
#define LX_ENOMEM 12
#define LX_EACCES 13
#define LX_EFAULT 14
#define LX_EBUSY 16
#define LX_EEXIST 17
#define LX_ENODEV 19
#define LX_ENOTDIR 20
#define LX_EISDIR 21
#define LX_EINVAL 22
#define LX_EMFILE 24
#define LX_ENOTTY 25
#define LX_ENOSPC 28
#define LX_ESPIPE 29
#define LX_EPIPE 32
#define LX_ERANGE 34
#define LX_ENAMETOOLONG 36
#define LX_ENOSYS 38
#define LX_ENOTEMPTY 39
#define LX_ENOTSUP 95
#define LX_AT_FDCWD (-100)
#define LX_DIRDEV 11
#define LX_SRDIR 112
#define LX_PATH 256
#define LX_IOMAX (1<<20)

static int lx_ok(u64 p,u64 n){ return jc_umapped(p,n); }
static int lx_okw(u64 p,u64 n){ extern int jc_umapped_w(u64,u64); return jc_umapped_w(p,n); }
static int lx_str(u64 p,char* out,int cap){
    if(!p)return -LX_EFAULT;
    for(int i=0;i<cap;i++){ u64 a=p+(u64)i; if(i==0||(a&0xFFFULL)==0){ if(!lx_ok(a,1))return -LX_EFAULT; }
        out[i]=*(const char*)a; if(!out[i])return i; }
    out[cap-1]=0; return -LX_ENAMETOOLONG;
}
static int lx_put(u64 p,const void* src,u64 n){ if(!lx_okw(p,n))return -LX_EFAULT; const u8* s=(const u8*)src; u8* d=(u8*)p; for(u64 i=0;i<n;i++)d[i]=s[i]; return 0; }
static int lx_get(void* dst,u64 p,u64 n){ if(!lx_ok(p,n))return -LX_EFAULT; u8* d=(u8*)dst; const u8* s=(const u8*)p; for(u64 i=0;i<n;i++)d[i]=s[i]; return 0; }
static void lx_zero(void* p,u64 n){ u8* d=(u8*)p; for(u64 i=0;i<n;i++)d[i]=0; }
static int lx_slen(const char* s){ int n=0; while(s[n])n++; return n; }

static void lx_fm_sync(int k,u64 off,u64 n);
static struct vfile* lx_vf(int fd){ int* t=fdt(); if(fd<0||fd>=FDMAX||t[fd]<0)return 0; struct vfile* v=&g_vf[t[fd]]; return v->used?v:0; }
static int lx_fd_install(int vi,int cloexec,int min){
    int* t=fdt(); for(int f=min;f<FDMAX;f++) if(t[f]==-1){ t[f]=vi; fdce()[f]=(u8)(cloexec?1:0); return f; }
    return -LX_EMFILE;
}
static int lx_path(int dirfd,u64 up,char* out,int empty_ok){
    char rel[LX_PATH]; int n=lx_str(up,rel,LX_PATH); if(n<0)return n;
    if(n==0){ if(!empty_ok)return -LX_ENOENT; out[0]=0; return 0; }
    { int c=0, lng=0; for(int i=0;i<n;i++){ if(rel[i]=='/')c=0; else { if(++c>255)return -LX_ENAMETOOLONG; if(c>63)lng=1; } }
      if(lng){ char mg[LX_PATH]; int o=0, i=0; while(i<n){ if(rel[i]=='/'){ if(o>=LX_PATH-1)return -LX_ENAMETOOLONG; mg[o++]=rel[i++]; continue; }
          int j=i; while(j<n&&rel[j]!='/')j++; int L=j-i;
          if(L<=63){ if(o+L>=LX_PATH-1)return -LX_ENAMETOOLONG; for(int k=0;k<L;k++)mg[o++]=rel[i+k]; }
          else { if(o+63>=LX_PATH-1)return -LX_ENAMETOOLONG; u64 h=0xcbf29ce484222325ULL; for(int k=0;k<L;k++){ h^=(u8)rel[i+k]; h*=0x100000001b3ULL; }
              for(int k=0;k<46;k++)mg[o++]=rel[i+k]; mg[o++]='~'; for(int k=15;k>=0;k--)mg[o++]="0123456789abcdef"[(h>>(k*4))&15]; }
          i=j; }
        mg[o]=0; for(int k=0;k<=o;k++)rel[k]=mg[k]; n=o; } }
    if(rel[0]=='/'){ for(int i=0;i<=n;i++)out[i]=rel[i]; return n; }
    int node=g_dcwd; const char* srb=0;
    if(dirfd!=LX_AT_FDCWD){ struct vfile* v=lx_vf(dirfd); if(!v)return -LX_EBADF; if(v->dev==LX_SRDIR)srb=v->vpath; else if(v->dev!=LX_DIRDEV)return -LX_ENOTDIR; else node=v->fs_idx; }
    char dp[LX_PATH]; if(srb){ int q=0; for(;srb[q]&&q<LX_PATH-1;q++)dp[q]=srb[q]; dp[q]=0; } else dt_path(node,dp); int o=0; for(int i=0;dp[i]&&o<LX_PATH-2;i++)out[o++]=dp[i];
    if(o>1)out[o++]='/'; for(int i=0;rel[i];i++){ if(o>=LX_PATH-1)return -LX_ENAMETOOLONG; out[o++]=rel[i]; }
    out[o]=0; return o;
}
static int lx_exists(const char* p){ u32 o[10]; return ux_stat2(p,o,1)==0; }
static int lx_isdir(const char* p){ u32 o[10]; if(ux_stat2(p,o,1))return 0; return o[9]||(o[0]&0170000u)==0040000u; }
static int lx_noent(const char* p){
    char q[LX_PATH]; int n=lx_slen(p); if(n>=LX_PATH)return -LX_ENOENT;
    for(int i=1;i<n;i++){ if(p[i]!='/')continue; for(int k=0;k<i;k++)q[k]=p[k]; q[i]=0;
        if(!lx_exists(q))return -LX_ENOENT; if(!lx_isdir(q))return -LX_ENOTDIR; }
    return -LX_ENOENT;
}
static int lx_parent_ok(const char* p){ int d; char b[64]; return k_split(p,&d,b)==0; }

static u32 lx_k2l(u32 k){ return k>>1; }
static u32 lx_l2k(u64 l){ return ((u32)l<<1)&~1u; }

static void lx_stat_out(u8* st,const u32* o,int dev_ino){
    lx_zero(st,144);
    *(u64*)(st+0)=0x4e5658ULL; *(u64*)(st+8)=(u64)o[5]+(u64)dev_ino; *(u64*)(st+16)=o[3];
    *(u32*)(st+24)=o[0]; *(u32*)(st+28)=o[1]; *(u32*)(st+32)=o[2];
    if((o[0]&0170000u)==0020000u)*(u64*)(st+40)=(u64)(1u<<8)|(o[5]&0xFF);
    *(u64*)(st+48)=o[4]; *(u64*)(st+56)=4096; *(u64*)(st+64)=((u64)o[4]+511)/512;
    *(u64*)(st+72)=o[7]; *(u64*)(st+88)=o[6]; *(u64*)(st+104)=o[8];
}
static int lx_stat_path(const char* p,u64 ubuf,int follow){
    u32 o[10]; if(ux_stat2(p,o,follow))return lx_noent(p);
    if(o[9])o[0]=(o[0]&0170000u)?o[0]:(0040000u|(o[0]&07777u));
    u8 st[144]; lx_stat_out(st,o,0); return lx_put(ubuf,st,144);
}
#define LX_AFU 18
#define LX_MFD 19
#define MFD_MAX 64
struct memfd { int used,ref,nframes,ncap,seals; u64 size; u64* frames; };
static struct memfd g_mfd[MFD_MAX];
static int lx_fstat(int fd,u64 ubuf){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    u32 o[10]; for(int i=0;i<10;i++)o[i]=0; o[3]=1;
    if(v->dev==0&&v->fs_idx>=0){ int k=v->fs_idx; o[0]=FS[k].mode?FS[k].mode:0100644u; o[1]=(u32)FS[k].uid; o[2]=(u32)FS[k].gid; o[3]=FS[k].orphan?0:(FS[k].nlink?FS[k].nlink:1); o[4]=(u32)FS[k].len; o[5]=(u32)k+1; o[6]=FS[k].mtime; o[7]=FS[k].atime; o[8]=FS[k].ctime; }
    else if(v->dev==LX_DIRDEV){ char dp[LX_PATH]; dt_path(v->fs_idx,dp); return lx_stat_path(dp,ubuf,1); }
    else if(v->dev==LX_SRDIR){ return lx_stat_path(v->vpath,ubuf,1); }
    else if(v->dev==14||v->dev==LX_AFU){ o[0]=0140777u; o[5]=7000u+(u32)v->fs_idx; }
    else if(v->dev==LX_MFD){ o[0]=0100600u; o[3]=1; o[4]=(u32)g_mfd[v->fs_idx].size; o[5]=30000u+(u32)v->fs_idx; }
    else if(v->dev==6||v->dev==7){ o[0]=0010600u; o[5]=5000u+(u32)v->fs_idx; }
    else if(v->dev==100&&v->vfs){ o[0]=0100444u; int n=v->vfs->size(v->vpath); o[4]=n>0?(u32)n:0; u32 h=2166136261u; for(int q=0;v->vpath[q];q++)h=(h^(u8)v->vpath[q])*16777619u; o[5]=0x10000000u|(h&0x0FFFFFFFu); }
    else { o[0]=0020666u; if(v->dev==5)o[0]=0020620u; o[5]=20000u+(u32)v->dev; }
    u8 st[144]; lx_stat_out(st,o,0); return lx_put(ubuf,st,144);
}

static long lx_proc_maps(const char* pa,int flags);
static long lx_proc_fd(const char* pa,int flags);
static int lx_open(int dirfd,u64 up,int flags,int mode){
    char p[LX_PATH]; int n=lx_path(dirfd,up,p,0); if(n<0)return n;
    { long pm=lx_proc_maps(p,flags); if(pm!=-9999)return (int)pm; pm=lx_proc_fd(p,flags); if(pm!=-9999)return (int)pm; }
    int acc=flags&3, cre=(flags&0x40)!=0, excl=(flags&0x80)!=0, dir=(flags&0x10000)!=0, cloexec=(flags&0x80000)!=0;
    int created=0;
    if(!dev_id(p)&&!vfs_lookup(p)){
        int isd=lx_isdir(p);
        if(isd){ if(acc!=0||(flags&0x200))return -LX_EISDIR; if(cre&&excl)return -LX_EEXIST;
            int d=dt_resolve_dir(p);
            if(d<0&&sysroot_isdir(p)){ int vi=jc_newvf(LX_SRDIR,0,flags); if(vi<0)return -LX_EMFILE; int k=0; for(;p[k]&&k<127;k++)g_vf[vi].vpath[k]=p[k]; g_vf[vi].vpath[k]=0; int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
            if(d<0)return -LX_ENOENT;
            int vi=jc_newvf(LX_DIRDEV,d,flags); if(vi<0)return -LX_EMFILE;
            int k=0; for(;p[k]&&k<63;k++)g_vf[vi].vpath[k]=p[k]; g_vf[vi].vpath[k]=0;
            int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
        if(dir)return lx_exists(p)?-LX_ENOTDIR:lx_noent(p);
        int ex=lx_exists(p);
        if(ex&&cre&&excl)return -LX_EEXIST;
        if(!ex&&!cre)return lx_noent(p);
        if(!ex&&!lx_parent_ok(p))return lx_noent(p);
        created=!ex;
        if(ex&&(acc!=0||(flags&0x200))){ int isd2; int k=ux_lookup(p,&isd2,1); if(k>=0&&!isd2&&!ux_perm_ok(k,2))return -LX_EACCES; }
        if(ex&&acc!=1){ int isd2; int k=ux_lookup(p,&isd2,1); if(k>=0&&!isd2&&!ux_perm_ok(k,4))return -LX_EACCES; }
    } else if(dir) return -LX_ENOTDIR;
    int fd=vfs_open(p,flags);
    if(fd<0){ int fv=0; for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used)fv++; int ff=0; int* t=fdt(); for(int i=0;i<FDMAX;i++) if(t[i]==-1)ff++;
        if(!fv)return -23; if(!ff)return -LX_EMFILE; return lx_exists(p)?-LX_EACCES:-LX_ENOSPC; }
    struct vfile* v=lx_vf(fd);
    if(v&&v->dev==0&&v->fs_idx>=0&&created){ int k=v->fs_idx; FS[k].mode=0100000u|((unsigned)mode&07777u&~(unsigned)g_umask); }
    if(v&&v->dev==0&&(flags&0x200)&&disk_ok&&v->fs_idx>=0){ int k=v->fs_idx; fs_sync(k); }
    return fd;
}

static int lx_nonblock(struct vfile* v){ return (v->flags&0x800)!=0; }
static long lx_sock_wait(struct sysregs* r,int n,int nb);
#define UN_MAX 128
#define UN_CAP 262144
#define UN_FQ 32
struct fbatch { u64 at; int nf,vf[8]; };
struct unsock { int used,state,peer,klen,rxn,rd_shut,wr_shut,backlog,qn,fqh,fqt,seq,rlh,rlt; u8 key[108]; u8* rx; u64 rcons; int q[16]; struct fbatch fq[UN_FQ]; int rl[64]; };
static struct unsock g_un[UN_MAX];
static struct unsock* un_get(int i){ return (i>=0&&i<UN_MAX&&g_un[i].used)?&g_un[i]:0; }
static int un_keyeq(struct unsock* s,const u8* k,int kl){ if(s->klen!=kl||kl<=0)return 0; for(int i=0;i<kl;i++) if(s->key[i]!=k[i])return 0; return 1; }
static int un_new(void){ for(int i=0;i<UN_MAX;i++) if(!g_un[i].used){ struct unsock* s=&g_un[i]; u8* z=(u8*)s; for(int k=0;k<(int)sizeof *s;k++)z[k]=0; s->rx=(u8*)kmalloc(UN_CAP); if(!s->rx)return -12; s->used=1; s->state=1; s->peer=-1; return i; } return -24; }
static void un_drop_fds(struct unsock* s){ while(s->fqh!=s->fqt){ struct fbatch* b=&s->fq[s->fqh%UN_FQ]; for(int k=0;k<b->nf;k++)vf_unref(b->vf[k]); s->fqh++; } }
void un_close(int i){ struct unsock* s=un_get(i); if(!s)return;
    un_drop_fds(s);
    if(s->peer>=0){ struct unsock* pr=un_get(s->peer); if(pr&&pr->peer==i){ pr->peer=-1; pr->rd_shut=0; } }
    for(int k=0;k<s->qn;k++){ struct unsock* c=un_get(s->q[k]); if(c){ c->peer=-1; c->state=5; } }
    if(s->rx)kfree(s->rx); s->rx=0; s->used=0; }
static int un_bind(int i,const u8* key,int kl){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state!=1)return -22; if(kl<=0||kl>108)return -22;
    for(int j=0;j<UN_MAX;j++) if(g_un[j].used&&(g_un[j].state==2||g_un[j].state==3)&&un_keyeq(&g_un[j],key,kl))return -98;
    for(int k=0;k<kl;k++)s->key[k]=key[k]; s->klen=kl; s->state=2; return 0; }
static int un_listen(int i,int backlog){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state==3){ s->backlog=backlog<1?1:backlog>16?16:backlog; return 0; } if(s->state!=2)return -22; s->state=3; s->qn=0; s->backlog=backlog<1?1:backlog>16?16:backlog; return 0; }
static int un_connect(int i,const u8* key,int kl){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state==4)return -106; if(s->state!=1&&s->state!=2)return -22;
    int L=-1; for(int j=0;j<UN_MAX;j++) if(g_un[j].used&&g_un[j].state==3&&un_keyeq(&g_un[j],key,kl)){ L=j; break; }
    if(L<0)return -111; struct unsock* lsn=&g_un[L]; if(lsn->qn>=lsn->backlog)return JS_BLOCK;
    int sv=un_new(); if(sv<0)return sv; struct unsock* gv=&g_un[sv]; gv->state=4; gv->peer=i; gv->klen=lsn->klen; for(int k=0;k<lsn->klen;k++)gv->key[k]=lsn->key[k];
    gv->seq=lsn->seq; s->seq=lsn->seq; s->peer=sv; s->state=4; lsn->q[lsn->qn++]=sv; return 0; }
static int un_accept(int i){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state!=3)return -22; if(!s->qn)return JS_BLOCK;
    int c=s->q[0]; for(int k=1;k<s->qn;k++)s->q[k-1]=s->q[k]; s->qn--; return c; }
static int un_send(int i,const u8* b,int n,const int* vf,int nfds){ struct unsock* s=un_get(i); if(!s)return -9; if(s->state!=4||s->wr_shut)return -32;
    struct unsock* pr=un_get(s->peer); if(!pr||pr->rd_shut)return -32;
    int room=UN_CAP-pr->rxn;
    if(s->seq){ if(n>UN_CAP)return -90; if(room<n||pr->rlt-pr->rlh>=64)return JS_BLOCK; pr->rl[pr->rlt%64]=n; pr->rlt++; }
    else if(room<=0)return n?JS_BLOCK:0; int c=n<room?n:room;
    if(nfds>0&&pr->fqt+1-pr->fqh<=UN_FQ){ struct fbatch* fb=&pr->fq[pr->fqt%UN_FQ]; fb->at=pr->rcons+(u64)pr->rxn; fb->nf=nfds>8?8:nfds; for(int k=0;k<fb->nf;k++){ fb->vf[k]=vf[k]; vf_ref(vf[k]); } pr->fqt++; }
    for(int k=0;k<c;k++)pr->rx[pr->rxn+k]=b[k]; pr->rxn+=c; return c; }
static void un_shift(struct unsock* s,int c){ int left=s->rxn-c; u8* d=s->rx; const u8* q=s->rx+c; int k=0;
    if(c>=8){ for(;k+8<=left;k+=8)*(u64*)(d+k)=*(const u64*)(q+k); }
    for(;k<left;k++)d[k]=q[k]; s->rxn=left; s->rcons+=(u64)c; }
static int un_recv(int i,u8* b,int n,int peek,int* ovf,int maxf,int* onf){ struct unsock* s=un_get(i); if(!s)return -9; if(onf)*onf=0;
    if(s->seq?s->rlh==s->rlt:s->rxn==0){ struct unsock* pr=un_get(s->peer); if(s->rd_shut||s->peer<0||!pr)return 0; return (n||s->seq)?JS_BLOCK:0; }
    int rec=s->seq?s->rl[s->rlh%64]:s->rxn; int c=rec<n?rec:n;
    for(int k=0;k<c;k++)b[k]=s->rx[k];
    if(s->seq&&!peek){ if(ovf&&onf){ while(s->fqh!=s->fqt){ struct fbatch* fb=&s->fq[s->fqh%UN_FQ]; if(fb->at>=s->rcons+(u64)(rec?rec:1))break; if(*onf+fb->nf>maxf)break; for(int k=0;k<fb->nf;k++)ovf[(*onf)++]=fb->vf[k]; s->fqh++; } }
        un_shift(s,rec); s->rlh++; return c; }
    if(!peek){ if(ovf&&onf){ while(s->fqh!=s->fqt){ struct fbatch* fb=&s->fq[s->fqh%UN_FQ]; if(fb->at>=s->rcons+(u64)c)break; if(*onf+fb->nf>maxf)break; for(int k=0;k<fb->nf;k++)ovf[(*onf)++]=fb->vf[k]; s->fqh++; } }
        un_shift(s,c); }
    return c; }
static int un_poll(int i){ struct unsock* s=un_get(i); if(!s)return 0x20; int re=0;
    if(s->state==3)return s->qn?1:0;
    struct unsock* pr=un_get(s->peer);
    if(s->rxn>0||(s->seq&&s->rlh!=s->rlt))re|=1; if(s->rd_shut||s->peer<0||!pr)re|=1|0x10;
    if(pr&&!pr->rd_shut&&UN_CAP-pr->rxn>0)re|=4; else if(!pr)re|=0x10;
    return re; }
static int mfd_new(void){ for(int i=0;i<MFD_MAX;i++) if(!g_mfd[i].used){ u8* z=(u8*)&g_mfd[i]; for(int k=0;k<(int)sizeof(struct memfd);k++)z[k]=0; g_mfd[i].used=1; g_mfd[i].ref=1; return i; } return -24; }
static long mfd_truncate(int i,u64 nsz){ if(i<0||i>=MFD_MAX||!g_mfd[i].used)return -9; struct memfd* m=&g_mfd[i]; if(nsz>0x40000000ULL)return -LX_EINVAL;
    u64 np=(nsz+4095)>>12;
    if((int)np>m->ncap){ u64* nf=(u64*)krealloc(m->frames,np*8); if(!nf)return -12; m->frames=nf; m->ncap=(int)np; }
    while((u64)m->nframes<np){ u64 f=pframe_alloc(); if(!f)return -12; m->frames[m->nframes++]=f; }
    while((u64)m->nframes>np)pframe_free(m->frames[--m->nframes]);
    m->size=nsz; return 0; }
void mfd_close(int i){ if(i<0||i>=MFD_MAX||!g_mfd[i].used)return; if(--g_mfd[i].ref>0)return; struct memfd* m=&g_mfd[i]; for(int k=0;k<m->nframes;k++)pframe_free(m->frames[k]); if(m->frames)kfree(m->frames); m->frames=0; m->used=0; }
static long mfd_io(struct vfile* v,u64 buf,u64 len,long off,int wr){
    if(v->fs_idx<0||v->fs_idx>=MFD_MAX||!g_mfd[v->fs_idx].used)return -LX_EBADF; struct memfd* m=&g_mfd[v->fs_idx]; u64 o=off<0?(u64)v->off:(u64)off;
    if(wr){ if(m->seals&8)return -LX_EPERM; if(o+len>m->size){ if(m->seals&4)return -LX_EPERM; long e=mfd_truncate(v->fs_idx,o+len); if(e)return e; } }
    else { if(o>=m->size)return 0; if(len>m->size-o)len=m->size-o; }
    u64 done=0; while(done<len){ u64 pg=(o+done)>>12, po=(o+done)&4095, n=4096-po; if(n>len-done)n=len-done; u8* f=(u8*)m->frames[pg]+po;
        if(wr?lx_get(f,buf+done,n):lx_put(buf+done,f,n))return done?(long)done:-LX_EFAULT; done+=n; }
    if(off<0)v->off=(int)(o+done); return (long)done; }
#define LX_EVFD 20
#define LX_TMFD 21
#define EV_MAX 128
#define TM_MAX 64
struct evfd { int used,sema; u64 cnt; };
static struct evfd g_ev[EV_MAX];
struct tmfd { int used,clk; u64 next,ival,exp; };
static struct tmfd g_tm[TM_MAX];
static u64 lx_now_ns(void);
static u64 tm_now(int clk){ return (clk==0||clk==8)?lx_now_ns():hr_ns(); }
static void tm_update(struct tmfd* t){ if(!t->next)return; u64 now=tm_now(t->clk); if(now<t->next)return;
    if(t->ival){ u64 n=1+(now-t->next)/t->ival; t->exp+=n; t->next+=n*t->ival; } else { t->exp+=1; t->next=0; } }
void evtm_close(int dev,int i){ if(dev==LX_EVFD&&i>=0&&i<EV_MAX)g_ev[i].used=0; if(dev==LX_TMFD&&i>=0&&i<TM_MAX)g_tm[i].used=0; }
static long lx_eventfd(u64 initval,int flags){ int i=-1; for(int k=0;k<EV_MAX;k++) if(!g_ev[k].used){ i=k; break; } if(i<0)return -LX_EMFILE;
    g_ev[i].used=1; g_ev[i].cnt=(u32)initval; g_ev[i].sema=(flags&1)!=0;
    int vi=jc_newvf(LX_EVFD,i,O_RDWR|(flags&0x800)); if(vi<0){ g_ev[i].used=0; return -LX_EMFILE; } int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static int ev_read(int i,u64 buf,u64 len){ if(len<8)return -LX_EINVAL; struct evfd* e=&g_ev[i]; if(!e->cnt)return JS_BLOCK; u64 v=e->sema?1:e->cnt; e->cnt-=v; if(lx_put(buf,&v,8))return -LX_EFAULT; return 8; }
static int ev_write(int i,u64 buf,u64 len){ if(len<8)return -LX_EINVAL; u64 v; if(lx_get(&v,buf,8))return -LX_EFAULT; if(v==~0ULL)return -LX_EINVAL; struct evfd* e=&g_ev[i]; if(e->cnt+v<e->cnt||e->cnt+v>0xFFFFFFFFFFFFFFFEULL)return JS_BLOCK; e->cnt+=v; return 8; }
static long lx_timerfd_create(int clk,int flags){ if(clk!=0&&clk!=1&&clk!=7&&clk!=8&&clk!=9)return -LX_EINVAL; int i=-1; for(int k=0;k<TM_MAX;k++) if(!g_tm[k].used){ i=k; break; } if(i<0)return -LX_EMFILE;
    g_tm[i].used=1; g_tm[i].clk=clk; g_tm[i].next=0; g_tm[i].ival=0; g_tm[i].exp=0;
    int vi=jc_newvf(LX_TMFD,i,O_RDWR|(flags&0x800)); if(vi<0){ g_tm[i].used=0; return -LX_EMFILE; } int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static long lx_timerfd_get(struct tmfd* t,u64 out){ tm_update(t); u64 o[4]={0,0,0,0}; o[0]=t->ival/1000000000ULL; o[1]=t->ival%1000000000ULL; if(t->next){ u64 now=tm_now(t->clk); u64 r=t->next>now?t->next-now:1; o[2]=r/1000000000ULL; o[3]=r%1000000000ULL; } return lx_put(out,o,32); }
static long lx_timerfd_set(int fd,int flags,u64 nv,u64 ov){ struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=LX_TMFD)return -LX_EINVAL; struct tmfd* t=&g_tm[v->fs_idx];
    if(ov){ long e=lx_timerfd_get(t,ov); if(e)return e; }
    u64 s[4]; if(lx_get(s,nv,32))return -LX_EFAULT; if(s[1]>=1000000000ULL||s[3]>=1000000000ULL)return -LX_EINVAL;
    u64 val=s[2]*1000000000ULL+s[3], ival=s[0]*1000000000ULL+s[1];
    t->exp=0; t->ival=ival; if(!val){ t->next=0; return 0; }
    u64 now=tm_now(t->clk); t->next=(flags&1)?(val>now?val:now):now+val; if(!t->next)t->next=1; return 0; }
static int tm_read(int i,u64 buf,u64 len){ if(len<8)return -LX_EINVAL; struct tmfd* t=&g_tm[i]; tm_update(t); if(!t->exp)return JS_BLOCK; u64 v=t->exp; t->exp=0; if(lx_put(buf,&v,8))return -LX_EFAULT; return 8; }
u32 g_enosys_nr[48], g_enosys_ct[48]; int g_enosys_n;
u32 g_gap_seq, g_enosys_last[48]; u32 g_gap_kind[40], g_gap_ct[40], g_gap_last[40]; u64 g_gap_val[40]; int g_gap_n;
static void enosys_note(u64 nr){ g_gap_seq++; for(int i=0;i<g_enosys_n;i++) if(g_enosys_nr[i]==(u32)nr){ g_enosys_ct[i]++; g_enosys_last[i]=g_gap_seq; return; } if(g_enosys_n<48){ g_enosys_nr[g_enosys_n]=(u32)nr; g_enosys_ct[g_enosys_n]=1; g_enosys_last[g_enosys_n]=g_gap_seq; g_enosys_n++; } }
void lx_gap(u32 kind,u64 val){ g_gap_seq++; for(int i=0;i<g_gap_n;i++) if(g_gap_kind[i]==kind&&g_gap_val[i]==val){ g_gap_ct[i]++; g_gap_last[i]=g_gap_seq; return; } if(g_gap_n<40){ g_gap_kind[g_gap_n]=kind; g_gap_val[g_gap_n]=val; g_gap_ct[g_gap_n]=1; g_gap_last[g_gap_n]=g_gap_seq; g_gap_n++; } }
static long lx_memfd(int flags){ if(flags&~0x1F)return -LX_EINVAL; int i=mfd_new(); if(i<0)return i; if(flags&8)g_mfd[i].seals=0x20; else if(!(flags&2))g_mfd[i].seals=1; int vi=jc_newvf(LX_MFD,i,O_RDWR); if(vi<0){ mfd_close(i); return -LX_EMFILE; } int fd=lx_fd_install(vi,(flags&1)!=0,0); if(fd<0){ vf_unref(vi); return fd; } return fd; }
static long lx_read(struct sysregs* r,int fd,u64 buf,u64 len){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if((v->flags&3)==1)return -LX_EBADF;
    if(v->dev==LX_DIRDEV)return -LX_EISDIR;
    if(len>LX_IOMAX)len=LX_IOMAX; if(!len)return 0;
    if(!lx_okw(buf,len))return -LX_EFAULT;
    if(v->dev==14)return lx_sock_wait(r,lxs_recv(v->fs_idx,(u8*)buf,(int)len,0,0,0),lx_nonblock(v));
    if(v->dev==LX_AFU)return lx_sock_wait(r,un_recv(v->fs_idx,(u8*)buf,(int)len,0,0,0,0),lx_nonblock(v));
    if(v->dev==LX_EVFD)return lx_sock_wait(r,ev_read(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_TMFD)return lx_sock_wait(r,tm_read(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_MFD)return mfd_io(v,buf,len,-1,0);
    int n=vfs_read(fd,(char*)buf,(int)len);
    if(n==JS_BLOCK){ if(lx_nonblock(v))return -LX_EAGAIN; jc_block(r,S_WAIT,g_ticks+1); }
    if(n<0)return v->dev==5?-LX_EIO:-LX_EBADF;
    return n;
}
static void lx_persist(int fd){
    struct vfile* v=lx_vf(fd);
    if(v&&v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N)g_fs_dirty[v->fs_idx]=1;
}
static long lx_write(struct sysregs* r,int fd,u64 buf,u64 len){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if((v->flags&3)==0&&v->dev==0)return -LX_EBADF;
    if(v->dev==LX_DIRDEV||v->dev==6||v->dev==100)return -LX_EBADF;
    if(len>LX_IOMAX)len=LX_IOMAX; if(!len)return 0;
    if(!lx_ok(buf,len))return -LX_EFAULT;
    if(v->dev==14){ int n=lxs_send(v->fs_idx,(const u8*)buf,(int)len,0,0,0); if(n==-32)jc_signal_proc(g_jcur,13); return lx_sock_wait(r,n,lx_nonblock(v)); }
    if(v->dev==LX_AFU){ int n=un_send(v->fs_idx,(const u8*)buf,(int)len,0,0); if(n==-32)jc_signal_proc(g_jcur,13); return lx_sock_wait(r,n,lx_nonblock(v)); }
    if(v->dev==LX_EVFD)return lx_sock_wait(r,ev_write(v->fs_idx,buf,len),lx_nonblock(v));
    if(v->dev==LX_TMFD)return -LX_EINVAL;
    if(v->dev==LX_MFD){ if((v->flags&3)==0)return -LX_EBADF; return mfd_io(v,buf,len,-1,1); }
    if(v->dev==0&&(v->flags&0x400)&&v->fs_idx>=0)v->off=FS[v->fs_idx].len;
    if(v->dev==0&&v->fs_idx>=0&&(v->off<0||v->off>=FS_CAP-1))return -27;
    int wo=v->off;
    int n=vfs_write(fd,(const char*)buf,(int)len);
    if(n>0&&v->dev==0&&v->fs_idx>=0)lx_fm_sync(v->fs_idx,(u64)wo,(u64)n);
    if(n==JS_BLOCK){ if(lx_nonblock(v))return -LX_EAGAIN; jc_block(r,S_WAIT,g_ticks+1); }
    if(n<0){ if(v->dev==7)return -LX_EPIPE; if(v->dev==4)return -LX_ENOSPC; if(v->dev==0)return -LX_EACCES; return -LX_EIO; }
    if(n>0)lx_persist(fd);
    return n;
}
static long lx_rwv(struct sysregs* r,int fd,u64 iov,int cnt,int wr){
    if(cnt<0||cnt>1024)return -LX_EINVAL; if(!lx_ok(iov,(u64)cnt*16))return -LX_EFAULT;
    long tot=0;
    for(int i=0;i<cnt;i++){ u64 b=((u64*)iov)[i*2], l=((u64*)iov)[i*2+1]; if(!l)continue;
        struct vfile* v=lx_vf(fd); if(!v)return tot?tot:-LX_EBADF;
        if(tot>0){ if(!wr&&v->dev==6&&g_pipe[v->fs_idx].n==0)break; if(wr&&v->dev==7&&g_pipe[v->fs_idx].n>=g_pipe[v->fs_idx].cap)break; if(!wr&&v->dev==5&&g_tin_n==0)break; if(v->dev==14&&(wr?g_lxs[v->fs_idx].txn>=LXS_TXCAP:g_lxs[v->fs_idx].rxn==0))break; if(v->dev==LX_AFU){ struct unsock* su=un_get(v->fs_idx); if(su&&(wr?(su->peer>=0&&un_get(su->peer)&&g_un[su->peer].rxn>=UN_CAP):su->rxn==0))break; } }
        long n=wr?lx_write(r,fd,b,l):lx_read(r,fd,b,l);
        if(n<0)return tot?tot:n; tot+=n; if((u64)n<l)break; }
    return tot;
}
static long lx_prw(int fd,u64 buf,u64 len,long off,int wr){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if(v->dev==LX_MFD){ if(off<0)return -LX_EINVAL; if(wr?((v->flags&3)==0):((v->flags&3)==1))return -LX_EBADF; if(len>LX_IOMAX)len=LX_IOMAX; if(!(wr?lx_ok(buf,len):lx_okw(buf,len)))return -LX_EFAULT; return mfd_io(v,buf,len,off,wr); }
    if(v->dev==100&&!wr&&v->vfs){ if(off<0)return -LX_EINVAL; if(len>LX_IOMAX)len=LX_IOMAX; if(!lx_okw(buf,len))return -LX_EFAULT; int save=v->off; v->off=(int)off; int n=vfs_read(fd,(char*)buf,(int)len); v->off=save; return n<0?-LX_EIO:n; }
    if(v->dev!=0||v->fs_idx<0)return -LX_ESPIPE; if(off<0)return -LX_EINVAL;
    if(wr?((v->flags&3)==0):((v->flags&3)==1))return -LX_EBADF;
    if(wr&&off>=FS_CAP-1)return -27;
    if(len>LX_IOMAX)len=LX_IOMAX; if(!(wr?lx_ok(buf,len):lx_okw(buf,len)))return -LX_EFAULT;
    if(off>FS_CAP)off=FS_CAP;
    int save=v->off, fl=v->flags; v->off=(int)off; v->flags&=~0x400;
    int n=wr?vfs_write(fd,(const char*)buf,(int)len):vfs_read(fd,(char*)buf,(int)len);
    v->off=save; v->flags=fl; if(n<0)return wr?-LX_EACCES:-LX_EIO; if(wr&&n>0){ lx_fm_sync(v->fs_idx,(u64)off,(u64)n); lx_persist(fd); } return n;
}
static long lx_lseek(int fd,long off,int wh){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if(wh<0||wh>2)return -LX_EINVAL;
    if(v->dev==LX_DIRDEV||v->dev==LX_SRDIR){ if(wh==0&&off>=0){ v->off=(int)off; return off; } return -LX_EINVAL; }
    if(v->dev==LX_MFD){ long b=wh==1?v->off:wh==2?(long)g_mfd[v->fs_idx].size:0; if(b+off<0||b+off>0x7FFFFFFFL)return -LX_EINVAL; v->off=(int)(b+off); return v->off; }
    if(v->dev==100){ long b=wh==1?v->off:wh==2?(long)v->vfs->size(v->vpath):0; if(b+off<0||b+off>0x7FFFFFFFL)return -LX_EINVAL; v->off=(int)(b+off); return v->off; }
    if(v->dev!=0&&v->dev!=9)return (v->dev==5||v->dev==6||v->dev==7||v->dev==14||v->dev==LX_AFU)?-LX_ESPIPE:0;
    long b=(wh==1)?v->off:(wh==2)?(v->dev==9?(long)W*H*4:FS[v->fs_idx].len):0;
    if(b+off<0)return -LX_EINVAL; if(b+off>0x7FFFFFFFL)return -LX_EINVAL;
    v->off=(int)(b+off); return v->off;
}
static long lx_getdents(int fd,u64 buf,u64 cap){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=LX_DIRDEV&&v->dev!=LX_SRDIR)return -LX_ENOTDIR;
    if(cap>65536)cap=65536; if(!lx_okw(buf,cap))return -LX_EFAULT;
    char dp[LX_PATH]; if(v->dev==LX_SRDIR){ int q=0; for(;v->vpath[q]&&q<LX_PATH-1;q++)dp[q]=v->vpath[q]; dp[q]=0; } else dt_path(v->fs_idx,dp);
    static int ndt_cache_fd=-1; static int ndt_cache=0; (void)ndt_cache_fd; (void)ndt_cache;
    u64 used=0;
    for(;;){ int n=v->off; char name[72]; int type=0;
        if(n==0){ name[0]='.'; name[1]=0; type=4; }
        else if(n==1){ name[0]='.'; name[1]='.'; name[2]=0; type=4; }
        else { int got=0; int ndt=0;
            if(v->dev==LX_DIRDEV){ got=(k_readdir_at(dp,n-2,name,&type)==1); if(!got){ int lo=0, hi=n-2; while(lo<hi){ int mid=(lo+hi)/2; char t2[72]; int ty2; if(k_readdir_at(dp,mid,t2,&ty2)==1)lo=mid+1; else hi=mid; } ndt=lo; } }
            if(!got){ int si=n-2-ndt; for(;;){ int sd=0; if(!sysroot_list(dp,si,name,64,&sd))break; char ep[LX_PATH]; int o=0; for(int i=0;dp[i]&&o<LX_PATH-2;i++)ep[o++]=dp[i]; if(o>1)ep[o++]='/'; for(int i=0;name[i]&&o<LX_PATH-1;i++)ep[o++]=name[i]; ep[o]=0;
                    int isd2=0; if(v->dev==LX_DIRDEV&&(ux_lookup(ep,&isd2,0)>=0)){ si++; v->off++; continue; } type=sysroot_isdir(ep)?4:8; got=1; break; } }
            if(!got)break; }
        int L=lx_slen(name); u64 rl=(19+(u64)L+1+7)&~7ULL;
        if(used+rl>cap){ if(!used)return -LX_EINVAL; break; }
        u8* d=(u8*)(buf+used); lx_zero(d,rl);
        u64 ino=(u64)(n+1)*7919ULL;
        if(n==0)ino=10000u+(u32)v->fs_idx; else if(n==1)ino=10000u+(u32)(v->fs_idx>0&&v->fs_idx<DN_MAX&&DT[v->fs_idx].parent>=0?DT[v->fs_idx].parent:0);
        else { char ep[LX_PATH]; int o=0; for(int i=0;dp[i]&&o<LX_PATH-2;i++)ep[o++]=dp[i]; if(o>1&&o<LX_PATH-2)ep[o++]='/'; for(int i=0;name[i]&&o<LX_PATH-1;i++)ep[o++]=name[i]; ep[o]=0; u32 so[10]; if(ux_stat2(ep,so,0)==0)ino=so[5]; }
        *(u64*)d=ino; *(i64*)(d+8)=n+1; *(u16*)(d+16)=(u16)rl; d[18]=(u8)type;
        for(int i=0;i<=L;i++)d[19+i]=(u8)name[i];
        used+=rl; v->off++; }
    return (long)used;
}

static int lx_poll_one(int fd,int ev){
    struct vfile* v=lx_vf(fd); if(!v)return 0x20;
    int re=0;
    if(v->dev==6){ struct jpipe* pp=&g_pipe[v->fs_idx]; if(pp->n>0)re|=1; if(pp->writers<=0)re|=0x10; }
    else if(v->dev==7){ struct jpipe* pp=&g_pipe[v->fs_idx]; if(pp->readers<=0)re|=8; else if(pp->n<pp->cap)re|=4; }
    else if(v->dev==5){ re|=4; struct jproc* p=jc_lead(&g_jp[g_jcur]); if((g_tin_n>0||g_tin_eof)&&p->pgid==g_tty_fg)re|=1; }
    else if(v->dev==10){ re|=4; if(g_mq_h!=g_mq_t)re|=1; }
    else if(v->dev==16){ extern int xin_ready(void); re|=4; if(xin_ready())re|=1; }
    else if(v->dev==LX_AFU)re=un_poll(v->fs_idx);
    else if(v->dev==LX_EVFD){ re|=4; if(g_ev[v->fs_idx].cnt)re|=1; }
    else if(v->dev==LX_TMFD){ tm_update(&g_tm[v->fs_idx]); if(g_tm[v->fs_idx].exp)re|=1; }
    else if(v->dev==14)re=lxs_poll(v->fs_idx);
    else re|=5;
    return re&(ev|0x38);
}
static int lx_wait_deadline(struct jproc* p,long ms){
    if(ms==0)return 1;
    if(!p->pollwake)p->pollwake=(ms<0)?~0ULL:g_ticks+((u64)ms+9)/10+1;
    if(g_ticks>=p->pollwake){ p->pollwake=0; return 1; }
    return 0;
}
static long lx_poll(struct sysregs* r,u64 fds,u64 n,long ms){
    struct jproc* p=&g_jp[g_jcur];
    if(n>FDMAX*4)return -LX_EINVAL; if(!lx_okw(fds,n*8))return -LX_EFAULT;
    int cnt=0;
    for(u64 i=0;i<n;i++){ int fd=*(int*)(fds+i*8); short ev=*(short*)(fds+i*8+4); short re=0;
        if(fd>=0)re=(short)lx_poll_one(fd,ev); *(short*)(fds+i*8+6)=re; if(re)cnt++; }
    if(cnt){ p->pollwake=0; return cnt; }
    if(lx_wait_deadline(p,ms))return 0;
    if(jc_deliverable(p)){ p->pollwake=0; return -LX_EINTR; }
    jc_block(r,S_WAIT,g_ticks+1);
}
#define LX_EPOLL 113
#define EPN 64
#define EPI 64
struct lx_ep { int used, n; int fd[EPN]; u32 ev[EPN]; u64 data[EPN]; };
static struct lx_ep g_ep[EPI];
static long lx_epoll_create(int fl){ int e=-1; for(int i=0;i<EPI;i++) if(!g_ep[i].used){ e=i; break; } if(e<0)return -LX_ENOMEM;
    g_ep[e].used=1; g_ep[e].n=0; int vi=jc_newvf(LX_EPOLL,e,O_RDWR); if(vi<0){ g_ep[e].used=0; return -LX_EMFILE; } int fd=lx_fd_install(vi,(fl&0x80000)?1:0,0); if(fd<0){ vf_unref(vi); g_ep[e].used=0; } return fd; }
static struct lx_ep* lx_ep_of(int fd){ struct vfile* v=lx_vf(fd); if(!v||v->dev!=LX_EPOLL)return 0; if(v->fs_idx<0||v->fs_idx>=EPI)return 0; return &g_ep[v->fs_idx]; }
static long lx_epoll_ctl(int epfd,int op,int fd,u64 uev){
    struct lx_ep* e=lx_ep_of(epfd); if(!e)return -LX_EINVAL; if(!lx_vf(fd))return -LX_EBADF;
    u32 ev=0; u64 data=0; if(op!=2){ u8 b[12]; if(lx_get(b,uev,12))return -LX_EFAULT; ev=*(u32*)b; data=*(u64*)(b+4); }
    int k=-1; for(int i=0;i<e->n;i++) if(e->fd[i]==fd){ k=i; break; }
    if(op==1){ if(k>=0)return -17; if(e->n>=EPN)return -LX_ENOMEM; e->fd[e->n]=fd; e->ev[e->n]=ev; e->data[e->n]=data; e->n++; return 0; }
    if(op==3){ if(k<0)return -LX_ENOENT; e->ev[k]=ev; e->data[k]=data; return 0; }
    if(op==2){ if(k<0)return -LX_ENOENT; e->n--; e->fd[k]=e->fd[e->n]; e->ev[k]=e->ev[e->n]; e->data[k]=e->data[e->n]; return 0; }
    return -LX_EINVAL; }
static long lx_epoll_wait(struct sysregs* r,int epfd,u64 out,int max,long ms){
    struct jproc* p=&g_jp[g_jcur]; struct lx_ep* e=lx_ep_of(epfd); if(!e)return -LX_EINVAL; if(max<=0)return -LX_EINVAL; if(!lx_okw(out,(u64)max*12))return -LX_EFAULT;
    int cnt=0;
    for(int i=0;i<e->n&&cnt<max;i++){ if(!lx_vf(e->fd[i])){ e->n--; e->fd[i]=e->fd[e->n]; e->ev[i]=e->ev[e->n]; e->data[i]=e->data[e->n]; i--; continue; }
        int re=lx_poll_one(e->fd[i],(int)(e->ev[i]&0x3F)); if(!re)continue;
        u8* o=(u8*)(out+(u64)cnt*12); *(u32*)o=(u32)re; *(u64*)(o+4)=e->data[i]; cnt++;
        if(e->ev[i]&0x40000000u)e->ev[i]&=~0x3Fu; }
    if(cnt){ p->pollwake=0; return cnt; }
    if(lx_wait_deadline(p,ms))return 0;
    if(jc_deliverable(p)){ p->pollwake=0; return -LX_EINTR; }
    jc_block(r,S_WAIT,g_ticks+1);
}
static u64 lx_tv_ticks(const u64* tv){ u64 us=tv[0]*1000000ULL+tv[1]; return (us+9999ULL)/10000ULL; }
static void lx_ticks_tv(u64 t,u64* tv){ tv[0]=t/100ULL; tv[1]=(t%100ULL)*10000ULL; }
static long lx_getitimer(int which,u64 out){
    struct jproc* p=jc_lead(&g_jp[g_jcur]); u64 v[4]={0,0,0,0};
    if(which<0||which>2)return -LX_EINVAL;
    if(which==0){ lx_ticks_tv(p->it_int,v); if(p->it_next>g_ticks)lx_ticks_tv(p->it_next-g_ticks,v+2); else if(p->it_next)v[3]=1; }
    return lx_put(out,v,32);
}
static long lx_setitimer(int which,u64 in,u64 out){
    struct jproc* p=jc_lead(&g_jp[g_jcur]);
    if(which<0||which>2)return -LX_EINVAL;
    if(out){ long e=lx_getitimer(which,out); if(e)return e; }
    if(!in)return 0;
    u64 v[4]; if(lx_get(v,in,32))return -LX_EFAULT;
    if(v[1]>=1000000ULL||v[3]>=1000000ULL)return -LX_EINVAL;
    if(which!=0)return 0;
    u64 val=lx_tv_ticks(v+2), itv=lx_tv_ticks(v);
    p->it_int=itv; p->it_next=val?g_ticks+val:0;
    return 0;
}
static long lx_alarm(unsigned sec){
    struct jproc* p=jc_lead(&g_jp[g_jcur]);
    u64 left=p->it_next>g_ticks?p->it_next-g_ticks:0;
    p->it_int=0; p->it_next=sec?g_ticks+(u64)sec*100ULL:0;
    return (long)((left+99ULL)/100ULL);
}
static long lx_select(struct sysregs* r,int n,u64 rd,u64 wr,u64 ex,long ms){
    struct jproc* p=&g_jp[g_jcur];
    if(n<0||n>1024)return -LX_EINVAL;
    int nb=(n+7)/8; u8 R[128],Wm[128],oR[128],oW[128];
    lx_zero(R,128); lx_zero(Wm,128); lx_zero(oR,128); lx_zero(oW,128);
    if(rd&&!lx_okw(rd,(u64)nb))return -LX_EFAULT; if(wr&&!lx_okw(wr,(u64)nb))return -LX_EFAULT; if(rd&&lx_get(R,rd,(u64)nb))return -LX_EFAULT; if(wr&&lx_get(Wm,wr,(u64)nb))return -LX_EFAULT; if(ex&&!lx_okw(ex,(u64)nb))return -LX_EFAULT;
    int cnt=0;
    for(int fd=0;fd<n;fd++){ int ir=(R[fd>>3]>>(fd&7))&1, iw=(Wm[fd>>3]>>(fd&7))&1; if(!ir&&!iw)continue;
        if(fd>=FDMAX||!lx_vf(fd))return -LX_EBADF;
        int re=lx_poll_one(fd,(ir?1:0)|(iw?4:0));
        if(ir&&(re&0x19)){ oR[fd>>3]|=(u8)(1u<<(fd&7)); cnt++; }
        if(iw&&(re&0x0C)){ oW[fd>>3]|=(u8)(1u<<(fd&7)); cnt++; } }
    if(cnt||lx_wait_deadline(p,ms)){ p->pollwake=0; if(rd)lx_put(rd,oR,(u64)nb); if(wr)lx_put(wr,oW,(u64)nb); if(ex){ u8 z[128]; lx_zero(z,128); lx_put(ex,z,(u64)nb); } return cnt; }
    if(jc_deliverable(p)){ p->pollwake=0; return -LX_EINTR; }
    jc_block(r,S_WAIT,g_ticks+1);
}

struct lx_fm { u64 cr3,va,off,pages; int k; };
static struct lx_fm g_fm[256];
static void lx_fm_add(u64 cr3,u64 va,u64 off,int k,u64 pages){ for(int i=0;i<256;i++) if(!g_fm[i].pages){ g_fm[i].cr3=cr3&~0xFFFULL; g_fm[i].va=va; g_fm[i].off=off; g_fm[i].k=k; g_fm[i].pages=pages; return; } }
static void lx_fm_wb(struct lx_fm* m,u64 a,u64 e){
    int k=m->k; if(k<0||k>=FS_N||!FS[k].used)return; u64 me=m->va+(m->pages<<12); if(a<m->va)a=m->va; if(e>me)e=me;
    for(u64 v=a&~0xFFFULL;v<e;v+=0x1000ULL){ u64* pt=vm_walk(m->cr3,v,0); if(!pt||!(*pt&1))continue; u64 fo=m->off+(v-m->va); if(fo>=(u64)FS[k].len)break;
        u64 n=(u64)FS[k].len-fo; if(n>4096)n=4096; const u8* f=(const u8*)(*pt&PTE_ADDR); char* d=fsd(k); if(!d)return; d+=fo; int ch=0;
        for(u64 b=0;b<n;b++) if((u8)d[b]!=f[b]){ d[b]=(char)f[b]; ch=1; } if(ch){ g_fs_dirty[k]=1; g_fs_wb_pending=1; } } }
static void lx_fm_sync(int k,u64 off,u64 n){
    if(n==0)return; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(!m->pages||m->k!=k)continue; u64 me=m->off+(m->pages<<12); if(off+n<=m->off||off>=me)continue;
        u64 a=off>m->off?off:m->off, e=off+n<me?off+n:me; const char* d=fsd(k); if(!d)return;
        for(u64 q=a;q<e;){ u64 v=m->va+(q-m->off); u64* pt=vm_walk(m->cr3,v&~0xFFFULL,0); u64 pe=(q|0xFFFULL)+1; if(pe>e)pe=e;
            if(pt&&(*pt&1)){ u8* f=(u8*)(*pt&PTE_ADDR); for(u64 b=q;b<pe;b++)f[(m->va+(b-m->off))&0xFFF]=(u8)d[b]; } q=pe; } } }
static void lx_fm_cut(u64 cr3,u64 a,u64 e){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(!m->pages||m->cr3!=cr3)continue; u64 me=m->va+(m->pages<<12); if(e<=m->va||a>=me)continue; lx_fm_wb(m,a,e); if(a<=m->va&&e>=me)m->pages=0; } }
static void lx_fm_msync(u64 cr3,u64 a,u64 e){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(m->pages&&m->cr3==cr3)lx_fm_wb(m,a,e); } }
static void lx_fm_dead(u64 cr3){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_fm* m=&g_fm[i]; if(m->pages&&m->cr3==cr3){ lx_fm_wb(m,m->va,m->va+(m->pages<<12)); m->pages=0; } } }
static void lx_fm_dup(u64 src,u64 dst){ src&=~0xFFFULL; int n=0; for(int i=0;i<256;i++) if(g_fm[i].pages&&g_fm[i].cr3==src)n++; for(int i=0;i<256&&n>0;i++) if(g_fm[i].pages&&g_fm[i].cr3==src){ lx_fm_add(dst,g_fm[i].va,g_fm[i].off,g_fm[i].k,g_fm[i].pages); n--; } }
static void lx_sha_dead(u64 cr3);
static void lx_sha_dup(u64 src,u64 dst);
struct lx_res { u64 cr3,b,e; };
static struct lx_res g_res[512];
static int lx_res_find(u64 cr3,u64 v){ cr3&=~0xFFFULL; for(int i=0;i<512;i++) if(g_res[i].e&&g_res[i].cr3==cr3&&v>=g_res[i].b&&v<g_res[i].e)return i; return -1; }
static u64 lx_res_next(u64 cr3,u64 v){ cr3&=~0xFFFULL; u64 m=~0ULL; for(int i=0;i<512;i++) if(g_res[i].e&&g_res[i].cr3==cr3&&g_res[i].b>v&&g_res[i].b<m)m=g_res[i].b; return m; }
static int lx_res_add(u64 cr3,u64 b,u64 e){ for(int i=0;i<512;i++) if(!g_res[i].e){ g_res[i].cr3=cr3&~0xFFFULL; g_res[i].b=b; g_res[i].e=e; return 0; } return -1; }
static void lx_res_cut(u64 cr3,u64 a,u64 e){ cr3&=~0xFFFULL;
    for(int i=0;i<512;i++){ struct lx_res* r=&g_res[i]; if(!r->e||r->cr3!=cr3||e<=r->b||a>=r->e)continue;
        if(a<=r->b&&e>=r->e){ r->e=0; continue; }
        if(a<=r->b){ r->b=e; continue; }
        if(e>=r->e){ r->e=a; continue; }
        u64 oe=r->e; r->e=a; lx_res_add(cr3,e,oe); } }
int lx_res_fault(u64 cr3,u64 va){ if(lx_res_find(cr3,va)<0)return -1; u64* e=vm_walk(cr3&~0xFFFULL,va&~0xFFFULL,0); if(e&&vm_pte_used(*e))return -1; return vm_map_lazy(cr3&~0xFFFULL,va&~0xFFFULL)?-1:0; }
void lx_res_dup(u64 src,u64 dst){ lx_fm_dup(src,dst); lx_sha_dup(src,dst); src&=~0xFFFULL; int n=0; for(int i=0;i<512;i++) if(g_res[i].e&&g_res[i].cr3==src)n++; for(int i=0;i<512&&n>0;i++) if(g_res[i].e&&g_res[i].cr3==src){ lx_res_add(dst,g_res[i].b,g_res[i].e); n--; } }
void lx_res_drop(u64 cr3){ cr3&=~0xFFFULL; for(int i=0;i<512;i++) if(g_res[i].cr3==cr3)g_res[i].e=0; }
static void lx_unmap_range(u64 a,u64 e){
    struct jproc* p=&g_jp[g_jcur]; { extern void xfb_untrack_range(u64,u64,u64); xfb_untrack_range(p->cr3,a,e); } lx_res_cut(p->cr3,a,e); lx_fm_cut(p->cr3,a,e); int shared=jc_tg_live(p->tg,g_jcur)>0; u64 nf=0; u64 fr[64];
    for(u64 v=a;v<e;v+=0x1000ULL){ u64* pt=vm_walk(p->cr3,v,0); if(!pt)continue;
        if(*pt&1){ u64 pa=*pt&PTE_ADDR; *pt=0; cpu_flush_page(v); { extern int g_xfb_live; extern void xfb_drop(u64); extern u32 pframe_refcount(u64); if(g_xfb_live&&pframe_refcount(pa)<=1)xfb_drop(pa); }
            if(!shared){ pframe_free(pa); continue; }
            fr[nf++]=pa; if(nf==64){ smp_tlb_sync(p->cr3); for(u64 k=0;k<nf;k++)pframe_free(fr[k]); nf=0; } }
        else if(*pt&PTE_LAZY)*pt=0; }
    if(nf){ smp_tlb_sync(p->cr3); for(u64 k=0;k<nf;k++)pframe_free(fr[k]); }
}
static u64 lx_find_gap(u64 cr3,u64 pages){
    u64 need=pages<<12, v=0x40004000000ULL, start=v, run=0;
    for(int guard=0;v+need<=0x48000000000ULL&&guard<4000000;guard++){
        { int ri=lx_res_find(cr3,v); if(ri>=0){ v=g_res[ri].e; run=0; start=v; continue; } }
        u64* e=vm_walk(cr3,v,0);
        if(!e){ u64 nv=(v+0x200000ULL)&~0x1FFFFFULL; u64 rn=lx_res_next(cr3,v); if(rn<nv){ run+=rn-v; v=rn; if(run>=need)return start; continue; } run+=nv-v; v=nv; }
        else if(vm_pte_used(*e)){ v+=0x1000ULL; run=0; start=v; }
        else { v+=0x1000ULL; run+=0x1000ULL; }
        if(run>=need)return start; }
    return 0;
}
struct lx_mapname { u64 cr3,b,e; char nm[40]; };
struct lx_mapname g_mapn[2048]; int g_mapn_i;
static void lx_note_map(u64 cr3,u64 b,u64 e,const char* nm){ if(g_mapn_i){ struct lx_mapname* l=&g_mapn[(g_mapn_i-1)%2048]; if(l->cr3==cr3&&l->e==b){ int same=1; for(int k=0;k<39&&(nm[k]||l->nm[k]);k++)if(nm[k]!=l->nm[k]){ same=0; break; } if(same){ l->e=e; return; } } } struct lx_mapname* m=&g_mapn[g_mapn_i%2048]; g_mapn_i++; m->cr3=cr3; m->b=b; m->e=e; int k=0; for(;nm[k]&&k<39;k++)m->nm[k]=nm[k]; m->nm[k]=0; }
void lx_mapn_dead(u64 cr3){ lx_res_drop(cr3); lx_fm_dead(cr3); lx_sha_dead(cr3); cr3&=~0xFFFULL; for(int i=0;i<2048;i++) if((g_mapn[i].cr3&~0xFFFULL)==cr3)g_mapn[i].cr3=0; }
static char* lx_hx(char* o,u64 v,int mn){ char t[20]; int n=0; do{ t[n++]="0123456789abcdef"[v&15]; v>>=4; }while(v||n<mn); while(n)*o++=t[--n]; return o; }
struct lx_me { u64 b,e; const char* nm; const char* pm; };
static int lx_fdpath(const char* pa){
    const char* q;
    if(pa[0]=='/'&&pa[1]=='d'&&pa[2]=='e'&&pa[3]=='v'&&pa[4]=='/'&&pa[5]=='f'&&pa[6]=='d'&&pa[7]=='/')q=pa+8;
    else { const char* s="/proc/"; for(int i=0;s[i];i++) if(pa[i]!=s[i])return -1; q=pa+6; struct jproc* L=jc_lead(&g_jp[g_jcur]);
        if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'&&q[4]=='/')q+=4;
        else if(q[0]=='t'&&q[1]=='h'&&q[2]=='r'&&q[3]=='e'&&q[4]=='a'&&q[5]=='d'&&q[6]=='-'&&q[7]=='s'&&q[8]=='e'&&q[9]=='l'&&q[10]=='f'&&q[11]=='/')q+=11;
        else { int pid=0,d=0; while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; } if(!d||(pid!=L->pid&&pid!=g_jp[g_jcur].pid))return -1; }
        if(!(q[0]=='/'&&q[1]=='f'&&q[2]=='d'&&q[3]=='/'))return -1; q+=4; }
    if(*q<'0'||*q>'9')return -1; int n=0; while(*q>='0'&&*q<='9'){ n=n*10+(*q-'0'); q++; if(n>100000)return -1; } if(*q)return -1; return n; }
static long lx_proc_fd(const char* pa,int flags){
    int n=lx_fdpath(pa); if(n<0)return -9999;
    int* t=fdt(); if(n>=FDMAX||t[n]<0)return -LX_ENOENT;
    int ov=t[n]; struct vfile* v=&g_vf[ov]; int cloexec=(flags&0x80000)!=0;
    if(v->dev==LX_MFD||v->dev==0||v->dev==100||v->dev==LX_DIRDEV||v->dev==LX_SRDIR){
        if(v->dev==0&&v->fs_idx>=0&&(flags&3)!=0){ if(!ux_perm_ok(v->fs_idx,2))return -LX_EACCES; }
        if((v->dev==100||v->dev==LX_SRDIR||v->dev==LX_DIRDEV)&&(flags&3)!=0)return -LX_EACCES;
        int vi=jc_newvf(v->dev,v->fs_idx,flags&~(0x80000|0x40|0x80|0x200)); if(vi<0)return -LX_EMFILE;
        g_vf[vi].vfs=v->vfs; for(int k=0;k<128;k++)g_vf[vi].vpath[k]=v->vpath[k];
        if(v->dev==LX_MFD)g_mfd[v->fs_idx].ref++;
        if(v->dev==LX_MFD&&(flags&0x200)&&(flags&3)!=0){ long e=mfd_truncate(v->fs_idx,0); if(e){ vf_unref(vi); return e; } }
        int fd=lx_fd_install(vi,cloexec,0); if(fd<0)vf_unref(vi); return fd; }
    int fd=lx_fd_install(ov,cloexec,0); if(fd>=0)vf_ref(ov); return fd; }
static int lx_fdlink(int n,char* out){
    int* t=fdt(); if(n<0||n>=FDMAX||t[n]<0)return -LX_ENOENT; struct vfile* v=&g_vf[t[n]]; char* o=out;
    const char* pre=0; if(v->dev==LX_MFD)pre="/memfd:nvx (deleted)"; else if(v->dev==14||v->dev==LX_AFU)pre="socket:["; else if(v->dev==5)pre="pipe:["; else if(v->dev==LX_EVFD||v->dev==LX_TMFD)pre=v->dev==LX_EVFD?"anon_inode:[eventfd]":"anon_inode:[timerfd]";
    if(pre){ while(*pre)*o++=*pre++; if(o[-1]=='['){ int x=t[n]+1000; char b[8]; int k=0; while(x){ b[k++]=(char)('0'+x%10); x/=10; } while(k)*o++=b[--k]; *o++=']'; } return (int)(o-out); }
    if(v->vpath[0]){ for(int k=0;v->vpath[k]&&k<127;k++)*o++=v->vpath[k]; return (int)(o-out); }
    if(v->dev==0&&v->fs_idx>=0){ const char* z=FS[v->fs_idx].name; if(*z!='/')*o++='/'; while(*z&&o-out<120)*o++=*z++; return (int)(o-out); }
    const char* z="/dev/null"; if(n<=2)z="/dev/tty"; while(*z)*o++=*z++; return (int)(o-out); }
static long lx_proc_maps(const char* pa,int flags){
    const char* s="/proc/"; for(int i=0;s[i];i++) if(pa[i]!=s[i])return -9999;
    const char* q=pa+6; struct jproc* P=jc_lead(&g_jp[g_jcur]);
    if(q[0]=='s'&&q[1]=='e'&&q[2]=='l'&&q[3]=='f'&&q[4]=='/')q+=5;
    else if(q[0]=='t'&&q[1]=='h'&&q[2]=='r'&&q[3]=='e'&&q[4]=='a'&&q[5]=='d'&&q[6]=='-'&&q[7]=='s'&&q[8]=='e'&&q[9]=='l'&&q[10]=='f'&&q[11]=='/')q+=12;
    else { if(*q<'0'||*q>'9')return -9999; int pid=0; while(*q>='0'&&*q<='9')pid=pid*10+(*q++-'0'); if(*q!='/')return -9999; q++; if(pid!=P->pid&&pid!=g_jp[g_jcur].pid)return -9999; }
    if(!(q[0]=='m'&&q[1]=='a'&&q[2]=='p'&&q[3]=='s'&&!q[4]))return -9999;
    if((flags&3)!=0)return -LX_EACCES;
    static struct lx_me me[400]; int n=0; u64 cr3=P->cr3&~0xFFFULL;
    me[n].b=0x40000001000ULL; me[n].e=0x40000101000ULL; me[n].nm="[stack]"; me[n].pm="rw-p"; n++;
    me[n].b=0x40000400000ULL; me[n].e=0x40002000000ULL; me[n].nm=P->exe[0]?P->exe:"/proc/self/exe"; me[n].pm="r-xp"; n++;
    if(P->brk>0x40002000000ULL){ me[n].b=0x40002000000ULL; me[n].e=(P->brk+0xFFFULL)&~0xFFFULL; me[n].nm="[heap]"; me[n].pm="rw-p"; n++; }
    me[n].b=0x40030000000ULL; me[n].e=0x40030040000ULL; me[n].nm="/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"; me[n].pm="r-xp"; n++;
    int cnt=g_mapn_i<2048?g_mapn_i:2048;
    for(int j=0;j<cnt&&n<400;j++){ struct lx_mapname* m=&g_mapn[(g_mapn_i-cnt+j)%2048]; if((m->cr3&~0xFFFULL)!=cr3||!m->e)continue; me[n].b=m->b; me[n].e=m->e; me[n].nm=m->nm; me[n].pm="r-xp"; n++; }
    for(int a=1;a<n;a++){ struct lx_me t=me[a]; int b=a-1; while(b>=0&&me[b].b>t.b){ me[b+1]=me[b]; b--; } me[b+1]=t; }
    char* out=(char*)kmalloc(65536); if(!out)return -LX_ENOMEM; char* o=out; u64 last=0;
    for(int a=0;a<n&&o-out<65000;a++){ if(me[a].b<last)continue; last=me[a].e;
        o=lx_hx(o,me[a].b,8); *o++='-'; o=lx_hx(o,me[a].e,8); *o++=' '; for(int k=0;k<4;k++)*o++=me[a].pm[k];
        const char* mid=" 00000000 00:00 0"; while(*mid)*o++=*mid++; for(int k=0;k<7;k++)*o++=' ';
        const char* z=me[a].nm; while(*z&&o-out<65400)*o++=*z++; *o++='\n'; }
    u64 len=(u64)(o-out);
    int mi=mfd_new(); if(mi<0){ kfree(out); return -LX_EMFILE; }
    if(mfd_truncate(mi,len?len:1)){ mfd_close(mi); kfree(out); return -LX_ENOMEM; }
    g_mfd[mi].size=len;
    for(u64 k=0;k<len;k++)((u8*)g_mfd[mi].frames[k>>12])[k&4095]=(u8)out[k];
    kfree(out);
    int vi=jc_newvf(LX_MFD,mi,0); if(vi<0){ mfd_close(mi); return -LX_EMFILE; }
    int fd=lx_fd_install(vi,(flags&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
static long lx_madvise(u64 a,u64 len,int adv){
    if(a&0xFFF)return -LX_EINVAL; if(!len)return 0; u64 e=(a+len+0xFFFULL)&~0xFFFULL; if(!jc_uok(a,e-a))return -LX_ENOMEM;
    if(adv!=4&&adv!=9)return 0;
    extern u32 pframe_refcount(u64 pa); struct jproc* p=&g_jp[g_jcur];
    for(u64 v=a;v<e;v+=0x1000ULL){ u64* pt=vm_walk(p->cr3,v,0); if(!pt||!(*pt&1))continue; u64 pa=*pt&PTE_ADDR; if((*pt&PTE_SHR)||pframe_refcount(pa)>1)continue;
        lx_unmap_range(v,v+0x1000ULL); vm_map_lazy(p->cr3,v); }
    return 0; }
static long lx_mmap(u64 addr,u64 len,int prot,int flags,int fd,u64 off){
    struct jproc* p=&g_jp[g_jcur]; (void)prot;
    if(!len||(off&0xFFF))return -LX_EINVAL;
    if(len>((flags&0x20)?0x10000000000ULL:0x40000000ULL))return -LX_ENOMEM;
    u64 pages=(len+0xFFFULL)>>12;
    int anon=(flags&0x20)!=0;
    struct vfile* v=0;
    if(!anon){ v=lx_vf(fd); if(!v)return -LX_EBADF; if(!((v->dev==0&&v->fs_idx>=0)||(v->dev==100&&v->vfs)||v->dev==LX_MFD))return -LX_ENODEV; }
    if(!anon&&v->dev==LX_MFD&&(g_mfd[v->fs_idx].seals&0x18)&&(flags&3)==1&&(prot&2))return -LX_EPERM;
    if(!anon&&v->dev!=LX_MFD&&pages>pframe_free_count())return -LX_ENOMEM;
    if(anon&&pages<=65536&&(pages>>9)+64>pframe_free_count())return -LX_ENOMEM;
    u64 base;
    if(flags&0x10){ if(addr&0xFFF)return -LX_EINVAL; if(!jc_uok(addr,pages<<12))return -LX_ENOMEM; base=addr;
        if(base+(pages<<12)>USTK_V-(u64)(JS_STK_PAGES-1)*0x1000ULL&&base<USTK_V+0x1000ULL)return -LX_EINVAL;
        lx_unmap_range(base,base+(pages<<12)); }
    else { base=lx_find_gap(p->cr3,pages+1); if(!base||!jc_uok(base,(pages+1)<<12))return -LX_ENOMEM;
        if(base+((pages+1)<<12)>g_mmap_next)g_mmap_next=base+((pages+1)<<12); }
    if(v&&v->dev==LX_MFD){ struct memfd* m=&g_mfd[v->fs_idx]; u64 pbase=off>>12;
        for(u64 i=0;i<pages;i++){ u64 va=base+(i<<12);
            if((long)(pbase+i)<m->nframes&&m->frames[pbase+i]){ u64 f=m->frames[pbase+i]; pframe_ref(f); if(vm_map(p->cr3,va,f,((flags&3)==1)?(0x7ULL|PTE_SHR):(0x5ULL|PTE_COW))){ pframe_free(f); lx_unmap_range(base,va); return -LX_ENOMEM; } }
            else if(vm_map_lazy(p->cr3,va)){ lx_unmap_range(base,va); return -LX_ENOMEM; } }
        return (long)base; }
    if(!v&&(flags&3)!=1&&pages>65536){ if(lx_res_add(p->cr3,base,base+(pages<<12))==0)return (long)base; }
    for(u64 i=0;i<pages;i++){
        if(!v&&!((flags&3)==1&&pages<=65536)){ if(vm_map_lazy(p->cr3,base+(i<<12))){ lx_unmap_range(base,base+(i<<12)); return -LX_ENOMEM; } continue; }
        u64 f=pframe_alloc(); if(!f||vm_map(p->cr3,base+(i<<12),f,((flags&3)==1)?(0x7ULL|PTE_SHR):0x7ULL)){ if(f)pframe_free(f); lx_unmap_range(base,base+(i<<12)); return -LX_ENOMEM; }
        if(v&&v->dev==100){ u64 fo=off+(i<<12); int got=v->vfs->read(v->vpath,(char*)f,(int)fo,4096); if(got<0)got=0; u8* d=(u8*)f; for(int b=got;b<4096;b++)d[b]=0; }
        else if(v){ int k=v->fs_idx; u64 fo=off+(i<<12); u8* d=(u8*)f; const char* sd=fsd(k); for(int b=0;b<4096;b++){ u64 q=fo+(u64)b; d[b]=(q<(u64)FS[k].len)?(u8)sd[q]:0; } } }
    if(v&&v->dev==0&&v->fs_idx>=0)lx_note_map(p->cr3,base,base+(pages<<12),FS[v->fs_idx].name);
    if(v&&v->dev==0&&v->fs_idx>=0&&(flags&3)==1)lx_fm_add(p->cr3,base,off,v->fs_idx,pages);
    if(v&&v->dev==100&&v->vfs){ const char* q=v->vpath; for(int z=0;v->vpath[z];z++) if(v->vpath[z]=='/')q=v->vpath+z+1; lx_note_map(p->cr3,base,base+(pages<<12),q); }
    if(v&&v->dev==0&&(flags&1)&&off==0){ const char* nm=FS[v->fs_idx].name; const char* x="Xvfb_screen"; int m=1; for(int q=0;x[q];q++) if(nm[q]!=x[q]){ m=0; break; }
        if(m&&pages<=65536&&(prot&2)){ extern void xfb_register(u64* frames,int n); u64* fr=(u64*)kmalloc(sizeof(u64)*pages); if(fr){ for(u64 i=0;i<pages;i++){ u64* e=vm_walk(p->cr3,base+(i<<12),0); fr[i]=e?(*e&0x000FFFFFFFFFF000ULL):0; } xfb_register(fr,(int)pages); kfree(fr); if(prot&2){ extern void xfb_track(u64,u64,int); xfb_track(p->cr3,base,(int)pages); } } } }
    return (long)base;
}
static long lx_munmap(u64 a,u64 len){
    if((a&0xFFF)||!len)return -LX_EINVAL;
    u64 e=(a+len+0xFFFULL)&~0xFFFULL; if(!jc_uok(a,e-a))return -LX_EINVAL;
    if(a<USTK_V+0x1000ULL&&e>USTK_V-(u64)(JS_STK_PAGES-1)*0x1000ULL)return -LX_EINVAL;
    lx_unmap_range(a,e); return 0;
}
static long lx_brk(u64 want){
    struct jproc* p=jc_lead(&g_jp[g_jcur]);
    u64 cur=p->lxbrk?p->lxbrk:g_ubrk;
    if(want<0x40002000000ULL||want>=0x40004000000ULL)return (long)cur;
    u64 top=(want+0xFFFULL)&~0xFFFULL;
    if(top>g_ubrk){ u64 need=(top-g_ubrk)>>12; if(need>pframe_free_count())return (long)cur;
        for(u64 v=g_ubrk;v<top;v+=0x1000ULL){ u64* e=vm_walk(p->cr3,v,0); if(e&&vm_pte_used(*e))return (long)cur; }
        u64 start=g_ubrk; while(g_ubrk<top){ if(vm_map_lazy(p->cr3,g_ubrk)){ lx_unmap_range(start,g_ubrk); g_ubrk=start; return (long)cur; } g_ubrk+=0x1000ULL; } }
    else if(top<g_ubrk){ lx_unmap_range(top,g_ubrk); g_ubrk=top; }
    p->lxbrk=want; return (long)want;
}

static long lx_sigaction(int s,u64 act,u64 oact,u64 sz){
    struct jproc* p=&g_jp[g_jcur];
    if(s<1||s>64||(sz&&sz!=8))return -LX_EINVAL;
    u64 na[4];
    if(act&&lx_get(na,act,32))return -LX_EFAULT;
    if(s>=32){ if(oact){ u64 z[4]={0,0,0,0}; if(lx_put(oact,z,32))return -LX_EFAULT; } return 0; }
    if(act&&(s==9||s==19))return -LX_EINVAL;
    if(oact){ u64 o[4]; o[0]=p->sighand[s]; o[1]=p->saflags[s]; o[2]=p->sigrest[s]; o[3]=lx_k2l(p->sigamask[s]); if(lx_put(oact,o,32))return -LX_EFAULT; }
    if(act){ p->sighand[s]=na[0]; p->saflags[s]=(u32)na[1]; p->sigrest[s]=(na[1]&0x04000000ULL)?na[2]:0; p->sigamask[s]=lx_l2k(na[3])&~((1u<<9)|(1u<<19));
        int ign=(na[0]==1||(na[0]==0&&sig_default(s)==0)); if(ign)p->sigpend&=~(1u<<s);
        for(int j=0;j<JMAX;j++){ struct jproc* q=&g_jp[j]; if(q==p||q->state==S_FREE||q->tg!=p->tg)continue;
            q->sighand[s]=p->sighand[s]; q->saflags[s]=p->saflags[s]; q->sigrest[s]=p->sigrest[s]; q->sigamask[s]=p->sigamask[s]; if(ign)q->sigpend&=~(1u<<s); } }
    return 0;
}
static long lx_sigprocmask(int how,u64 set,u64 oset,u64 sz){
    struct jproc* p=&g_jp[g_jcur];
    if(sz!=8)return -LX_EINVAL;
    u64 ns=0; if(set&&lx_get(&ns,set,8))return -LX_EFAULT;
    if(oset){ u64 o=lx_k2l(p->sigmask); if(lx_put(oset,&o,8))return -LX_EFAULT; }
    if(set){ u32 k=lx_l2k(ns); if(how==0)p->sigmask|=k; else if(how==1)p->sigmask&=~k; else if(how==2)p->sigmask=k; else return -LX_EINVAL;
        p->sigmask&=~((1u<<9)|(1u<<19)); }
    return 0;
}
static void lx_sigreturn(struct sysregs* r) __attribute__((noreturn));
static void lx_sigreturn(struct sysregs* r){
    struct jproc* p=&g_jp[g_jcur]; u64 f=((u64*)r)[18];
    if(!lx_ok(f,sizeof(regs_pt)+8)){ jc_finish(g_jcur,11); jc_leave(); }
    regs_pt c=*(regs_pt*)f; u32 m=(u32)*(u64*)(f+sizeof(regs_pt));
    if(!jc_uok(c.rip,1)){ jc_finish(g_jcur,11); jc_leave(); }
    c.cs=0x1B; c.ss=0x23; c.rflags=(c.rflags&0xCD5)|0x202; p->ctx=c; p->sigmask=m&~((1u<<9)|(1u<<19)); jc_leave();
}

static long lx_clone(struct sysregs* r,u64 flags,u64 stack,u64 ptid,u64 ctid,u64 tls){
    if((flags&0x80000ULL)&&tls>=0x800000000000ULL)return -LX_EPERM;
    if(stack&&!jc_uok(stack-8,8))return -LX_EINVAL;
    if(flags&0x10000ULL){
        if((flags&0x900ULL)!=0x900ULL){ lx_gap(7,flags); return -LX_EINVAL; }
        int ci=jc_clone_thread(r,stack,tls,(flags&0x80000ULL)!=0); if(ci<0)return -LX_EAGAIN;
        struct jproc* c=&g_jp[ci]; int tid=c->pid;
        if(flags&0x200000ULL)c->ctid=ctid;
        if((flags&0x1000000ULL)&&ctid)lx_put(ctid,&tid,4);
        if((flags&0x100000ULL)&&ptid)lx_put(ptid,&tid,4);
        return tid; }
    if(flags&0x800ULL&&!(flags&0x100ULL))return -LX_EINVAL;
    int pid=jc_fork(r); if(pid<0)return -LX_EAGAIN;
    int ci=jc_idx(pid); struct jproc* c=&g_jp[ci];
    if(stack)c->ctx.rsp=stack;
    if(flags&0x80000ULL)c->fsbase=tls;
    c->pollwake=0; c->susp=0;
    if(flags&0x200000ULL)c->ctid=ctid;
    if((flags&0x100000ULL)&&ptid)lx_put(ptid,&pid,4);
    return pid;
}
static u64 lx_now_ns(void);
static int lx_futex_op(u32* uaddr,int op,u32 arg,u32* oldv){
    if(op&8){ op&=7; arg=1u<<(arg&31); }
    u32 old=__atomic_load_n(uaddr,__ATOMIC_SEQ_CST), nv;
    for(;;){
        switch(op){ case 0: nv=arg; break; case 1: nv=old+arg; break; case 2: nv=old|arg; break; case 3: nv=old&~arg; break; case 4: nv=old^arg; break; default: return -LX_ENOSYS; }
        if(__atomic_compare_exchange_n(uaddr,&old,nv,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))break; }
    *oldv=old; return 0;
}
#define FX_SHARED_TG (-77)
static u64 lx_fx_key(struct jproc* p,int op,u64 ua,int* tg){
    *tg=p->tg; if(op&128)return ua;
    u64* e=vm_walk(p->cr3&~0xFFFULL,ua&~0xFFFULL,0); if(!e||!(*e&1)){ u32 t; if(lx_get(&t,ua,4))return ua; e=vm_walk(p->cr3&~0xFFFULL,ua&~0xFFFULL,0); if(!e||!(*e&1))return ua; }
    *tg=FX_SHARED_TG; return (*e&PTE_ADDR)|(ua&0xFFFULL); }
static long lx_futex(struct sysregs* r,u64 ua,int op,u32 val,u64 ts,u64 ua2,u32 val3){
    struct jproc* p=&g_jp[g_jcur]; int cmd=op&127; int tg=p->tg;
    if(ua&3)return -LX_EINVAL;
    u64 ka=lx_fx_key(p,op,ua,&tg); int tg2=tg; u64 ka2=(cmd==3||cmd==4||cmd==5)&&!(ua2&3)?lx_fx_key(p,op,ua2,&tg2):ua2;
    if(cmd==0||cmd==9){
        u32 bits=(cmd==9)?val3:0xFFFFFFFFu; if(!bits)return -LX_EINVAL;
        if(p->fx_wait){
            if(p->fx_woken){ p->fx_wait=0; p->fx_woken=0; return 0; }
            if(p->fx_dead&&g_ticks>=p->fx_dead){ p->fx_wait=0; return -110; }
            if(jc_deliverable(p)){ p->fx_wait=0; return -LX_EINTR; }
            jc_block(r,S_WAIT,p->fx_dead?p->fx_dead:g_ticks+0x7FFFFFFFFFFFULL); }
        u32 cur; if(lx_get(&cur,ua,4))return -LX_EFAULT;
        if(cur!=val)return -LX_EAGAIN;
        u64 dead=0;
        if(ts){ u64 t[2]; if(lx_get(t,ts,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL;
            if(cmd!=9&&t[0]>100000000ULL)t[0]=100000000ULL;
            if(t[0]>0x3FFFFFFFFULL)t[0]=0x3FFFFFFFFULL;
            u64 ns=t[0]*1000000000ULL+t[1];
            if(cmd==9){ u64 now=(op&256)?lx_now_ns():hr_ns(); ns=ns>now?ns-now:0; if(ns>100000000ULL*1000000000ULL)ns=100000000ULL*1000000000ULL; }
            if(!ns)return -110;
            dead=g_ticks+(ns+9999999ULL)/10000000ULL; }
        p->fx_wait=1; p->fx_woken=0; p->fx_tg=tg; p->fx_addr=ka; p->fx_bits=bits; p->fx_dead=dead;
        jc_block(r,S_WAIT,dead?dead:g_ticks+0x7FFFFFFFFFFFULL); }
    if(cmd==1||cmd==10){ if(cmd==10&&!val3)return -LX_EINVAL; return jc_futex_wake(tg,ka,(int)(val>0x7FFFFFFFu?0x7FFFFFFF:val),cmd==10?val3:0xFFFFFFFFu); }
    if(cmd==3||cmd==4){
        if(ua2&3)return -LX_EINVAL;
        if(cmd==4){ u32 cur; if(lx_get(&cur,ua,4))return -LX_EFAULT; if(cur!=val3)return -LX_EAGAIN; }
        return jc_futex_requeue(tg,ka,ka2,(int)(val>0x7FFFFFFFu?0x7FFFFFFF:val),(int)((u32)ts>0x7FFFFFFFu?0x7FFFFFFF:(u32)ts)); }
    if(cmd==5){
        if(ua2&3)return -LX_EINVAL; if(!jc_umapped_w(ua2,4))return -LX_EFAULT;
        int opk=(int)((val3>>28)&0xF), cmp=(int)((val3>>24)&0xF); int oparg=(int)((val3>>12)&0xFFF), cmparg=(int)(val3&0xFFF);
        if(oparg&0x800)oparg|=~0xFFF; if(cmparg&0x800)cmparg|=~0xFFF;
        u32 old; int e=lx_futex_op((u32*)ua2,opk,(u32)oparg,&old); if(e)return e;
        int n=jc_futex_wake(tg,ka,(int)(val>0x7FFFFFFFu?0x7FFFFFFF:val),0xFFFFFFFFu);
        int o=(int)old, c=cmparg, hit;
        switch(cmp){ case 0: hit=o==c; break; case 1: hit=o!=c; break; case 2: hit=o<c; break; case 3: hit=o<=c; break; case 4: hit=o>c; break; case 5: hit=o>=c; break; default: return -LX_ENOSYS; }
        if(hit)n+=jc_futex_wake(tg2,ka2,(int)((u32)ts>0x7FFFFFFFu?0x7FFFFFFF:(u32)ts),0xFFFFFFFFu);
        return n; }
    return -LX_ENOSYS;
}
static void lx_norm(char* p){ char out[LX_PATH]; int o=0, i=0;
    while(p[i]){ while(p[i]=='/')i++; if(!p[i])break; int st=i; while(p[i]&&p[i]!='/')i++; int L=i-st;
        if(L==1&&p[st]=='.')continue;
        if(L==2&&p[st]=='.'&&p[st+1]=='.'){ while(o>0&&out[o-1]!='/')o--; if(o>0)o--; continue; }
        if(o+1+L>=LX_PATH-1)break; out[o++]='/'; for(int k=0;k<L;k++)out[o++]=p[st+k]; }
    if(!o)out[o++]='/'; out[o]=0; for(int k=0;k<=o;k++)p[k]=out[k]; }
static void lx_resolve_exe(const char* ab,char* out,int cap){ char cur[LX_PATH]; int k=0; for(;ab[k]&&k<LX_PATH-1;k++)cur[k]=ab[k]; cur[k]=0; lx_norm(cur);
    for(int hop=0;hop<8;hop++){ char t[LX_PATH]; int n=ux_readlink(cur,t,LX_PATH-1); if(n<=0)break; t[n]=0;
        char nx[LX_PATH]; int o=0; if(t[0]!='/'){ int d=0; for(int q=0;cur[q];q++) if(cur[q]=='/')d=q; for(int q=0;q<=d&&o<LX_PATH-1;q++)nx[o++]=cur[q]; }
        for(int q=0;t[q]&&o<LX_PATH-1;q++)nx[o++]=t[q]; nx[o]=0; lx_norm(nx); int q=0; for(;nx[q]&&q<LX_PATH-1;q++)cur[q]=nx[q]; cur[q]=0; }
    int L=0; while(cur[L]&&L<cap-1){ out[L]=cur[L]; L++; } out[L]=0; }
static int lx_proc_exe(const char* pa,u64 ub,u64 cap){ const char* s="/proc/"; for(int i=0;s[i];i++) if(pa[i]!=s[i])return -9999;
    const char* q=pa+6; struct jproc* L=jc_lead(&g_jp[g_jcur]);
    const char* sf="self"; const char* ts="thread-self"; int m=1; for(int i=0;sf[i];i++) if(q[i]!=sf[i]){ m=0; break; }
    if(m)q+=4; else { m=1; for(int i=0;ts[i];i++) if(q[i]!=ts[i]){ m=0; break; } if(m)q+=11; else { int pid=0,d=0; while(*q>='0'&&*q<='9'){ pid=pid*10+(*q-'0'); q++; d=1; } if(!d||(pid!=L->pid&&pid!=g_jp[g_jcur].pid))return -9999; } }
    if(q[0]=='/'&&q[1]=='f'&&q[2]=='d'&&q[3]=='/'){ int n=lx_fdpath(pa); if(n<0)return -9999; char lk[160]; int L2=lx_fdlink(n,lk); if(L2<0)return L2; if((u64)L2>cap)L2=(int)cap; if(lx_put(ub,lk,(u64)L2))return -LX_EFAULT; return L2; }
    if(!(q[0]=='/'&&q[1]=='e'&&q[2]=='x'&&q[3]=='e'&&q[4]==0))return -9999;
    if(!L->exe[0])return -LX_ENOENT; int n=0; while(L->exe[n])n++; if((u64)n>cap)n=(int)cap; if(lx_put(ub,L->exe,(u64)n))return -LX_EFAULT; return n; }
void lx_set_exe(char* out,const char* args){ char w[LX_PATH]; int k=0; while(args[k]==' ')args++; while(args[k]&&args[k]!=' '&&k<LX_PATH-1){ w[k]=args[k]; k++; } w[k]=0; if(!k)return;
    char ab[LX_PATH]; ab[0]=0;
    if(w[0]=='/'){ for(int q=0;q<=k;q++)ab[q]=w[q]; }
    else { int slash=0; for(int q=0;w[q];q++) if(w[q]=='/')slash=1; if(slash)return;
        static const char* const pres[]={"/opt/gnu/bin/","/usr/local/bin/","/usr/bin/","/bin/","/usr/sbin/","/sbin/",0};
        for(int pi=0;pres[pi]&&!ab[0];pi++){ char gp[LX_PATH]; int o=0; const char* pre=pres[pi]; while(*pre)gp[o++]=*pre++; for(int q=0;w[q]&&o<LX_PATH-1;q++)gp[o++]=w[q]; gp[o]=0;
            u32 l=0; int sx=0; if(sysroot_get(gp,&l,&sx)||lx_exists(gp)){ for(int q=0;q<=o;q++)ab[q]=gp[q]; } } }
    if(ab[0])lx_resolve_exe(ab,out,128); }
static long lx_execve(struct sysregs* r,u64 upath,u64 uargv,u64 uenv){
    char path[LX_PATH]; int n=lx_str(upath,path,LX_PATH); if(n<0)return n; if(!n)return -LX_ENOENT;
    if(n>63)return -LX_ENAMETOOLONG;
    int used=0; g_xa_n=0;
    if(uargv) for(int i=0;;i++){ u64 sp; if(lx_get(&sp,uargv+(u64)i*8,8))return -LX_EFAULT; if(!sp)break;
        if(g_xa_n>=XA_MAX)return -LX_E2BIG; int L=lx_str(sp,g_xa_buf+used,XA_CAP-used); if(L<0)return L==-LX_ENAMETOOLONG?-LX_E2BIG:L;
        g_xa_off[g_xa_n++]=used; used+=L+1; }
    if(!g_xa_n){ for(int i=0;i<=n;i++)g_xa_buf[i]=path[i]; g_xa_off[0]=0; g_xa_n=1; }
    used=0; g_xe_n=0;
    if(uenv) for(int i=0;;i++){ u64 sp; if(lx_get(&sp,uenv+(u64)i*8,8))return -LX_EFAULT; if(!sp)break;
        if(g_xe_n>=XA_MAX)return -LX_E2BIG; int L=lx_str(sp,g_xe_buf+used,XA_CAP-used); if(L<0)return L==-LX_ENAMETOOLONG?-LX_E2BIG:L;
        g_xe_off[g_xe_n++]=used; used+=L+1; }
    for(int i=0;i<n;i++) if(path[i]==' ')return -LX_ENOENT;
    for(int hop=0;hop<3;hop++){ const u8* hd=0; u32 hl=0; hd=sysroot_get(path,&hl,0);
        if(!hd){ int isd=0; int f=ux_lookup(path,&isd,1); if(f>=0&&!isd){ hd=(const u8*)fsd(f); hl=(u32)FS[f].len; } }
        if(!hd||hl<3||hd[0]!='#'||hd[1]!='!')break;
        char ip[64], ia[64]; int a=2, il=0, al=0; while(a<(int)hl&&a<200&&(hd[a]==' '||hd[a]=='\t'))a++;
        while(a<(int)hl&&a<200&&hd[a]!=' '&&hd[a]!='\t'&&hd[a]!='\n'&&hd[a]!='\r'&&il<63)ip[il++]=(char)hd[a++]; ip[il]=0;
        while(a<(int)hl&&a<200&&(hd[a]==' '||hd[a]=='\t'))a++;
        while(a<(int)hl&&a<200&&hd[a]!='\n'&&hd[a]!='\r'&&al<63)ia[al++]=(char)hd[a++]; while(al>0&&(ia[al-1]==' '||ia[al-1]=='\t'))al--; ia[al]=0;
        if(!il)return -LX_ENOEXEC;
        static char nb[XA_CAP]; static int no[XA_MAX]; int nu=0, nn=0;
        const char* pre[3]={ip,al?ia:0,path};
        for(int q=0;q<3;q++){ if(!pre[q])continue; if(nn>=XA_MAX)return -LX_E2BIG; int L=0; while(pre[q][L])L++; if(nu+L+1>XA_CAP)return -LX_E2BIG; no[nn++]=nu; for(int z=0;z<=L;z++)nb[nu+z]=pre[q][z]; nu+=L+1; }
        for(int q=1;q<g_xa_n;q++){ const char* sa=g_xa_buf+g_xa_off[q]; int L=0; while(sa[L])L++; if(nn>=XA_MAX||nu+L+1>XA_CAP)return -LX_E2BIG; no[nn++]=nu; for(int z=0;z<=L;z++)nb[nu+z]=sa[z]; nu+=L+1; }
        for(int q=0;q<nu;q++)g_xa_buf[q]=nb[q]; for(int q=0;q<nn;q++)g_xa_off[q]=no[q]; g_xa_n=nn;
        for(int q=0;q<=il;q++)path[q]=ip[q]; n=il; }
    u32 L0; if(!jc_find_prog(path,&L0)){ if(lx_isdir(path))return -LX_EACCES; return lx_exists(path)?-LX_EACCES:-LX_ENOENT; }
    { char ab[LX_PATH]; ab[0]=0; if(path[0]=='/'){ int k=0; for(;path[k]&&k<LX_PATH-1;k++)ab[k]=path[k]; ab[k]=0; } else if(lx_path(LX_AT_FDCWD,upath,ab,0)<0)ab[0]=0;
      if(ab[0])lx_resolve_exe(ab,jc_lead(&g_jp[g_jcur])->exe,128); }
    g_xa_set=1; g_xe_set=1;
    jc_exec(r,path);
    g_xa_set=0; g_xe_set=0;
    return -LX_ENOEXEC;
}
static long lx_wait4(struct sysregs* r,int pid,u64 st,int opts,u64 ru){
    if(st&&!lx_okw(st,4))return -LX_EFAULT;
    if(ru){ if(!lx_okw(ru,144))return -LX_EFAULT; lx_zero((void*)ru,144); }
    int v=jc_waitpid(r,pid,st,opts&0xB);
    return v<0?-LX_ECHILD:v;
}

static u64 g_lx_epoch0;
static u64 lx_now_ns(void){ if(!g_lx_epoch0){ u64 h=hr_ns(); g_lx_epoch0=k_realtime_ms()*1000000ULL-h; if(!g_lx_epoch0)g_lx_epoch0=1; } return g_lx_epoch0+hr_ns(); }
static long lx_clock(int clk,u64 ts){
    u64 ns;
    if(clk==0||clk==5||clk==8||clk==11){ ns=lx_now_ns(); }
    else if(clk==1||clk==4||clk==6||clk==7||clk==9||clk==2||clk==3)ns=hr_ns();
    else return -LX_EINVAL;
    u64 t[2]={ns/1000000000ULL,ns%1000000000ULL}; return lx_put(ts,t,16);
}
static long lx_sleep_ns(struct sysregs* r,u64 ns){
    u64 ticks=(ns+9999999ULL)/10000000ULL;
    if(!ticks){ r->rax=0; jc_yield_sys(r); }
    r->rax=0; jc_block(r,S_SLEEP,g_ticks+ticks);
}
static long lx_nanosleep(struct sysregs* r,u64 req){
    u64 t[2]; if(lx_get(t,req,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL;
    if(t[0]>100000000ULL)t[0]=100000000ULL;
    return lx_sleep_ns(r,t[0]*1000000000ULL+t[1]);
}
static long lx_clock_nanosleep(struct sysregs* r,int clk,int fl,u64 req){
    u64 t[2]; if(lx_get(t,req,16))return -LX_EFAULT; if((i64)t[0]<0||t[1]>=1000000000ULL)return -LX_EINVAL;
    if(t[0]>100000000ULL)t[0]=100000000ULL;
    u64 want=t[0]*1000000000ULL+t[1];
    if(fl&1){ u64 now=(clk==0)?lx_now_ns():hr_ns(); if(want<=now)return 0; want-=now; }
    return lx_sleep_ns(r,want);
}

static void lx_cpy(char* d,const char* s,int cap){ int i=0; for(;s[i]&&i<cap-1;i++)d[i]=s[i]; d[i]=0; }
static long lx_uname(u64 buf){
    char u[390]; lx_zero(u,390);
    lx_cpy(u,"NoovexOS",65); lx_cpy(u+65,g_hostname,65); lx_cpy(u+130,"9.83",65); lx_cpy(u+195,"NoovexOS 64 v9.83",65); lx_cpy(u+260,"x86_64",65); lx_cpy(u+325,"(none)",65);
    return lx_put(buf,u,390);
}
static long lx_rlimit(int res,u64 out){
    if(!out)return 0;
    u64 v[2]={~0ULL,~0ULL};
    if(res==3){ v[0]=(u64)JS_STK_PAGES*4096ULL; v[1]=v[0]; }
    else if(res==7){ v[0]=FDMAX; v[1]=FDMAX; }
    else if(res<0||res>15)return -LX_EINVAL;
    return lx_put(out,v,16);
}
static long lx_access(const char* p,int mode){
    u32 o[10]; if(ux_stat2(p,o,1))return lx_noent(p);
    if(mode==0||g_uid==0){ if((mode&1)&&!(o[0]&0111)&&!o[9])return -LX_EACCES; return 0; }
    u32 bits=(o[1]==(u32)g_uid)?(o[0]>>6)&7:(o[0]&7);
    if((mode&4)&&!(bits&4))return -LX_EACCES; if((mode&2)&&!(bits&2))return -LX_EACCES; if((mode&1)&&!(bits&1))return -LX_EACCES;
    return 0;
}
static long lx_mkdir(const char* p,int mode){
    if(lx_exists(p))return -LX_EEXIST;
    if(lx_slen(p)>200)return -LX_ENAMETOOLONG;
    { int L=lx_slen(p), c=0; while(L>0&&p[L-1]!='/'){ L--; c++; } if(c>63)return -LX_ENAMETOOLONG; }
    int d; char b[64]; if(k_split(p,&d,b)<0)return lx_noent(p); if(!b[0])return -LX_EEXIST;
    if(b[0]=='.'&&(!b[1]||(b[1]=='.'&&!b[2])))return -LX_EEXIST;
    int nd=dt_mkdir_at(d,b); if(nd<0)return -LX_ENOSPC;
    dt_setmode(nd,0040000u|((unsigned)mode&0777u&~(unsigned)g_umask)); DT[nd].uid=g_uid; DT[nd].gid=g_gid; dt_save(); return 0;
}
static long lx_rmdir(const char* p){
    int d=dt_resolve_dir(p); if(d<0)return lx_exists(p)?-LX_ENOTDIR:-LX_ENOENT;
    if(d==0)return -LX_EBUSY;
    if(!perm_dir(d))return -LX_EACCES;
    if(dt_busy(d))return -LX_EBUSY;
    if(k_rmdir(p))return k_is_ancestor(d,g_dcwd)?-LX_EBUSY:-LX_ENOTEMPTY;
    return 0;
}
static long lx_unlink(const char* p){
    if(lx_isdir(p))return -LX_EISDIR;
    u32 o[10]; if(ux_stat2(p,o,0))return -LX_ENOENT;
    return ux_unlink(p)?-LX_EACCES:0;
}
static long lx_rename(const char* a,const char* b){
    if(!lx_exists(a)){ int isd; if(ux_lookup(a,&isd,0)<0)return -LX_ENOENT; }
    if(lx_isdir(b)&&!lx_isdir(a))return -LX_EISDIR;
    if(lx_isdir(a)&&lx_exists(b)&&!lx_isdir(b))return -LX_ENOTDIR;
    if(lx_isdir(a)&&lx_isdir(b)){ int db=dt_resolve_dir(b); int da=dt_resolve_dir(a); if(da==db)return 0;
        if(!perm_dir(da)||!perm_dir(db))return -LX_EACCES; if(dt_busy(db)||k_is_ancestor(db,g_dcwd))return -LX_EBUSY;
        for(int i=0;i<DN_MAX;i++) if(DT[i].used&&DT[i].parent==db)return -LX_ENOTEMPTY;
        for(int k=0;k<FS_N;k++) if(FS[k].used&&FS[k].dir==db)return -LX_ENOTEMPTY;
        if(k_is_ancestor(da,db))return -LX_EINVAL;
        DT[db].used=0; if(k_rename(a,b)){ DT[db].used=1; return -LX_EINVAL; } return 0; }
    else if(lx_isdir(b)){ return -LX_EISDIR; }
    if(lx_isdir(a)){ int da=dt_resolve_dir(a); if(!perm_dir(da))return -LX_EACCES; }
    else { int isd; int k=ux_lookup(a,&isd,0); if(k>=0&&!isd&&!ux_can_unlink(FS[k].dir,k))return -LX_EACCES; }
    return k_rename(a,b)?-LX_EINVAL:0;
}
static int lx_sockfd(int fd,struct vfile** vo){ struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF; if(v->dev!=14)return -88; *vo=v; return v->fs_idx; }
static long lx_sock_wait(struct sysregs* r,int n,int nb){
    if(n!=JS_BLOCK)return n;
    if(nb)return -LX_EAGAIN;
    if(jc_deliverable(&g_jp[g_jcur]))return -LX_EINTR;
    jc_block(r,S_WAIT,g_ticks+1);
}
static int lx_sa_in(u64 a,u64 len,u32* ip,u16* port){
    if(!a||len<2)return -LX_EINVAL; u8 sa[16]; lx_zero(sa,16); if(lx_get(sa,a,len<16?len:16))return -LX_EFAULT;
    u16 fam=(u16)(sa[0]|(sa[1]<<8));
    if(fam==0){ *ip=0; *port=0; return 0; }
    if(fam!=2)return -97; if(len<16)return -LX_EINVAL;
    *port=(u16)((sa[2]<<8)|sa[3]); *ip=((u32)sa[4]<<24)|((u32)sa[5]<<16)|((u32)sa[6]<<8)|sa[7]; return 0;
}
static int lx_sa_out(u64 a,u64 lenp,u32 ip,u16 port){
    if(!a||!lenp)return 0; u32 L; if(lx_get(&L,lenp,4))return -LX_EFAULT;
    u8 sa[16]; lx_zero(sa,16); sa[0]=2; sa[2]=(u8)(port>>8); sa[3]=(u8)port; sa[4]=(u8)(ip>>24); sa[5]=(u8)(ip>>16); sa[6]=(u8)(ip>>8); sa[7]=(u8)ip;
    if(lx_put(a,sa,L<16?L:16))return -LX_EFAULT; L=16; return lx_put(lenp,&L,4);
}
static int lx_un_key(u64 a,u64 len,u8* key,int* kl){
    if(!a||len<2)return -LX_EINVAL; if(len>110)len=110; u8 sa[112]; lx_zero(sa,112); if(lx_get(sa,a,len))return -LX_EFAULT;
    u16 fam=(u16)(sa[0]|(sa[1]<<8)); if(fam!=1)return -97;
    int pl=(int)len-2; if(pl<=0)return -LX_EINVAL; const u8* p=sa+2;
    int kn; if(p[0]==0)kn=pl; else { kn=0; while(kn<pl&&kn<108&&p[kn])kn++; }
    if(kn<=0||kn>108)return -LX_EINVAL; for(int i=0;i<kn;i++)key[i]=p[i]; *kl=kn; return 0;
}
static int lx_un_keyout(u64 a,u64 lenp,const u8* key,int kl){
    if(!a||!lenp)return 0; u32 L; if(lx_get(&L,lenp,4))return -LX_EFAULT;
    u8 sa[112]; lx_zero(sa,112); sa[0]=1; int kn=kl>108?108:kl; for(int i=0;i<kn;i++)sa[2+i]=key[i];
    u32 tot=(u32)(2+kn+(key&&kl&&key[0]?1:0)); if(lx_put(a,sa,L<tot?L:tot))return -LX_EFAULT; return lx_put(lenp,&tot,4);
}
static long lx_socket(int dom,int type,int proto){
    int tu=type&0xF;
    if(dom==1){ if(tu!=1&&tu!=5){ lx_gap(1,(u64)tu); return -93; } int ui=un_new(); if(ui<0)return ui; g_un[ui].seq=tu==5; int vi=jc_newvf(LX_AFU,ui,O_RDWR|(type&0x800)); if(vi<0){ un_close(ui); return -LX_EMFILE; } int fd=lx_fd_install(vi,(type&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd; }
    if(dom!=2){ lx_gap(2,(u64)dom); return -97; }
    int t=type&0xF; if(t!=1&&t!=2){ lx_gap(3,(u64)t); return -93; }
    if(t==1&&proto!=0&&proto!=6)return -93; if(t==2&&proto!=0&&proto!=17)return -93;
    int sid=lxs_new(t); if(sid<0)return sid;
    int vi=jc_newvf(14,sid,O_RDWR|(type&0x800)); if(vi<0){ lxs_release(sid); return -LX_EMFILE; }
    int fd=lx_fd_install(vi,(type&0x80000)!=0,0); if(fd<0)vf_unref(vi); return fd;
}
static long lx_connect(struct sysregs* r,int fd,u64 a,u64 len){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ u8 key[108]; int kl; int e=lx_un_key(a,len,key,&kl); if(e)return e; int n=un_connect(vu->fs_idx,key,kl); return lx_sock_wait(r,n,lx_nonblock(vu)); }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    u32 ip; u16 port; int e=lx_sa_in(a,len,&ip,&port); if(e)return e;
    int n=lxs_connect(sid,ip,port,lx_nonblock(v));
    return lx_sock_wait(r,n,0);
}
static long lx_accept(struct sysregs* r,int fd,u64 a,u64 lenp,int flags){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ int c=un_accept(vu->fs_idx); if(c<0)return lx_sock_wait(r,c,lx_nonblock(vu));
        int vi=jc_newvf(LX_AFU,c,O_RDWR|(flags&0x800)); if(vi<0){ un_close(c); return -LX_EMFILE; }
        int nf=lx_fd_install(vi,(flags&0x80000)!=0,0); if(nf<0){ vf_unref(vi); return nf; }
        struct unsock* cs=un_get(c); if(cs&&a)lx_un_keyout(a,lenp,cs->key,cs->klen); return nf; }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    if(g_lxs[sid].type!=1)return -95; if(g_lxs[sid].st!=LXS_LISTEN)return -LX_EINVAL;
    int c=lxs_accept(sid); if(c<0)return lx_sock_wait(r,c,lx_nonblock(v));
    int vi=jc_newvf(14,c,O_RDWR|(flags&0x800)); if(vi<0){ lxs_release(c); return -LX_EMFILE; }
    int nf=lx_fd_install(vi,(flags&0x80000)!=0,0); if(nf<0){ vf_unref(vi); return nf; }
    lx_sa_out(a,lenp,g_lxs[c].rip,g_lxs[c].rport); return nf;
}
static long lx_sendto(struct sysregs* r,int fd,u64 buf,u64 len,int fl,u64 a,u64 alen){
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    if(len>65536)len=65536; if(len&&!lx_ok(buf,len))return -LX_EFAULT;
    u32 ip=0; u16 port=0; int has=0; if(a&&g_lxs[sid].type==2){ int e=lx_sa_in(a,alen,&ip,&port); if(e)return e; has=1; }
    int n=lxs_send(sid,(const u8*)buf,(int)len,ip,port,has);
    if(n==-32&&!(fl&0x4000))jc_signal_proc(g_jcur,13);
    return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
}
static long lx_recvfrom(struct sysregs* r,int fd,u64 buf,u64 len,int fl,u64 a,u64 alenp){
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    if(len>LX_IOMAX)len=LX_IOMAX; if(len&&!lx_okw(buf,len))return -LX_EFAULT;
    u32 ip=0; u16 port=0;
    int n=lxs_recv(sid,(u8*)buf,(int)len,(fl&2)!=0,&ip,&port);
    long rv=lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
    if(rv>=0&&a){ int e=lx_sa_out(a,alenp,ip,port); if(e)return e; }
    return rv;
}
static long lx_un_msg(struct sysregs* r,struct vfile* v,u64 msg,int fl,int wr){
    int sid=v->fs_idx;
    u64 m[7]; if(lx_get(m,msg,56))return -LX_EFAULT;
    u64 iov=m[2], cnt=m[3]; if(cnt>1024)return -LX_EINVAL; if(cnt&&!lx_ok(iov,cnt*16))return -LX_EFAULT;
    u64 tot=0; for(u64 i=0;i<cnt;i++){ u64 l=((u64*)iov)[i*2+1]; if(l&&!(wr?lx_ok(((u64*)iov)[i*2],l):lx_okw(((u64*)iov)[i*2],l)))return -LX_EFAULT; tot+=l; }
    if(tot>65536)tot=65536;
    u64 ctl=m[4], ctllen=m[5];
    u8* k=(u8*)kmalloc(tot?tot:1); if(!k)return -12;
    if(wr){
        u64 o=0; for(u64 i=0;i<cnt&&o<tot;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<tot;j++)k[o++]=b[j]; }
        int vfs[16]; int nf=0;
        if(ctl&&ctllen>=16&&lx_ok(ctl,ctllen)){ u64 off=0; while(off+16<=ctllen){ u64 cl=*(u64*)(ctl+off); int lvl=*(int*)(ctl+off+8), typ=*(int*)(ctl+off+12);
            if(cl<16||off+cl>ctllen)break; if(lvl==1&&typ==1){ int nfd=(int)((cl-16)/4); u8* dp=(u8*)(ctl+off+16); for(int q=0;q<nfd&&nf<16;q++){ int ufd=*(int*)(dp+q*4); if(ufd<0||ufd>=FDMAX||fdt()[ufd]<0){ nf=-1; break; } vfs[nf++]=fdt()[ufd]; } if(nf<0)break; }
            off+=(cl+7)&~7ULL; } }
        if(nf<0){ kfree(k); return -LX_EBADF; }
        int n=un_send(sid,k,(int)tot,vfs,nf); kfree(k);
        if(n==-32&&!(fl&0x4000))jc_signal_proc(g_jcur,13);
        return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
    }
    int vfs[16]; int nf=0; int maxfit=(ctl&&ctllen>=16)?(int)((ctllen-16)/4):0; if(maxfit>16)maxfit=16;
    int n=un_recv(sid,k,(int)tot,(fl&2)!=0,vfs,maxfit,&nf);
    if(n<0){ kfree(k); return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40)); }
    u64 o=0; for(u64 i=0;i<cnt&&o<(u64)n;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<(u64)n;j++)b[j]=k[o++]; }
    kfree(k);
    if(nf>0&&ctl&&ctllen>=16&&lx_okw(ctl,ctllen)){ u64 cl=16+(u64)nf*4; *(u64*)(ctl+0)=cl; *(int*)(ctl+8)=1; *(int*)(ctl+12)=1;
        for(int q=0;q<nf;q++){ int newfd=lx_fd_install(vfs[q],0,0); *(int*)(ctl+16+q*4)=newfd<0?-1:newfd; if(newfd<0)vf_unref(vfs[q]); }
        if(lx_put(msg+40,&cl,8))return -LX_EFAULT; }
    else { u64 z=0; if(nf>0)for(int q=0;q<nf;q++)vf_unref(vfs[q]); if(lx_put(msg+40,&z,8))return -LX_EFAULT; }
    if(m[0]&&m[1]){ struct unsock* s=un_get(sid); struct unsock* pr=s?un_get(s->peer):0; if(pr)lx_un_keyout(m[0],msg+8,pr->key,pr->klen); }
    u32 f0=0; if(lx_put(msg+48,&f0,4))return -LX_EFAULT;
    return n;
}
static long lx_msg(struct sysregs* r,int fd,u64 msg,int fl,int wr){
    { struct vfile* vu=lx_vf(fd); if(vu&&vu->dev==LX_AFU)return lx_un_msg(r,vu,msg,fl,wr); }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    u64 m[7]; if(lx_get(m,msg,56))return -LX_EFAULT;
    u64 iov=m[2], cnt=m[3]; if(cnt>1024)return -LX_EINVAL; if(cnt&&!lx_ok(iov,cnt*16))return -LX_EFAULT;
    u64 tot=0; for(u64 i=0;i<cnt;i++){ u64 l=((u64*)iov)[i*2+1]; if(l&&!(wr?lx_ok(((u64*)iov)[i*2],l):lx_okw(((u64*)iov)[i*2],l)))return -LX_EFAULT; tot+=l; }
    if(tot>65536)tot=65536;
    u8* k=(u8*)kmalloc(tot?tot:1); if(!k)return -12;
    long rv;
    if(wr){
        u64 o=0; for(u64 i=0;i<cnt&&o<tot;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<tot;j++)k[o++]=b[j]; }
        u32 ip=0; u16 port=0; int has=0;
        if(m[0]&&g_lxs[sid].type==2){ int e=lx_sa_in(m[0],(u32)m[1],&ip,&port); if(e){ kfree(k); return e; } has=1; }
        int n=lxs_send(sid,k,(int)tot,ip,port,has); kfree(k);
        if(n==-32&&!(fl&0x4000))jc_signal_proc(g_jcur,13);
        return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40));
    }
    u32 ip=0; u16 port=0;
    int n=lxs_recv(sid,k,(int)tot,(fl&2)!=0,&ip,&port);
    if(n<0){ kfree(k); return lx_sock_wait(r,n,lx_nonblock(v)||(fl&0x40)); }
    u64 o=0; for(u64 i=0;i<cnt&&o<(u64)n;i++){ u8* b=(u8*)((u64*)iov)[i*2]; u64 l=((u64*)iov)[i*2+1]; for(u64 j=0;j<l&&o<(u64)n;j++)b[j]=k[o++]; }
    kfree(k); rv=n;
    if(m[0]&&m[1]){ u8 sa[16]; lx_zero(sa,16); sa[0]=2; sa[2]=(u8)(port>>8); sa[3]=(u8)port; sa[4]=(u8)(ip>>24); sa[5]=(u8)(ip>>16); sa[6]=(u8)(ip>>8); sa[7]=(u8)ip;
        if(lx_put(m[0],sa,m[1]<16?m[1]:16))return -LX_EFAULT; u32 L=16; if(lx_put(msg+8,&L,4))return -LX_EFAULT; }
    u64 z=0; if(lx_put(msg+40,&z,8))return -LX_EFAULT; u32 f0=0; if(lx_put(msg+48,&f0,4))return -LX_EFAULT;
    return rv;
}
static long lx_sockname(int fd,u64 a,u64 lenp,int peer){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ struct unsock* s=un_get(vu->fs_idx); if(!s)return -LX_EBADF;
        if(peer){ struct unsock* pr=un_get(s->peer); if(!pr)return -107; return lx_un_keyout(a,lenp,pr->key,pr->klen); }
        return lx_un_keyout(a,lenp,s->key,s->klen); }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    struct lxsock* s=&g_lxs[sid];
    if(peer){ if(s->type==1?s->st!=LXS_EST:!s->conn)return -107; return lx_sa_out(a,lenp,s->rip,s->rport); }
    return lx_sa_out(a,lenp,s->lport?my_ip:0,s->lport);
}
static long lx_getsockopt(int fd,int lvl,int opt,u64 val,u64 lenp){
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    struct lxsock* s=&g_lxs[sid]; int x=0;
    if(lvl==1){ if(opt==4){ lxs_pump(); x=s->err; s->err=0; } else if(opt==3)x=s->type; else if(opt==7)x=LXS_TXCAP; else if(opt==8)x=LXS_RXCAP; else if(opt==39)x=2; else if(opt==38)x=s->type==1?6:17; }
    if(!val||!lenp)return -LX_EFAULT; u32 L; if(lx_get(&L,lenp,4))return -LX_EFAULT; if(L>4)L=4;
    if(lx_put(val,&x,L))return -LX_EFAULT; return lx_put(lenp,&L,4);
}
static long lx_bind(int fd,u64 a,u64 len){
    struct vfile* vu=lx_vf(fd); if(!vu)return -LX_EBADF;
    if(vu->dev==LX_AFU){ u8 key[108]; int kl; int e=lx_un_key(a,len,key,&kl); if(e)return e; return un_bind(vu->fs_idx,key,kl); }
    struct vfile* v; int sid=lx_sockfd(fd,&v); if(sid<0)return sid;
    u32 ip; u16 port; int e=lx_sa_in(a,len,&ip,&port); if(e)return e;
    return lxs_bind(sid,port);
}
static int a_is_in(int q){ return q==0||q==2; }
static long lx_ioctl(int fd,u64 req,u64 arg){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    struct jproc* p=&g_jp[g_jcur];
    if(req==0x5451){ fdce()[fd]=1; return 0; }
    if(v->dev==14){ if(req==0x541B){ lxs_pump(); int n=g_lxs[v->fs_idx].type==2?(g_lxs[v->fs_idx].rxn?rd16(g_lxs[v->fs_idx].rx+6):0):g_lxs[v->fs_idx].rxn; return lx_put(arg,&n,4); } if(req==0x5421){ int on=0; if(lx_get(&on,arg,4))return -LX_EFAULT; if(on)v->flags|=0x800; else v->flags&=~0x800; return 0; } if(req==0x5450){ fdce()[fd]=0; return 0; } return -25; }
    if(req==0x5450){ fdce()[fd]=0; return 0; }
    if(req==0x5421){ int on; if(lx_get(&on,arg,4))return -LX_EFAULT; if(on)v->flags|=0x800; else v->flags&=~0x800; return 0; }
    if(req==0x541B){ int n=0; if(v->dev==6)n=g_pipe[v->fs_idx].n; else if(v->dev==5)n=g_tin_n; else if(v->dev==0&&v->fs_idx>=0)n=FS[v->fs_idx].len-v->off; if(n<0)n=0; return lx_put(arg,&n,4); }
    if(v->dev==12){
        if(req==0x5000||req==0x5001)return 0;
        if(req==0xC0045002u){ int r=g_snd_state==1&&g_snd.rate>0?g_snd.rate:48000; return lx_put(arg,&r,4); }
        if(req==0xC0045005u){ int f=0x10; return lx_put(arg,&f,4); }
        if(req==0xC0045006u){ int c=2; return lx_put(arg,&c,4); }
        if(req==0xC0045003u){ int c=1; return lx_put(arg,&c,4); }
        if(req==0x8004500Bu){ int m=0x10; return lx_put(arg,&m,4); }
        if(req==0x8010500Cu){ int bi[4]; int fr=snd_init()?(g_snd.start(),g_snd.free_bytes()):0; bi[0]=fr/4096; bi[1]=32; bi[2]=4096; bi[3]=fr; return lx_put(arg,bi,16); }
        if(req==0xC004500Au){ return 0; }
        if(req==0x8010500Du){ int bi[4]; int av=snd_rec_start()?snd_cap_avail():0; bi[0]=av/4096; bi[1]=256; bi[2]=4096; bi[3]=av; return lx_put(arg,bi,16); }
        if(req==0x80045004u||req==0xC0045004u){ int bs=4096; return lx_put(arg,&bs,4); }
        if(req==0x8004500Fu){ int c=0x1100; return lx_put(arg,&c,4); }
        if(req==0x40045010u||req==0x5016u){ return 0; }
        return -LX_EINVAL; }
    if(v->dev==15){ extern long media_ioctl(int vfidx,u64 req,u64 arg); return media_ioctl((int)(v-g_vf),req,arg); }
    if(v->dev==9){
        if(req==0x4600){ u32 vi[40]; lx_zero((u8*)vi,160); vi[0]=W; vi[1]=H; vi[2]=SW; vi[3]=H; vi[6]=32; vi[8]=16; vi[9]=8; vi[11]=8; vi[12]=8; vi[14]=0; vi[15]=8; vi[17]=24; vi[18]=8; return lx_put(arg,vi,160); }
        if(req==0x4602){ u8 fx[80]; lx_zero(fx,80); const char* id="noovexfb"; for(int i=0;id[i];i++)fx[i]=(u8)id[i]; *(u32*)(fx+24)=SW*H*4; *(u32*)(fx+36)=2; *(u32*)(fx+48)=SW*4; return lx_put(arg,fx,80); }
        return -LX_EINVAL; }
    if(v->dev!=5)return -LX_ENOTTY;
    if(req==0x802C542Aull){ vt_raw(); u8 t[44]; lx_zero(t,44); *(u32*)(t+0)=g_tios.iflag; *(u32*)(t+4)=g_tios.oflag; *(u32*)(t+8)=g_tios.cflag; *(u32*)(t+12)=g_tios.lflag; t[16]=g_tios.line;
        for(int i=0;i<19;i++)t[17+i]=g_tios.cc[i]; *(u32*)(t+36)=38400; *(u32*)(t+40)=38400; return lx_put(arg,t,44); }
    if(req==0x402C542Bull||req==0x402C542Cull||req==0x402C542Dull){ u8 t[44]; if(lx_get(t,arg,44))return -LX_EFAULT; vt_raw();
        g_tios.iflag=*(u32*)(t+0); g_tios.oflag=*(u32*)(t+4); g_tios.cflag=*(u32*)(t+8)|0x30; g_tios.lflag=*(u32*)(t+12); g_tios.line=t[16]; for(int i=0;i<19;i++)g_tios.cc[i]=t[17+i];
        if(req==0x402C542Dull){ g_tin_n=0; g_tin_eof=0; }
        return 0; }
    switch(req){
    case 0x5401: { vt_raw(); u8 t[36]; lx_zero(t,36); *(u32*)(t+0)=g_tios.iflag; *(u32*)(t+4)=g_tios.oflag; *(u32*)(t+8)=g_tios.cflag; *(u32*)(t+12)=g_tios.lflag; t[16]=g_tios.line;
        for(int i=0;i<19;i++)t[17+i]=g_tios.cc[i];
        return lx_put(arg,t,36); }
    case 0x5402: case 0x5403: case 0x5404: { u8 t[36]; if(lx_get(t,arg,36))return -LX_EFAULT; vt_raw();
        g_tios.iflag=*(u32*)(t+0); g_tios.oflag=*(u32*)(t+4); g_tios.cflag=*(u32*)(t+8)|0x30; g_tios.lflag=*(u32*)(t+12); g_tios.line=t[16]; for(int i=0;i<19;i++)g_tios.cc[i]=t[17+i];
        if(req==0x5404){ g_tin_n=0; g_tin_eof=0; }
        return 0; }
    case 0x540B: if(a_is_in((int)arg)){ g_tin_n=0; g_tin_eof=0; } return 0;
    case 0x5413: { u16 w[4]; w[0]=(u16)(trows>0?trows:25); w[1]=(u16)(tcols>0?tcols:80); w[2]=0; w[3]=0; return lx_put(arg,w,8); }
    case 0x5414: return 0;
    case 0x540F: { int g=g_tty_fg?g_tty_fg:jc_lead(p)->pgid; return lx_put(arg,&g,4); }
    case 0x5410: { int g; if(lx_get(&g,arg,4))return -LX_EFAULT; if(jc_lead(p)->sid!=1)return -LX_ENOTTY; g_tty_fg=g; return 0; }
    case 0x5429: { int s=1; return lx_put(arg,&s,4); }
    case 0x540E: return 0;
    }
    lx_gap(4,req); return -LX_EINVAL;
}
static long lx_fcntl(int fd,int cmd,u64 arg){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    int* t=fdt();
    switch(cmd){
    case 0: case 1030: { if(arg>=FDMAX)return -LX_EINVAL; int n=lx_fd_install(t[fd],cmd==1030,(int)arg); if(n>=0)vf_ref(t[fd]); return n; }
    case 1: return fdce()[fd]&1;
    case 2: fdce()[fd]=(u8)(arg&1); return 0;
    case 3: return (long)(v->flags&~0x80000);
    case 4: v->flags=(v->flags&~(0x400|0x800))|(int)(arg&(0x400|0x800)); return 0;
    case 5: { if(!arg)return -LX_EFAULT; short ty=2; return lx_put(arg,&ty,2); }
    case 6: case 7: return 0;
    case 1025: case 1031: return 0;
    case 1032: return 4096;
    case 1033: { if(v->dev!=LX_MFD)return -LX_EINVAL; struct memfd* m=&g_mfd[v->fs_idx]; if(arg&~0x3FULL)return -LX_EINVAL; if(m->seals&1)return -LX_EPERM; m->seals|=(int)arg; return 0; }
    case 1034: { if(v->dev!=LX_MFD)return -LX_EINVAL; return g_mfd[v->fs_idx].seals; }
    case 36: { if(!arg)return -LX_EFAULT; short ty=2; return lx_put(arg,&ty,2); }
    case 37: case 38: return 0;
    case 8: case 10: case 15: case 1024: case 1026: case 1036: case 1037: case 1038: case 1039: return 0;
    case 9: case 11: return 0;
    case 16: { if(!arg)return -LX_EFAULT; u32 z[2]={0,0}; return lx_put(arg,z,8); }
    }
    lx_gap(5,(u64)cmd); return -LX_EINVAL;
}
static long lx_pipe(u64 out,int flags){
    if(!lx_okw(out,8))return -LX_EFAULT;
    int rv,wv; if(jc_pipe_new(4096,&rv,&wv)<0)return -LX_EMFILE;
    int a=lx_fd_install(rv,flags&0x80000,0); if(a<0){ vf_unref(rv); vf_unref(wv); return a; }
    int b=lx_fd_install(wv,flags&0x80000,0); if(b<0){ fdt()[a]=-1; vf_unref(rv); vf_unref(wv); return b; }
    if(flags&0x800){ g_vf[rv].flags|=0x800; g_vf[wv].flags|=0x800; }
    ((int*)out)[0]=a; ((int*)out)[1]=b; return 0;
}
static long lx_dup3(int o,int n,int fl){
    if(!lx_vf(o))return -LX_EBADF; if(n<0||n>=FDMAX)return -LX_EBADF;
    if(o==n)return fl<0?n:-LX_EINVAL;
    jc_dup2(o,n); if(fl>0&&(fl&0x80000))fdce()[n]=1; return n;
}
static long lx_close(int fd){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    int* t=fdt(); vf_unref(t[fd]); t[fd]=-1; fdce()[fd]=0; return 0;
}
static long lx_getrandom(u64 buf,u64 n){
    if(n>LX_IOMAX)n=LX_IOMAX; if(!lx_okw(buf,n))return -LX_EFAULT;
    csprng_bytes((u8*)buf,(int)n);
    return (long)n;
}
static long lx_sysinfo(u64 out){
    u8 s[112]; lx_zero(s,112);
    *(u64*)(s+0)=g_ticks/100; *(u64*)(s+32)=(u64)g_ram_mb<<20; *(u64*)(s+40)=(u64)pframe_free_count()*4096ULL;
    int n=0; for(int i=0;i<JMAX;i++) if(g_jp[i].state!=S_FREE)n++; *(u16*)(s+80)=(u16)n; *(u32*)(s+104)=1;
    return lx_put(out,s,112);
}
static long lx_statfs(u64 out){
    unsigned long o[8]; ux_statfs("/",o); u64 s[15]; lx_zero(s,sizeof(s));
    s[0]=o[0]; s[1]=o[1]; s[2]=o[2]; s[3]=o[3]; s[4]=o[4]; s[5]=o[5]; s[6]=o[6]; s[8]=o[7]; s[9]=o[1];
    return lx_put(out,s,120);
}
static long lx_sendfile(struct sysregs* r,int out,int in,u64 offp,u64 cnt){
    struct vfile* vi=lx_vf(in); struct vfile* vo=lx_vf(out); if(!vi||!vo)return -LX_EBADF;
    if((vi->flags&3)==1)return -LX_EBADF;
    if(vo->dev==0&&(vo->flags&3)==0)return -LX_EBADF;
    if(vo->dev==LX_DIRDEV||vo->dev==6||vo->dev==100)return -LX_EBADF;
    if(vi->dev!=0||vi->fs_idx<0)return -LX_EINVAL;
    long off=vi->off; if(offp){ if(lx_get(&off,offp,8))return -LX_EFAULT; }
    if(off<0)return -LX_EINVAL;
    int k=vi->fs_idx; long avail=FS[k].len-off; if(avail<=0)return 0; if(cnt>(u64)avail)cnt=(u64)avail; if(cnt>LX_IOMAX)cnt=LX_IOMAX;
    if(fs_ensure(k))return -LX_EIO;
    int n; if(vo->dev==0&&vo->fs_idx==k){ static char sfb[65536]; if(cnt>sizeof sfb)cnt=sizeof sfb; kmemcpy(sfb,FS[k].data+off,(unsigned long)cnt); n=vfs_write(out,sfb,(int)cnt); } else n=vfs_write(out,FS[k].data+off,(int)cnt);
    if(n==JS_BLOCK){ if(lx_nonblock(vo))return -LX_EAGAIN; jc_block(r,S_WAIT,g_ticks+1); }
    if(n<0)return vo->dev==7?-LX_EPIPE:-LX_EIO;
    if(n>0)lx_persist(out);
    if(offp){ long no=off+n; lx_put(offp,&no,8); } else vi->off+=n;
    return n;
}
static long lx_utimens(const char* p,u64 times){
    u32 at=0,mt=0;
    if(times){ u64 t[4]; if(lx_get(t,times,32))return -LX_EFAULT;
        if(t[1]==0x3ffffffeULL&&t[3]==0x3ffffffeULL)return 0;
        if(t[1]!=0x3fffffffULL&&t[1]!=0x3ffffffeULL)at=(u32)t[0]; if(t[3]!=0x3fffffffULL&&t[3]!=0x3ffffffeULL)mt=(u32)t[2]; }
    if(!lx_exists(p))return -LX_ENOENT;
    return ux_utime(p,at,mt)?-LX_EACCES:0;
}
static long lx_chdir_node(int d){ if(d<0)return -LX_ENOENT; g_dcwd=d; return 0; }
static long lx_getcwd(u64 buf,u64 sz){
    char pb[256]; dt_path(g_dcwd,pb); u64 L=(u64)lx_slen(pb)+1;
    if(sz<L)return -LX_ERANGE; if(lx_put(buf,pb,L))return -LX_EFAULT; return (long)L;
}
static long lx_fchmod(int fd,int mode){
    struct vfile* v=lx_vf(fd); if(!v)return -LX_EBADF;
    if(v->dev==LX_DIRDEV){ char dp[LX_PATH]; dt_path(v->fs_idx,dp); return ux_chmod(dp,(unsigned)mode)?-LX_EPERM:0; }
    if(v->dev!=0||v->fs_idx<0)return 0; int k=v->fs_idx;
    if(g_uid!=0&&g_uid!=FS[k].uid)return -LX_EPERM;
    FS[k].mode=(FS[k].mode&0170000u)|((unsigned)mode&07777u); if(!(FS[k].mode&0170000u))FS[k].mode|=0100000u; ux_touch(k,0); nvx3_flush_meta(); return 0;
}
static long lx_setid(int* cur,int nv){ if(nv==-1)return 0; if(g_uid==0||nv==*cur){ *cur=nv; return 0; } return -LX_EPERM; }

struct lx_scr { u32 nr,pid; u64 a1,a2; long ret; };
static struct lx_scr g_scr[32]; static int g_scr_i;
struct lx_shm { int used,key,rm,natt,mi; u64 size; u32 cpid; };
static struct lx_shm g_lxshm[64];
struct lx_sha { u64 cr3,va,len; int id; };
static struct lx_sha g_sha[256];
static void lx_shm_free(int id){ struct lx_shm* s=&g_lxshm[id]; if(!s->used)return; mfd_close(s->mi); s->used=0; }
static void lx_sha_dead(u64 cr3){ cr3&=~0xFFFULL; for(int i=0;i<256;i++){ struct lx_sha* a=&g_sha[i]; if(!a->len||a->cr3!=cr3)continue; struct lx_shm* s=&g_lxshm[a->id]; a->len=0; if(s->used&&--s->natt<=0){ s->natt=0; if(s->rm)lx_shm_free(a->id); } } }
static void lx_sha_dup(u64 src,u64 dst){ src&=~0xFFFULL; int n=0; for(int i=0;i<256;i++) if(g_sha[i].len&&g_sha[i].cr3==src)n++;
    for(int i=0;i<256&&n>0;i++) if(g_sha[i].len&&g_sha[i].cr3==src){ n--; for(int j=0;j<256;j++) if(!g_sha[j].len){ g_sha[j]=g_sha[i]; g_sha[j].cr3=dst&~0xFFFULL; g_lxshm[g_sha[i].id].natt++; break; } } }
static long lx_shmget(int key,u64 size,int fl){
    if(key!=0){ for(int i=0;i<64;i++){ struct lx_shm* s=&g_lxshm[i]; if(s->used&&!s->rm&&s->key==key){ if((fl&0x200)&&(fl&0x400))return -LX_EEXIST; if(size>s->size)return -LX_EINVAL; return i; } } if(!(fl&0x200))return -LX_ENOENT; }
    if(size<1||size>0x40000000ULL)return -LX_EINVAL;
    { u64 pg=(size+0xFFFULL)>>12; if(pg>(u64)(pframe_free_count()/4u))return -LX_ENOMEM; }
    int id=-1; for(int i=0;i<64;i++) if(!g_lxshm[i].used){ id=i; break; } if(id<0)return -28;
    int mi=mfd_new(); if(mi<0)return -28; if(mfd_truncate(mi,(size+0xFFFULL)&~0xFFFULL)){ mfd_close(mi); return -LX_ENOMEM; }
    struct lx_shm* s=&g_lxshm[id]; s->used=1; s->key=key; s->rm=0; s->natt=0; s->mi=mi; s->size=size; s->cpid=(u32)jc_lead(&g_jp[g_jcur])->pid; return id; }
static long lx_shmat(int id,u64 addr,int fl){
    if(id<0||id>=64||!g_lxshm[id].used)return -LX_EINVAL; struct lx_shm* s=&g_lxshm[id]; struct jproc* p=&g_jp[g_jcur]; struct memfd* m=&g_mfd[s->mi];
    u64 pages=(s->size+0xFFFULL)>>12, base;
    if(addr){ if(fl&0x2000)addr&=~0xFFFULL; if(addr&0xFFF)return -LX_EINVAL; if(!jc_uok(addr,pages<<12))return -LX_EINVAL; if(!(fl&0x4000)){ for(u64 v=addr;v<addr+(pages<<12);v+=0x1000ULL){ u64* e=vm_walk(p->cr3,v,0); if(e&&vm_pte_used(*e))return -LX_EINVAL; } } lx_unmap_range(addr,addr+(pages<<12)); base=addr; }
    else { base=lx_find_gap(p->cr3,pages+1); if(!base)return -LX_ENOMEM; }
    int slot=-1; for(int i=0;i<256;i++) if(!g_sha[i].len){ slot=i; break; } if(slot<0)return -LX_ENOMEM;
    u64 pf=(fl&0x1000)?(0x5ULL|PTE_SHR):(0x7ULL|PTE_SHR);
    for(u64 i=0;i<pages;i++){ u64 f=m->frames[i]; pframe_ref(f); if(vm_map(p->cr3,base+(i<<12),f,pf)){ pframe_free(f); lx_unmap_range(base,base+(i<<12)); return -LX_ENOMEM; } }
    g_sha[slot].cr3=p->cr3&~0xFFFULL; g_sha[slot].va=base; g_sha[slot].len=pages<<12; g_sha[slot].id=id; s->natt++; return (long)base; }
static long lx_shmdt(u64 addr){ struct jproc* p=&g_jp[g_jcur]; u64 cr3=p->cr3&~0xFFFULL;
    for(int i=0;i<256;i++){ struct lx_sha* a=&g_sha[i]; if(a->len&&a->cr3==cr3&&a->va==addr){ u64 len=a->len; int id=a->id; a->len=0; lx_unmap_range(addr,addr+len); struct lx_shm* s=&g_lxshm[id]; if(s->used&&--s->natt<=0){ s->natt=0; if(s->rm)lx_shm_free(id); } return 0; } }
    return -LX_EINVAL; }
static long lx_shmctl(int id,int cmd,u64 buf){ cmd&=0xFF;
    if(cmd==3||cmd==14){ if(buf){ u8 z[128]; lx_zero(z,128); if(lx_put(buf,z,cmd==3?64:56))return -LX_EFAULT; } return 63; }
    if(id<0||id>=64||!g_lxshm[id].used)return -LX_EINVAL; struct lx_shm* s=&g_lxshm[id];
    if(cmd==0){ s->rm=1; if(s->natt<=0)lx_shm_free(id); return 0; }
    if(cmd==2||cmd==13){ if(!buf)return -LX_EFAULT; u8 d[112]; lx_zero(d,112); *(int*)(d+0)=s->key; *(u32*)(d+4)=(u32)g_uid; *(u32*)(d+8)=(u32)g_gid; *(u32*)(d+12)=(u32)g_uid; *(u32*)(d+16)=(u32)g_gid; *(u16*)(d+20)=0777; *(u64*)(d+48)=s->size; *(u32*)(d+80)=s->cpid; *(u64*)(d+88)=(u64)s->natt; if(lx_put(buf,d,112))return -LX_EFAULT; return cmd==13?id:0; }
    if(cmd==1||cmd==11||cmd==12)return 0;
    return -LX_EINVAL; }
struct lx_scr g_scr_snap[32]; int g_scr_sn;
u32 g_scf_nr[40], g_scf_err[40], g_scf_ct[40]; u64 g_scf_a1[40], g_scf_a2[40]; int g_scf_n;
void lx_sc_snap(void){ int n=g_scr_i<32?g_scr_i:32; for(int i=0;i<n;i++)g_scr_snap[i]=g_scr[(g_scr_i-n+i)%32]; g_scr_sn=n; }
u16 g_lastsc[JMAX]; u32 g_sccount[JMAX];
static void lx_sc_note(u32 nr,u32 pid,u64 a1,u64 a2,long v){
    if(g_jcur>=0&&g_jcur<JMAX){ g_lastsc[g_jcur]=(u16)nr; g_sccount[g_jcur]++; }
    struct lx_scr* s=&g_scr[g_scr_i%32]; g_scr_i++; s->nr=nr; s->pid=pid; s->a1=a1; s->a2=a2; s->ret=v;
    if(v>=0||v<-4095)return; u32 e=(u32)(-v); if(e==2||e==11||e==4||e==38||e==110||e==25||e==17||e==20)return;
    for(int i=0;i<g_scf_n;i++) if(g_scf_nr[i]==nr&&g_scf_err[i]==e){ g_scf_ct[i]++; g_scf_a1[i]=a1; g_scf_a2[i]=a2; return; }
    if(g_scf_n<40){ g_scf_nr[g_scf_n]=nr; g_scf_err[g_scf_n]=e; g_scf_ct[g_scf_n]=1; g_scf_a1[g_scf_n]=a1; g_scf_a2[g_scf_n]=a2; g_scf_n++; } }
static long lx_dispatch(struct sysregs* r){
    struct jproc* p=&g_jp[g_jcur];
    u64 nr=r->rax, a1=r->rdi, a2=r->rsi, a3=r->rdx, a4=r->r10, a5=r->r8, a6=r->r9;
    char pa[LX_PATH], pb[LX_PATH]; long e;
    #define PATH1(fd,u,emp) do{ e=lx_path((fd),(u),pa,(emp)); if(e<0)return e; }while(0)
    #define PATH2(fd,u) do{ e=lx_path((fd),(u),pb,0); if(e<0)return e; }while(0)
    switch(nr){
    case 0: return lx_read(r,(int)a1,a2,a3);
    case 1: return lx_write(r,(int)a1,a2,a3);
    case 2: return lx_open(LX_AT_FDCWD,a1,(int)a2,(int)a3);
    case 3: return lx_close((int)a1);
    case 4: PATH1(LX_AT_FDCWD,a1,0); return lx_stat_path(pa,a2,1);
    case 5: return lx_fstat((int)a1,a2);
    case 6: PATH1(LX_AT_FDCWD,a1,0); return lx_stat_path(pa,a2,0);
    case 7: return lx_poll(r,a1,a2,(long)(int)a3);
    case 213: return lx_epoll_create(0);
    case 291: return lx_epoll_create((int)a1);
    case 233: return lx_epoll_ctl((int)a1,(int)a2,(int)a3,a4);
    case 232: case 281: case 441: return lx_epoll_wait(r,(int)a1,a2,(int)a3,nr==441?(a4?0:-1):(long)(int)a4);
    case 8: return lx_lseek((int)a1,(long)a2,(int)a3);
    case 9: return lx_mmap(a1,a2,(int)a3,(int)a4,(int)a5,a6);
    case 10: return 0;
    case 11: return lx_munmap(a1,a2);
    case 12: return lx_brk(a1);
    case 13: return lx_sigaction((int)a1,a2,a3,a4);
    case 14: return lx_sigprocmask((int)a1,a2,a3,a4);
    case 15: lx_sigreturn(r);
    case 16: return lx_ioctl((int)a1,a2,a3);
    case 17: return lx_prw((int)a1,a2,a3,(long)a4,0);
    case 18: return lx_prw((int)a1,a2,a3,(long)a4,1);
    case 19: return lx_rwv(r,(int)a1,a2,(int)a3,0);
    case 20: return lx_rwv(r,(int)a1,a2,(int)a3,1);
    case 21: PATH1(LX_AT_FDCWD,a1,0); return lx_access(pa,(int)a2);
    case 22: return lx_pipe(a1,0);
    case 23: { long ms=-1; if(a5){ u64 tv[2]; if(lx_get(tv,a5,16))return -LX_EFAULT; if((i64)tv[0]<0||tv[1]>=1000000ULL)return -LX_EINVAL; if(tv[0]>100000000ULL)tv[0]=100000000ULL; ms=(long)(tv[0]*1000+tv[1]/1000); } return lx_select(r,(int)a1,a2,a3,a4,ms); }
    case 24: r->rax=0; jc_yield_sys(r);
    case 25: return -LX_ENOMEM;
    case 29: return lx_shmget((int)a1,a2,(int)a3);
    case 30: return lx_shmat((int)a1,a2,(int)a3);
    case 31: return lx_shmctl((int)a1,(int)a2,a3);
    case 67: return lx_shmdt(a1);
    case 26: { if(a1&0xFFF)return -LX_EINVAL; lx_fm_msync(p->cr3,a1,a1+a2); return 0; }
    case 27: return -LX_ENOSYS;
    case 28: return lx_madvise(a1,a2,(int)a3);
    case 187: return 0;
    case 32: { if(!lx_vf((int)a1))return -LX_EBADF; int vi=fdt()[(int)a1]; int n=lx_fd_install(vi,0,0); if(n>=0)vf_ref(vi); return n; }
    case 33: return lx_dup3((int)a1,(int)a2,-1);
    case 34: r->rax=(u64)-LX_EINTR; jc_block(r,S_SLEEP,g_ticks+0x7FFFFFFFFFFFULL);
    case 35: return lx_nanosleep(r,a1);
    case 36: return lx_getitimer((int)a1,a2);
    case 38: return lx_setitimer((int)a1,a2,a3);
    case 37: return lx_alarm((unsigned)a1);
    case 39: return jc_lead(p)->pid;
    case 40: return lx_sendfile(r,(int)a1,(int)a2,a3,a4);
    case 41: return lx_socket((int)a1,(int)a2,(int)a3);
    case 42: return lx_connect(r,(int)a1,a2,a3);
    case 43: return lx_accept(r,(int)a1,a2,a3,0);
    case 288: return lx_accept(r,(int)a1,a2,a3,(int)a4);
    case 44: return lx_sendto(r,(int)a1,a2,a3,(int)a4,a5,a6);
    case 45: return lx_recvfrom(r,(int)a1,a2,a3,(int)a4,a5,a6);
    case 46: return lx_msg(r,(int)a1,a2,(int)a3,1);
    case 47: return lx_msg(r,(int)a1,a2,(int)a3,0);
    case 48: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU){ if(a2>2)return -LX_EINVAL; struct unsock* s=un_get(vu->fs_idx); if(!s)return -LX_EBADF; if(a2==0||a2==2)s->rd_shut=1; if(a2==1||a2==2)s->wr_shut=1; return 0; } struct vfile* v; int sid=lx_sockfd((int)a1,&v); if(sid<0)return sid; if(a2>2)return -LX_EINVAL; return lxs_shutdown(sid,(int)a2); }
    case 49: return lx_bind((int)a1,a2,a3);
    case 50: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU)return un_listen(vu->fs_idx,(int)a2); struct vfile* v; int sid=lx_sockfd((int)a1,&v); return sid<0?sid:lxs_listen(sid,(int)a2); }
    case 51: return lx_sockname((int)a1,a2,a3,0);
    case 52: return lx_sockname((int)a1,a2,a3,1);
    case 53: { if((int)a1!=1){ lx_gap(2,a1); return -97; } int sty=(int)a2&0xF; if(sty!=1&&sty!=2&&sty!=5){ lx_gap(8,(u64)sty); return -93; } if(!lx_okw(a4,8))return -LX_EFAULT;
        int A=un_new(); if(A<0)return A; int B=un_new(); if(B<0){ un_close(A); return B; }
        g_un[A].state=4; g_un[A].peer=B; g_un[B].state=4; g_un[B].peer=A; g_un[A].seq=g_un[B].seq=sty!=1;
        int cl=((int)a2&0x80000)!=0, nb=(int)a2&0x800;
        int va=jc_newvf(LX_AFU,A,O_RDWR|nb); if(va<0){ un_close(A); un_close(B); return -LX_EMFILE; }
        int vb=jc_newvf(LX_AFU,B,O_RDWR|nb); if(vb<0){ vf_unref(va); un_close(B); return -LX_EMFILE; }
        int fa=lx_fd_install(va,cl,0); if(fa<0){ vf_unref(va); vf_unref(vb); return fa; }
        int fb=lx_fd_install(vb,cl,0); if(fb<0){ fdt()[fa]=-1; vf_unref(va); vf_unref(vb); return fb; }
        int sv[2]={fa,fb}; if(lx_put(a4,sv,8)){ fdt()[fa]=-1; fdt()[fb]=-1; vf_unref(va); vf_unref(vb); return -LX_EFAULT; }
        return 0; }
    case 54: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU)return 0; struct vfile* v; int sid=lx_sockfd((int)a1,&v); return sid<0?sid:0; }
    case 55: { struct vfile* vu=lx_vf((int)a1); if(vu&&vu->dev==LX_AFU){ int lvl=(int)a2,opt=(int)a3,x=0;
            if(lvl==1&&opt==17){ u32 uc[3]={1,0,0}; if(!a4||!a5)return -LX_EFAULT; u32 L; if(lx_get(&L,a5,4))return -LX_EFAULT; if(L>12)L=12; if(lx_put(a4,uc,L))return -LX_EFAULT; return lx_put(a5,&L,4); }
            if(lvl==1){ if(opt==3)x=1; else if(opt==7||opt==8)x=UN_CAP; }
            if(!a4||!a5)return -LX_EFAULT; u32 L; if(lx_get(&L,a5,4))return -LX_EFAULT; if(L>4)L=4; if(lx_put(a4,&x,L))return -LX_EFAULT; return lx_put(a5,&L,4); }
        return lx_getsockopt((int)a1,(int)a2,(int)a3,a4,a5); }
    case 319: return lx_memfd((int)a2);
    case 284: return lx_eventfd(a1,0);
    case 290: return lx_eventfd(a1,(int)a2);
    case 283: return lx_timerfd_create((int)a1,(int)a2);
    case 286: return lx_timerfd_set((int)a1,(int)a2,a3,a4);
    case 287: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if(v->dev!=LX_TMFD)return -LX_EINVAL; return lx_timerfd_get(&g_tm[v->fs_idx],a2); }
    case 204: { u64 m=0; extern volatile int g_cores_online; int n=g_smp_on?g_cores_online:1; for(int i=0;i<n&&i<64;i++)m|=1ULL<<i; u64 cap=a2; if(cap<8)return -LX_EINVAL; if(lx_put(a3,&m,8))return -LX_EFAULT; return 8; }
    case 203: return 0;
    case 309: { u32 c=(u32)cpu_id(); if(a1&&lx_put(a1,&c,4))return -LX_EFAULT; if(a2&&lx_put(a2,&(u32){0},4))return -LX_EFAULT; return 0; }
    case 60: jc_thread_exit(g_jcur,((int)a1&0xFF)<<8); jc_leave();
    case 231: jc_finish(g_jcur,((int)a1&0xFF)<<8); jc_leave();
    case 56: return lx_clone(r,a1,a2,a3,a4,a5);
    case 57: case 58: return lx_clone(r,0,0,0,0,0);
    case 59: return lx_execve(r,a1,a2,a3);
    case 61: return lx_wait4(r,(int)a1,a2,(int)a3,a4);
    case 200: case 234: { int tid=(int)(nr==234?a2:a1), sig=(int)(nr==234?a3:a2);
        if(sig<0||sig>64||tid<=0)return -LX_EINVAL; int t=jc_idx(tid); if(t<0||g_jp[t].state==S_ZOMB||g_jp[t].state==S_TDEAD)return -LX_ESRCH;
        if(nr==234&&g_jp[t].tgid!=(int)a1)return -LX_ESRCH;
        if(g_uid!=0&&g_jp[t].uid!=g_uid)return -LX_EPERM;
        if(sig>=32||!sig)return 0;
        if(sig==9||sig==19)jc_signal_proc(t,sig); else jc_signal_thread(t,sig); return 0; }
    case 62: { int pid=(int)a1, sig=(int)a2;
        if(sig<0||sig>64)return -LX_EINVAL; if(sig>=32)return 0;
        g_kill_check=1; int rv=jc_kill(pid,sig,g_jcur); g_kill_check=0;
        if(rv<0)return g_kill_denied?-LX_EPERM:-LX_ESRCH; return 0; }
    case 63: return lx_uname(a1);
    case 72: return lx_fcntl((int)a1,(int)a2,a3);
    case 73: return lx_vf((int)a1)?0:-LX_EBADF;
    case 74: case 75: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if(v->dev==0&&v->fs_idx>=0&&v->fs_idx<FS_N)g_fs_dirty[v->fs_idx]=0; return k_fsync((int)a1)?-LX_EIO:0; }
    case 76: PATH1(LX_AT_FDCWD,a1,0); if((long)a2<0)return -LX_EINVAL; if(!lx_exists(pa))return -LX_ENOENT; if(lx_isdir(pa))return -LX_EISDIR; return k_truncate(pa,(long)a2)?-LX_EACCES:0;
    case 77: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if((long)a2<0)return -LX_EINVAL; if(v->dev==LX_MFD){ struct memfd* m=&g_mfd[v->fs_idx]; if(((u64)a2>m->size&&(m->seals&4))||((u64)a2<m->size&&(m->seals&2)))return -LX_EPERM; return mfd_truncate(v->fs_idx,(u64)a2); } if(v->dev!=0||(v->flags&3)==0)return -LX_EINVAL; return k_ftruncate((int)a1,(long)a2)?-LX_EACCES:0; }
    case 78: return -LX_ENOSYS;
    case 79: return lx_getcwd(a1,a2);
    case 80: PATH1(LX_AT_FDCWD,a1,0); { int d=dt_resolve_dir(pa); if(d<0)return lx_exists(pa)?-LX_ENOTDIR:lx_noent(pa); return lx_chdir_node(d); }
    case 81: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if(v->dev!=LX_DIRDEV)return -LX_ENOTDIR; return lx_chdir_node(v->fs_idx); }
    case 82: PATH1(LX_AT_FDCWD,a1,0); PATH2(LX_AT_FDCWD,a2); return lx_rename(pa,pb);
    case 83: PATH1(LX_AT_FDCWD,a1,0); return lx_mkdir(pa,(int)a2);
    case 84: PATH1(LX_AT_FDCWD,a1,0); return lx_rmdir(pa);
    case 85: return lx_open(LX_AT_FDCWD,a1,0x241,(int)a2);
    case 86: PATH1(LX_AT_FDCWD,a1,0); PATH2(LX_AT_FDCWD,a2); { int L=lx_slen(pb), c=0; while(L>0&&pb[L-1]!='/'){ L--; c++; } if(c>51)return -LX_ENAMETOOLONG; } if(!lx_exists(pa))return -LX_ENOENT; if(lx_exists(pb))return -LX_EEXIST; return ux_link(pa,pb)?-LX_EPERM:0;
    case 87: PATH1(LX_AT_FDCWD,a1,0); return lx_unlink(pa);
    case 88: { int n=lx_str(a1,pa,LX_PATH); if(n<0)return n; PATH2(LX_AT_FDCWD,a2); if(lx_exists(pb))return -LX_EEXIST; return ux_symlink(pa,pb)?-LX_ENOSPC:0; }
    case 89: { char t[64]; if(lx_str(a1,t,64)>0){ int pe=lx_proc_exe(t,a2,a3); if(pe!=-9999)return pe; } } PATH1(LX_AT_FDCWD,a1,0); { if((long)a3<=0)return -LX_EINVAL; if(!lx_okw(a2,a3))return -LX_EFAULT; int n=ux_readlink(pa,(char*)a2,(int)(a3>LX_PATH?LX_PATH:a3)); if(n<0)return lx_exists(pa)?-LX_EINVAL:-LX_ENOENT; return n; }
    case 90: PATH1(LX_AT_FDCWD,a1,0); if(!lx_exists(pa))return -LX_ENOENT; return ux_chmod(pa,(unsigned)a2)?-LX_EPERM:0;
    case 91: return lx_fchmod((int)a1,(int)a2);
    case 92: case 94: PATH1(LX_AT_FDCWD,a1,0); if(!lx_exists(pa)){ int isd; if(ux_lookup(pa,&isd,0)<0)return -LX_ENOENT; } return ux_chown(pa,(int)a2,(int)a3,nr==92)?-LX_EPERM:0;
    case 93: return lx_vf((int)a1)?0:-LX_EBADF;
    case 95: { int old=g_umask; g_umask=(int)a1&0777; return old; }
    case 96: { if(a1){ u64 ns=lx_now_ns(); u64 tv[2]={ns/1000000000ULL,(ns%1000000000ULL)/1000}; if(lx_put(a1,tv,16))return -LX_EFAULT; } if(a2){ u32 tz[2]={0,0}; if(lx_put(a2,tz,8))return -LX_EFAULT; } return 0; }
    case 97: return lx_rlimit((int)a1,a2);
    case 98: { u8 z[144]; lx_zero(z,144); *(u64*)z=g_ticks/100; *(u64*)(z+8)=(g_ticks%100)*10000; return lx_put(a2,z,144); }
    case 99: return lx_sysinfo(a1);
    case 100: { if(a1){ u64 t[4]={g_ticks,0,0,0}; if(lx_put(a1,t,32))return -LX_EFAULT; } return (long)g_ticks; }
    case 102: case 107: return g_uid;
    case 104: case 108: return g_gid;
    case 105: { long v=lx_setid(&g_uid,(int)a1); if(!v)p->uid=g_uid; return v; }
    case 106: { long v=lx_setid(&g_gid,(int)a1); if(!v)p->gid=g_gid; return v; }
    case 113: case 117: { long v=lx_setid(&g_uid,(int)a2); if(!v)p->uid=g_uid; return v; }
    case 114: case 119: { long v=lx_setid(&g_gid,(int)a2); if(!v)p->gid=g_gid; return v; }
    case 118: case 120: { int v=nr==118?g_uid:g_gid; int o[3]={v,v,v}; if(a1&&lx_put(a1,o,4))return -LX_EFAULT; if(a2&&lx_put(a2,o+1,4))return -LX_EFAULT; if(a3&&lx_put(a3,o+2,4))return -LX_EFAULT; return 0; }
    case 115: return 0;
    case 116: return g_uid==0?0:-LX_EPERM;
    case 109: { struct jproc* P=jc_lead(p); int pid=(int)a1?(int)a1:P->pid, pg=(int)a2?(int)a2:pid; if(pg<0)return -LX_EINVAL; int t=jc_idx(pid);
        if(t<0)return -LX_ESRCH; t=g_jp[t].tg; if(t!=p->tg&&g_jp[t].ppid!=P->pid)return -LX_ESRCH; if(g_jp[t].sid!=P->sid)return -LX_EPERM; g_jp[t].pgid=pg; jc_tg_sync_ids(t); return 0; }
    case 110: return jc_lead(p)->ppid;
    case 111: return jc_lead(p)->pgid;
    case 112: { struct jproc* P=jc_lead(p); if(P->pgid==P->pid)return -LX_EPERM; P->sid=P->pid; P->pgid=P->pid; jc_tg_sync_ids(p->tg); return P->pid; }
    case 121: { int pid=(int)a1?(int)a1:jc_lead(p)->pid; int t=jc_idx(pid); return t<0?-LX_ESRCH:jc_lead(&g_jp[t])->pgid; }
    case 124: { int pid=(int)a1?(int)a1:jc_lead(p)->pid; int t=jc_idx(pid); return t<0?-LX_ESRCH:jc_lead(&g_jp[t])->sid; }
    case 127: { u64 o=lx_k2l(p->sigpend&p->sigmask); return lx_put(a1,&o,8); }
    case 130: { if(a2!=8)return -LX_EINVAL; u64 m; if(lx_get(&m,a1,8))return -LX_EFAULT;
        p->suspold=p->sigmask; p->susp=1; p->sigmask=lx_l2k(m)&~((1u<<9)|(1u<<19));
        r->rax=(u64)-LX_EINTR; jc_block(r,S_SLEEP,g_ticks+0x7FFFFFFFFFFFULL); }
    case 131: { if(a2){ u64 o[3]={0,2,0}; if(lx_put(a2,o,24))return -LX_EFAULT; } return 0; }
    case 132: PATH1(LX_AT_FDCWD,a1,0); return lx_utimens(pa,0);
    case 133: return -LX_EPERM;
    case 135: return 0;
    case 137: PATH1(LX_AT_FDCWD,a1,0); if(!lx_exists(pa))return -LX_ENOENT; return lx_statfs(a2);
    case 138: if(!lx_vf((int)a1))return -LX_EBADF; return lx_statfs(a2);
    case 140: return 20;
    case 141: return 0;
    case 142: case 144: case 145: case 146: case 147: return 0;
    case 143: { int z=0; return lx_put(a2,&z,4); }
    case 148: return -LX_ENOSYS;
    case 149: case 150: case 151: case 152: return 0;
    case 157: if(a1==15){ int n=lx_str(a2,pa,16); if(n<0)return n; int i=0; for(;pa[i]&&i<15;i++)p->cmd[i]=pa[i]; p->cmd[i]=0; return 0; }
        if(a1==16){ char nm[16]; lx_zero(nm,16); for(int i=0;p->cmd[i]&&i<15;i++)nm[i]=p->cmd[i]; return lx_put(a2,nm,16); }
        if(a1==2||a1==37||a1==39){ int z=0; if(a1==39)return 0; return lx_put(a2,&z,4); }
        if(a1==3)return 1;
        if(a1==30)return 50000;
        if(a1==1||a1==4||a1==7||a1==8||a1==21||a1==23||a1==29||a1==36||a1==38||a1==47||a1==52||a1==0x59616d61ULL||a1==0x53564d41ULL)return 0;
        lx_gap(6,a1); return -LX_EINVAL;
    case 158: if(a1==0x1002){ if(a2>=0x800000000000ULL)return -LX_EPERM; p->fsbase=a2; lsys_fsbase=a2; cpu_set_fsbase(a2); return 0; }
        if(a1==0x1003)return lx_put(a2,&p->fsbase,8);
        if(a1==0x1001){ if(a2>=0x800000000000ULL)return -LX_EPERM; p->gsbase=a2; cpu_wrmsr(0xC0000102u,a2); return 0; }
        if(a1==0x1004)return lx_put(a2,&p->gsbase,8);
        if(a1==0x1011)return 1;
        return -LX_EINVAL;
    case 160: return 0;
    case 161: return -LX_EPERM;
    case 162: for(int k=0;k<FS_N;k++)g_fs_dirty[k]=0; nvx3_sync_all(); return 0;
    case 165: { int n=lx_str(a2,pa,LX_PATH); if(n<0)return n; return ux_mount("",pa)?-LX_EPERM:0; }
    case 166: { int n=lx_str(a1,pa,LX_PATH); if(n<0)return n; return ux_umount(pa)?-LX_EINVAL:0; }
    case 169: return -LX_EPERM;
    case 170: { if(g_uid!=0)return -LX_EPERM; if(a2>31)return -LX_EINVAL; char h[32]; if(lx_get(h,a1,a2))return -LX_EFAULT; h[a2]=0; for(u64 i=0;i<=a2;i++)g_hostname[i]=h[i]; return 0; }
    case 186: return p->pid;
    case 191: case 192: case 193: case 194: case 195: case 196: case 197: case 198: case 199: return -LX_ENOTSUP;
    case 201: { u64 t=lx_now_ns()/1000000000ULL; if(a1&&lx_put(a1,&t,8))return -LX_EFAULT; return (long)t; }
    case 202: return lx_futex(r,a1,(int)a2,(u32)a3,a4,a5,(u32)a6);
    case 217: return lx_getdents((int)a1,a2,a3);
    case 218: p->ctid=a1; return p->pid;
    case 221: return 0;
    case 228: return lx_clock((int)a1,a2);
    case 229: { if(a2){ u64 t[2]={0,hr_precise()?1:10000000}; if(lx_put(a2,t,16))return -LX_EFAULT; } return 0; }
    case 230: return lx_clock_nanosleep(r,(int)a1,(int)a2,a3);
    case 257: return lx_open((int)a1,a2,(int)a3,(int)a4);
    case 258: PATH1((int)a1,a2,0); return lx_mkdir(pa,(int)a3);
    case 260: PATH1((int)a1,a2,0); if(!lx_exists(pa))return -LX_ENOENT; return ux_chown(pa,(int)a3,(int)a4,!((int)a5&0x100))?-LX_EPERM:0;
    case 262: { int fl=(int)a4; if(fl&0x1000){ if(!a2)return lx_fstat((int)a1,a3); char t1[4]; int n=lx_str(a2,t1,4); if(n==0)return lx_fstat((int)a1,a3); }
        PATH1((int)a1,a2,0); return lx_stat_path(pa,a3,!(fl&0x100)); }
    case 263: PATH1((int)a1,a2,0); return ((int)a3&0x200)?lx_rmdir(pa):lx_unlink(pa);
    case 264: case 316: if(nr==316&&a5)return -LX_EINVAL; PATH1((int)a1,a2,0); PATH2((int)a3,a4); return lx_rename(pa,pb);
    case 265: PATH1((int)a1,a2,0); PATH2((int)a3,a4); { int L=lx_slen(pb), c=0; while(L>0&&pb[L-1]!='/'){ L--; c++; } if(c>51)return -LX_ENAMETOOLONG; } if(!lx_exists(pa))return -LX_ENOENT; if(lx_exists(pb))return -LX_EEXIST; return ux_link(pa,pb)?-LX_EPERM:0;
    case 266: { int n=lx_str(a1,pa,LX_PATH); if(n<0)return n; PATH2((int)a2,a3); if(lx_exists(pb))return -LX_EEXIST; return ux_symlink(pa,pb)?-LX_ENOSPC:0; }
    case 267: { char t[64]; if(lx_str(a2,t,64)>0){ int pe=lx_proc_exe(t,a3,a4); if(pe!=-9999)return pe; } } PATH1((int)a1,a2,0); { if((long)a4<=0)return -LX_EINVAL; if(!lx_okw(a3,a4))return -LX_EFAULT; int n=ux_readlink(pa,(char*)a3,(int)(a4>LX_PATH?LX_PATH:a4)); if(n<0)return lx_exists(pa)?-LX_EINVAL:-LX_ENOENT; return n; }
    case 268: PATH1((int)a1,a2,0); if(!lx_exists(pa))return -LX_ENOENT; return ux_chmod(pa,(unsigned)a3)?-LX_EPERM:0;
    case 269: case 439: PATH1((int)a1,a2,0); return lx_access(pa,(int)a3);
    case 270: { long ms=-1; if(a5){ u64 ts[2]; if(lx_get(ts,a5,16))return -LX_EFAULT; if((i64)ts[0]<0||ts[1]>=1000000000ULL)return -LX_EINVAL; if(ts[0]>100000000ULL)ts[0]=100000000ULL; ms=(long)(ts[0]*1000+ts[1]/1000000); } return lx_select(r,(int)a1,a2,a3,a4,ms); }
    case 271: { long ms=-1; if(a3){ u64 ts[2]; if(lx_get(ts,a3,16))return -LX_EFAULT; if((i64)ts[0]<0||ts[1]>=1000000000ULL)return -LX_EINVAL; if(ts[0]>100000000ULL)ts[0]=100000000ULL; ms=(long)(ts[0]*1000+(ts[1]+999999)/1000000); } return lx_poll(r,a1,a2,ms); }
    case 273: if(a2!=24)return -LX_EINVAL; p->robust=a1; return 0;
    case 274: { int t=a1?jc_idx((int)a1):g_jcur; if(t<0)return -LX_ESRCH; u64 h=g_jp[t].robust, L=24; if(lx_put(a2,&h,8)||lx_put(a3,&L,8))return -LX_EFAULT; return 0; }
    case 280: { if(!a2){ struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; return 0; } PATH1((int)a1,a2,0); return lx_utimens(pa,a3); }
    case 285: { struct vfile* v=lx_vf((int)a1); if(!v)return -LX_EBADF; if((long)a3<0||(long)a4<=0)return -LX_EINVAL; if(a2&~1ULL)return -LX_ENOTSUP; if(a2&1)return 0; u64 end=a3+a4;
        if(v->dev==LX_MFD){ struct memfd* m=&g_mfd[v->fs_idx]; if(end<=m->size)return 0; if(m->seals&4)return -LX_EPERM; return mfd_truncate(v->fs_idx,end); }
        if(v->dev==0&&v->fs_idx>=0){ if((v->flags&3)==0)return -LX_EBADF; if(end<=(u64)FS[v->fs_idx].len)return 0; if(end>=(u64)FS_CAP)return -27; return k_ftruncate((int)a1,(long)end)<0?-LX_EIO:0; }
        return -LX_ENOTSUP; }
    case 292: return lx_dup3((int)a1,(int)a2,(int)a3|0x40000000);
    case 293: return lx_pipe(a1,(int)a2);
    case 302: { if(a1&&(int)a1!=jc_lead(p)->pid&&(int)a1!=p->pid)return -LX_ESRCH; if(a4){ long v=lx_rlimit((int)a2,a4); if(v)return v; } return 0; }
    case 318: return lx_getrandom(a1,a2);
    case 334: return -LX_ENOSYS;
    case 436: { u32 lo=(u32)a1, hi=(u32)a2; if(lo>hi)return -LX_EINVAL; for(u32 f=lo;f<=hi&&f<FDMAX;f++){ if(!lx_vf((int)f))continue; if(a3&4)fdce()[f]=1; else lx_close((int)f); } return 0; }
    }
    enosys_note(nr);
    return -LX_ENOSYS;
    #undef PATH1
    #undef PATH2
}
int g_lxtrace=0;
void linux_syscall(struct sysregs* r){
    if(!g_js_running||g_jcur<0){
        if(r->rax==60||r->rax==231){ r->rbx=r->rdi; r->rax=0; syscall_dispatch(r); return; }
        if(r->rax==1&&(r->rdi==1||r->rdi==2)&&r->rdx<4096&&jc_umapped(r->rsi,r->rdx)){ const char* s=(const char*)r->rsi; u64 n=r->rdx; for(u64 i=0;i<n&&g_r3out_n<255;i++)g_r3out[g_r3out_n++]=s[i]; g_r3out[g_r3out_n]=0; r->rax=n; return; }
        r->rax=(u64)-LX_ENOSYS; return; }
    struct jproc* p=&g_jp[g_jcur]; p->lx=1;
    if(PCPU->tlb_pending){ PCPU->tlb_pending=0; cpu_set_pt(cpu_get_pt()); }
    struct jproc* P=jc_lead(p);
    g_dcwd=P->cwd; g_umask=P->umask; g_ubrk=P->brk; g_mmap_next=P->mmnext;
    u64 tnr=r->rax, ta1=r->rdi, ta2=r->rsi;
    long v=lx_dispatch(r);
    lx_sc_note((u32)tnr,g_jp[g_jcur].pid,ta1,ta2,v);
    if(g_lxtrace>0){ g_lxtrace--; char b[120],*q=b; q=scopy(q,"sc "); q=udec(q,(u32)tnr); q=scopy(q," "); q=uhex(q,ta1,8); q=scopy(q," "); q=uhex(q,ta2,8); q=scopy(q," = "); if(v<0){ *q++='-'; q=udec(q,(u32)(-v)); } else q=uhex(q,(u64)v,8); *q=0; klog(b); }
    p=&g_jp[g_jcur]; P=jc_lead(p);
    P->cwd=g_dcwd; P->umask=g_umask; P->brk=g_ubrk; P->mmnext=g_mmap_next;
    r->rax=(u64)v;
    if(jc_deliverable(p))jc_yield_sys(r);
}
