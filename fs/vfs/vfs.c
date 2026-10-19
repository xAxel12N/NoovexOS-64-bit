#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/arch/x86_64/user/ring3.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/sys/linux/lxsock.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/arch/x86_64/interrupts/interrupts.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/core/sound.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/lib/crypto/nvxcrypto2.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/apps/system/files_classic.h"
/* ================= VFS layer: pluggable filesystems + mount table =================
   A uniform interface so device files, NVXFS, and synthetic filesystems (/proc)
   plug in the same way. Each filesystem registers vfs_fs_ops; the mount table
   routes a path prefix to a filesystem. The legacy in-memory FS[] store remains
   the default ("rootfs"); this layer adds structure and makes /proc possible. */

#define VFS_MAXFS 8
static const vfs_fs_ops* g_vfs[VFS_MAXFS]; static int g_vfs_n=0;
void vfs_register(const vfs_fs_ops* fs){ if(g_vfs_n<VFS_MAXFS)g_vfs[g_vfs_n++]=fs; }
const vfs_fs_ops* vfs_lookup(const char* path){ for(int i=0;i<g_vfs_n;i++) if(g_vfs[i]->match&&g_vfs[i]->match(path))return g_vfs[i]; return 0; }

/* ---- procfs: synthetic files exposing live kernel state ---- */
static int procfs_build(const char* p,char* out,int cap);
static char g_procbuf[4096];
static int procfs_match(const char* p){ if(!(p[0]=='/'&&p[1]=='p'&&p[2]=='r'&&p[3]=='o'&&p[4]=='c'&&(p[5]=='/'||p[5]==0)))return 0; return procfs_build(p,g_procbuf,4096)>=0; }
static int procfs_build(const char* p,char* out,int cap){
    char* o=out; const char* leaf=p+5; while(*leaf=='/')leaf++;
    #define PW(str) do{ const char* z=str; while(*z&&(o-out)<cap-1)*o++=*z++; }while(0)
    #define PN(n) do{ char nb[16]; int L=0; unsigned v=(unsigned)(n); if(!v)nb[L++]='0'; char t[16]; int ti=0; while(v){t[ti++]='0'+v%10;v/=10;} while(ti)nb[L++]=t[--ti]; for(int q=0;q<L&&(o-out)<cap-1;q++)*o++=nb[q]; }while(0)
    if(seq((char*)leaf,"meminfo")){ PW("MemTotal: "); PN(g_ram_mb*1024); PW(" kB\nMemArch: x86-64\n"); }
    else if(seq((char*)leaf,"uptime")){ PN((unsigned)(g_ticks/100)); PW(" ticks\n"); }
    else if(seq((char*)leaf,"version")){ PW("NoovexOS 64 v9.83 (from-scratch, Unix-like)\n"); }
    else if(seq((char*)leaf,"cpuinfo")){ for(int ci=0;ci<NCPU;ci++){ if(!g_cpu[ci].online)continue; PW("processor\t: "); PN(ci); PW("\napicid\t\t: "); PN(g_cpu[ci].apic_id); PW("\narch\t\t: x86-64 long mode\n\n"); } PW("scheduling\t: "); PW(g_smp_on?"all cores\n":"core 0 only\n"); }
    else if(seq((char*)leaf,"fb")){ PN(W); PW(" "); PN(H); PW(" 32 "); PN(W*4); PW("\n"); }
    else if(seq((char*)leaf,"mounts")){ for(int i=0;i<g_vfs_n;i++){ PW(g_vfs[i]->name); PW("\n"); } PW("rootfs\n"); }
    else if(!*leaf){ PW("meminfo\nuptime\nversion\ncpuinfo\nmounts\nfb\n"); }
    else { extern int proc_gen(const char*,char*,int); int n=proc_gen(p,out,cap); if(n<0||n>=cap)return -1; out[n]=0; return n; }
    *o=0; return (int)(o-out);
    #undef PW
    #undef PN
}
static int procfs_read(const char* path,char* buf,int off,int len){ int n=procfs_build(path,g_procbuf,4096); if(n<0)return -1; if(off<0||off>=n)return 0; int c=n-off; if(c>len)c=len; for(int i=0;i<c;i++)buf[i]=g_procbuf[off+i]; return c; }
static int procfs_size(const char* path){ return procfs_build(path,g_procbuf,4096); }
static const vfs_fs_ops PROCFS={ "procfs", procfs_match, procfs_read, procfs_size };

extern const vfs_fs_ops SYSROOTFS;
void vfs_init(void){ vfs_register(&PROCFS); sysroot_init(); vfs_register(&SYSROOTFS); }

int vfs_open(const char* nm,int flags){
    fd_init();
    const vfs_fs_ops* fs=vfs_lookup(nm);
    if(fs){ int vi=-1; for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used){vi=i;break;} if(vi<0)return -1;
        g_vf[vi].used=1; g_vf[vi].fs_idx=-1; g_vf[vi].off=0; g_vf[vi].flags=flags; g_vf[vi].dev=100; g_vf[vi].vfs=fs;
        for(int k=0;k<127;k++){ g_vf[vi].vpath[k]=nm[k]; if(!nm[k])break; } g_vf[vi].vpath[127]=0;
        int fd=-1; int* T=fdt(); for(int i=(g_jcur>=0?0:3);i<FDMAX;i++) if(T[i]==-1){fd=i;break;} if(fd<0){g_vf[vi].used=0;return -1;}
        g_vf[vi].ref=1; T[fd]=vi; fdce()[fd]=(flags&0x80000)?1:0; return fd; }
    int dv=dev_id(nm);
    if(dv){ int vi=-1; for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used){vi=i;break;} if(vi<0)return -1;
        g_vf[vi].used=1; g_vf[vi].fs_idx=-1; g_vf[vi].off=0; g_vf[vi].flags=flags; g_vf[vi].dev=dv; if(dv==9)g_fb0_open++; if(dv==10)g_mouse_open++;
        int fd=-1; int* T=fdt(); for(int i=(g_jcur>=0?0:3);i<FDMAX;i++) if(T[i]==-1){fd=i;break;} if(fd<0){g_vf[vi].used=0;return -1;}
        g_vf[vi].ref=1; T[fd]=vi; fdce()[fd]=(flags&0x80000)?1:0; return fd; }
    int isd=0; int idx=ux_lookup(nm,&isd,1); if(isd)return -1;
    if(idx>=0&&(flags&O_CREAT)&&(flags&0x80))return -1;
    int created_now=-1; if(idx<0){ if(!(flags&O_CREAT))return -1; int pd; char bn[64]; if(k_split(nm,&pd,bn)<0||!bn[0])return -1; if(!perm_dir(pd))return -1; created_now=1;
        for(int k=0;k<FS_N;k++) if(!FS[k].used){ idx=k; fs_newslot(k); int n=0; while(bn[n]&&n<63){FS[k].name[n]=bn[n];n++;} FS[k].name[n]=0; FS[k].used=1; FS[k].folder=0; FS[k].dir=pd; FS[k].mode=0100644u; FS[k].uid=g_uid; FS[k].gid=g_uid; FS[k].nlink=1; break; } if(idx<0)return -1; }
    if(created_now>=0){ FS[idx].mode=0100000u|(0666u&~(unsigned)g_umask); ux_touch(idx,1); FS[idx].atime=FS[idx].mtime; }
    else { int acc=flags&3; if(acc!=1&&!perm_file(idx,4))return -1; if((acc!=0||(flags&O_TRUNC))&&!perm_file(idx,2))return -1; }
    if((flags&O_TRUNC)&&created_now<0&&FS[idx].len){ if(fs_truncate_to(idx,0))return -1; ux_touch(idx,1); g_fs_dirty[idx]=1; }
    if(created_now>=0){ g_fs_dirty[idx]=1; if(pd_touch(idx))return -1; }
    int vi=-1; for(int i=0;i<VMAXOPEN;i++) if(!g_vf[i].used){vi=i;break;} if(vi<0)return -1;
    g_vf[vi].used=1; g_vf[vi].fs_idx=idx; g_vf[vi].off=(flags&O_APPEND)?FS[idx].len:0; g_vf[vi].flags=flags; g_vf[vi].dev=0;
    int fd=-1; int* T=fdt(); for(int i=(g_jcur>=0?0:3);i<FDMAX;i++) if(T[i]==-1){fd=i;break;} if(fd<0){g_vf[vi].used=0;return -1;}
    g_vf[vi].ref=1; T[fd]=vi; fdce()[fd]=(flags&0x80000)?1:0; return fd;
}


int vfs_read(int fd,char* buf,int len){ fd_init(); int* T=fdt(); if(fd<0||fd>=FDMAX)return -1; if(T[fd]==-2)return 0; if(T[fd]<0)return -1;
    struct vfile* v=&g_vf[T[fd]];
    if(v->dev==5&&g_js_running) return jc_tty_read(buf,len);
    if(v->dev==6) return jc_pipe_read(v,buf,len);
    if(v->dev==9||v->dev==10||v->dev==16) return ux_dev_read(v,buf,len);
    if(v->dev==7) return -1;
    if(v->dev==14){ int n=lxs_recv(v->fs_idx,(u8*)buf,len,0,0,0); return n==JS_BLOCK?n:(n<0?-1:n); }
    if(v->dev==12) return snd_dev_read(buf,len);
    if(v->dev==100 && v->vfs){ int n=v->vfs->read(v->vpath,buf,v->off,len); if(n>0)v->off+=n; return n<0?0:n; }
    if(v->dev){ switch(v->dev){
        case 1: return 0;                                  /* /dev/null: EOF */
        case 2: case 4: for(int i=0;i<len;i++)buf[i]=0; return len;   /* /dev/zero, /dev/full read as zeros */
        case 3: if(len>0)csprng_bytes((u8*)buf,len); return len; /* /dev/random, /dev/urandom */
        case 5: return 0;                                  /* /dev/tty read: nothing queued */
        default: return 0; } }
    if(v->fs_idx<0||!FS[v->fs_idx].used||v->off<0)return 0;
    int avail=FS[v->fs_idx].len-v->off; if(len>avail)len=avail; if(len<=0)return 0;
    if(fs_ensure(v->fs_idx))return -1;
    kmemcpy(buf,FS[v->fs_idx].data+v->off,(unsigned long)len); v->off+=len; return len; }
int vfs_write(int fd,const char* buf,int len){ fd_init();
    int* T=fdt(); if(fd<0||fd>=FDMAX)return -1;
    if(T[fd]==-2){ for(int i=0;i<len && g_r3out_n<255;i++)g_r3out[g_r3out_n++]=buf[i]; g_r3out[g_r3out_n]=0; return len; }
    if(T[fd]<0)return -1; struct vfile* v=&g_vf[T[fd]];
    if(v->dev==5&&g_js_running){ jc_tty_out(buf,len); return len; }
    if(v->dev==7) return jc_pipe_write(v,buf,len);
    if(v->dev==9||v->dev==10) return ux_dev_write(v,buf,len);
    if(v->dev==12) return snd_dev_write(buf,len);
    if(v->dev==14){ int n=lxs_send(v->fs_idx,(const u8*)buf,len,0,0,0); return n==JS_BLOCK?n:(n<0?-1:n); }
    if(v->dev==13){ char ln[160]; int o=0; for(int i=0;i<len;i++){ char c=buf[i]; if(c=='\n'||o>=159){ ln[o]=0; if(o)klog(ln); o=0; if(c!='\n')ln[o++]=c; } else if(c>=32&&c<127)ln[o++]=c; } if(o){ ln[o]=0; klog(ln); } return len; }
    if(v->dev==6) return -1;
    if(v->dev){ switch(v->dev){
        case 1: case 2: case 3: return len;
        case 4: return -1;
        case 5: for(int i=0;i<len && g_r3out_n<255;i++)g_r3out[g_r3out_n++]=buf[i]; g_r3out[g_r3out_n]=0; return len;
        default: return len; } }
    if(v->fs_idx<0||!FS[v->fs_idx].used) return -1;
    if(!perm_file(v->fs_idx,2)) return -1;
    if(v->flags&O_APPEND)v->off=FS[v->fs_idx].len;
    if(v->off<0||v->off>FS_CAP-1) return -1;
    if((long)v->off+len>FS_CAP-1)len=FS_CAP-1-v->off; if(len<=0)return 0;
    { int k=v->fs_idx; long L=FS[k].len, end=(long)v->off+len; long lo=v->off<L?v->off:L; char* d=fs_wr_range(k,lo,end); if(!d)return -1;
      for(long i=L;i<v->off;i++)d[i]=0; kmemcpy(d+v->off,buf,(unsigned long)len);
      v->off+=len; if(v->off>FS[k].len)FS[k].len=v->off; d[FS[k].len]=0; ux_touch(k,1); g_fs_dirty[k]=1; }
    return len; }
int vfs_lseek(int fd,int off,int whence){ fd_init(); int* T=fdt(); if(fd<0||fd>=FDMAX||T[fd]<0)return -1;
    struct vfile* v=&g_vf[T[fd]]; if(v->dev==9){ long tot=(long)W*H*4; long b=(whence==1)?v->off:(whence==2)?tot:0; v->off=(int)(b+off); if(v->off<0)v->off=0; return v->off; } if(v->dev||v->fs_idx<0)return -1; long base=(whence==1)?v->off:(whence==2)?FS[v->fs_idx].len:0; long no=base+(long)off; if(no<0)no=0; if(no>FS_CAP-1)return -1; v->off=(int)no; return v->off; }
int vfs_close(int fd){ fd_init(); int* T=fdt(); if(fd<0||fd>=FDMAX||T[fd]<0)return -1; if(g_jcur<0&&fd<3)return -1; vf_unref(T[fd]); T[fd]=-1; fdce()[fd]=0; return 0; }
int vfs_dup(int fd){ fd_init(); int* T=fdt(); if(fd<0||fd>=FDMAX||T[fd]==-1)return -1; int nfd=-1; for(int i=(g_jcur>=0?0:3);i<FDMAX;i++) if(T[i]==-1){nfd=i;break;} if(nfd<0)return -1; T[nfd]=T[fd]; if(T[fd]>=0)vf_ref(T[fd]); fdce()[nfd]=0; return nfd; }
int vfs_fstat(int fd){ fd_init(); int* T=fdt(); if(fd<0||fd>=FDMAX||T[fd]<0)return -1; struct vfile* v=&g_vf[T[fd]]; if(v->dev||v->fs_idx<0)return 0; return FS[v->fs_idx].len; }
    /* NVXFS persist (defined later) */
/* ported/expanded from the Noovex9 32-bit syscall ABI (INT 0x80, number in rax; args rbx,rcx,rdx,rsi,rdi) */
