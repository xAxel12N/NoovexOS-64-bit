#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/nvx/nvx4.h"
#include "include/nvx/fs/vfs/unixfs.h"
/* =========================================================================
   RAM file system
   ========================================================================= */

/* Unix mode bits (from V6 ino.h) */

#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_IRUSR  0000400
#define S_IWUSR  0000200
#define S_IXUSR  0000100

FsFile FS[FS_N];
static char g_fs_zero[65536];
void kmemcpy(void* d,const void* s,unsigned long n){ cpu_copy(d,s,n); }

static u64 g_fs_tick;
u8 g_fs_dirty[FS_N];

volatile int g_fs_wb_pending; u64 g_fs_wb_due;
void fs_wb_mark(int k){ (void)k; if(!g_fs_wb_pending){ g_fs_wb_pending=1; g_fs_wb_due=g_ticks+100; } }

void fs_free(int k){ if(k<0||k>=FS_N)return; if(FS[k].data)kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; FS[k].lazy=0; FS[k].clean=0; FS[k].dlo=0; FS[k].dhi=0; FS[k].orphan=0; }
static long fs_evict(long want,int keep){
    long got=0;
    for(int pass=0;pass<2&&got<want;pass++)
    for(int k=0;k<FS_N&&got<want;k++){
        if(k==keep||!FS[k].used||!FS[k].data||FS[k].lazy||!FS[k].clean)continue;
        if(FS[k].tick+(pass==0?64u:8u)>g_fs_tick)continue;
        if(fs_is_open(k)||!fs_reloadable(k))continue;
        got+=FS[k].cap; kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; FS[k].lazy=1; }
    return got;
}
static void* fs_alloc(int k,long n){ void* p=kmalloc((unsigned long)n); if(!p&&fs_evict(n+4096,k)>0)p=kmalloc((unsigned long)n); return p; }
int fs_reserve(int k,long need){
    if(k<0||k>=FS_N||need<0||need>FS_CAP)return -1;
    if(FS[k].data&&FS[k].cap>need)return 0;
    long nc=FS[k].cap?(long)FS[k].cap+(long)FS[k].cap/2:64; if(nc<need+1)nc=need+1; if(nc>(long)FS_CAP+1)nc=(long)FS_CAP+1; nc=(nc+63)&~63L;
    char* p=(char*)fs_alloc(k,nc); if(!p&&nc>need+1){ nc=(need+64)&~63L; p=(char*)fs_alloc(k,nc); } if(!p)return -1;
    int L=FS[k].data?FS[k].len:0; if(L>nc-1)L=(int)nc-1; if(FS[k].data){ kmemcpy(p,FS[k].data,(unsigned long)L); kfree(FS[k].data); }
    p[L]=0; FS[k].data=p; FS[k].cap=(int)nc; return 0;
}
int fs_ensure(int k){
    if(k<0||k>=FS_N)return -1; FS[k].tick=++g_fs_tick;
    if(FS[k].lazy){ if(fs_backend_load(k))return -1; FS[k].lazy=0; FS[k].clean=1; }
    if(!FS[k].data){ if(fs_reserve(k,FS[k].len>0?FS[k].len:0))return -1; for(int i=0;i<=FS[k].len;i++)FS[k].data[i]=0; }
    return 0;
}
char* fsd(int k){ if(fs_ensure(k))return g_fs_zero; return FS[k].data; }
static void fs_dirty(int k,long lo,long hi){ if(k<0||k>=FS_N)return; if(FS[k].clean||FS[k].dlo>=FS[k].dhi){ FS[k].dlo=(int)lo; FS[k].dhi=(int)hi; } else { if(lo<FS[k].dlo)FS[k].dlo=(int)lo; if(hi>FS[k].dhi)FS[k].dhi=(int)hi; } FS[k].clean=0; }
char* fs_wr(int k,long need){ if(fs_ensure(k)||fs_reserve(k,need))return 0; fs_dirty(k,0,0x7FFFFFFF); return FS[k].data; }
char* fs_wr_range(int k,long lo,long hi){ if(fs_ensure(k)||fs_reserve(k,hi))return 0; fs_dirty(k,lo,hi); return FS[k].data; }
int fs_set(int k,const char* src,long n){
    if(k<0||k>=FS_N||n<0||n>FS_CAP-1)return -1;
    if(FS[k].data&&FS[k].cap>n+(long)FS[k].cap/2+65536L){ kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; }
    FS[k].lazy=0; int ol=FS[k].len; FS[k].len=0; if(fs_reserve(k,n)){ FS[k].len=FS[k].data?ol:0; if(!FS[k].data)FS[k].len=0; return -1; }
    if(src&&n)kmemcpy(FS[k].data,src,(unsigned long)n); else if(n)for(long i=0;i<n;i++)FS[k].data[i]=0;
    FS[k].data[n]=0; FS[k].len=(int)n; fs_dirty(k,0,0x7FFFFFFF); return 0;
}
int fs_truncate_to(int k,long n){
    if(k<0||k>=FS_N||n<0||n>FS_CAP-1)return -1;
    if(n==0){ if(FS[k].data&&FS[k].cap>65536){ kfree(FS[k].data); FS[k].data=0; FS[k].cap=0; } FS[k].lazy=0; FS[k].len=0; if(FS[k].data)FS[k].data[0]=0; fs_dirty(k,0,0x7FFFFFFF); return 0; }
    if(fs_ensure(k)||fs_reserve(k,n))return -1;
    for(long i=FS[k].len;i<n;i++)FS[k].data[i]=0; FS[k].data[n]=0; long lo=n<FS[k].len?n:FS[k].len; FS[k].len=(int)n; fs_dirty(k,lo,0x7FFFFFFF); return 0;
}
/* ================= POSIX RUNG 1: fd table + VFS ================= */

            /* dev: 0=file 1=null 2=zero 3=random 4=full 5=tty/console */
struct vfile g_vf[VMAXOPEN];
int g_fd[FDMAX]; static int g_fd_init=0;   /* fd -> vfile index; -1 free, -2 = std stream (0/1/2) */
 
void fd_init(void){ if(g_fd_init)return; g_fd_init=1; for(int i=0;i<FDMAX;i++)g_fd[i]=-1; for(int i=0;i<VMAXOPEN;i++)g_vf[i].used=0; g_fd[0]=-2; g_fd[1]=-2; g_fd[2]=-2; }
int fs_find_name(const char* nm){ for(int k=0;k<FS_N;k++) if(FS[k].used){ int eq=1; for(int c=0;c<64;c++){ char a=FS[k].name[c],b=nm[c]; if(a!=b){eq=0;break;} if(!a&&!b)break; } if(eq)return k; } return -1; }
int dev_id(const char* nm){
    if(nm[0]=='/'&&nm[1]=='d'&&nm[2]=='e'&&nm[3]=='v'&&nm[4]=='/'){ const char* d=nm+5;
        if(seq((char*)d,"null"))return 1; if(seq((char*)d,"zero"))return 2;
        if(seq((char*)d,"random")||seq((char*)d,"urandom"))return 3; if(seq((char*)d,"full"))return 4;
        if(seq((char*)d,"tty")||seq((char*)d,"console"))return 5;
        if(seq((char*)d,"fb0"))return 9; if(seq((char*)d,"dsp")||seq((char*)d,"audio"))return 12; if(seq((char*)d,"kmsg"))return 13; if(seq((char*)d,"media"))return 15; if(seq((char*)d,"input/mouse")||seq((char*)d,"input/mice"))return 10; if(seq((char*)d,"nvxxin"))return 16; }
    return 0;
}
