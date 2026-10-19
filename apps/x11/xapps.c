#include "include/nvx/base.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/drivers/video/fb/framebuffer.h"
#include "include/nvx/graphics/theme/theme.h"
#include "include/nvx/fs/vfs/sysroot.h"
#include "include/nvx/apps/internet/browser.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/ramfs.h"

#define XA_MAX 32
#define ICS 48
typedef struct { char name[48]; char exec[160]; char icon[96]; int term; u32* pix; int pixok; } xapp;
static xapp g_xa[XA_MAX]; static int g_nxa; static int g_xa_files=-1;
static u32* g_idle_pix; static int g_idle_ok;
extern void x_launch(const char* prog);
extern void run_cmd(char* line);

static int xs_len(const char* s){ int n=0; while(s[n])n++; return n; }
static int xs_pre(const char* s,const char* p){ while(*p){ if(*s!=*p)return 0; s++; p++; } return 1; }
int g_apps_gen;
static const u8* anyread(const char* path,u32* len){ const u8* d=sysroot_get(path,len,0); if(d)return d; int k=k_find_file(path); if(k>=0){ *len=(u32)FS[k].len; return (const u8*)fsd(k); } return 0; }
static u32* load_png_scaled(const char* path){
    u32 L=0; const u8* d=anyread(path,&L); if(!d||L<8||d[0]!=0x89||d[1]!='P')return 0;
    int cap=1024*1024*4; u8* tmp=(u8*)kmalloc((unsigned long)cap); if(!tmp)return 0; int w=0,h=0;
    if(png_decode(d,(int)L,PNG_RAW,PNG_RAW_SZ,tmp,cap,&w,&h)!=0||w<=0||h<=0||w>1024||h>1024){ kfree(tmp); return 0; }
    u32* o=(u32*)kmalloc(ICS*ICS*4); if(!o){ kfree(tmp); return 0; }
    for(int y=0;y<ICS;y++) for(int x=0;x<ICS;x++){ int sx=x*w/ICS, sy=y*h/ICS; const u8* p=tmp+((long)sy*w+sx)*4; o[y*ICS+x]=((u32)p[3]<<24)|((u32)p[0]<<16)|((u32)p[1]<<8)|p[2]; }
    kfree(tmp); return o; }
static void find_icon(xapp* a){
    a->pixok=1; if(!a->icon[0])return;
    if(a->icon[0]=='/'){ a->pix=load_png_scaled(a->icon); return; }
    static const char* const dirs[]={"/usr/share/icons/hicolor/48x48/apps/","/usr/share/icons/hicolor/64x64/apps/","/usr/share/icons/hicolor/128x128/apps/","/usr/share/icons/hicolor/32x32/apps/","/usr/share/icons/hicolor/256x256/apps/","/usr/share/pixmaps/",0};
    for(int i=0;dirs[i]&&!a->pix;i++){ char p[200]; char* q=p; q=scopy(q,dirs[i]); q=scopy(q,a->icon); q=scopy(q,".png"); *q=0; a->pix=load_png_scaled(p); } }
static void parse_desktop(const char* path){
    if(g_nxa>=XA_MAX)return; u32 L=0; const u8* d=anyread(path,&L); if(!d)return;
    xapp a; for(int i=0;i<(int)sizeof(a);i++)((char*)&a)[i]=0; int in=0, nodisp=0, app=1;
    const char* s=(const char*)d; const char* e=s+L;
    while(s<e){ const char* l=s; while(s<e&&*s!='\n')s++; int n=(int)(s-l); if(s<e)s++; if(n<=0)continue;
        if(l[0]=='['){ in=(n>=15&&xs_pre(l,"[Desktop Entry]")); continue; } if(!in)continue;
        char* dst=0; int cap=0; int k=0;
        if(xs_pre(l,"Name=")){ dst=a.name; cap=48; k=5; } else if(xs_pre(l,"Exec=")){ dst=a.exec; cap=160; k=5; } else if(xs_pre(l,"Icon=")){ dst=a.icon; cap=96; k=5; }
        else if(xs_pre(l,"NoDisplay=true"))nodisp=1; else if(xs_pre(l,"Terminal=true"))a.term=1; else if(xs_pre(l,"Type=")&&!xs_pre(l,"Type=Application"))app=0;
        if(dst){ int o=0; for(int i=k;i<n&&o<cap-1;i++){ if(dst==a.exec&&l[i]=='%'&&i+1<n){ i++; continue; } dst[o++]=l[i]; } while(o>0&&(dst[o-1]==' '||dst[o-1]=='\r'))o--; dst[o]=0; } }
    if(!a.name[0]||!a.exec[0]||nodisp||!app)return;
    for(int i=0;i<g_nxa;i++){ int same=1; for(int k=0;;k++){ if(g_xa[i].name[k]!=a.name[k]){ same=0; break; } if(!a.name[k])break; } if(same)return; }
    g_xa[g_nxa++]=a; }
void xapps_refresh(void){
    int files=0, layers=0; sysroot_stats(&files,&layers); int key=files*131071+g_apps_gen; if(key==g_xa_files)return; g_xa_files=key;
    for(int i=0;i<g_nxa;i++) if(g_xa[i].pix)kfree(g_xa[i].pix); g_nxa=0;
    char nm[96]; int isd=0;
    for(int i=0;i<256&&sysroot_list("/usr/share/applications",i,nm,96,&isd);i++){ int L=xs_len(nm); if(isd||L<9)continue; if(!xs_pre(nm+L-8,".desktop"))continue; if(xs_pre(nm,"python3-ide"))continue;
        char p[160]; char* q=p; q=scopy(q,"/usr/share/applications/"); q=scopy(q,nm); *q=0; parse_desktop(p); }
    { int t=0; for(int i=0;i<256&&k_readdir_at("/usr/share/applications",i,nm,&t)==1;i++){ int L=xs_len(nm); if(t==4||L<9)continue; if(!xs_pre(nm+L-8,".desktop"))continue; if(xs_pre(nm,"python3-ide"))continue;
        char p[160]; char* q=p; q=scopy(q,"/usr/share/applications/"); q=scopy(q,nm); *q=0; parse_desktop(p); } } }
int xapps_count(void){ xapps_refresh(); return g_nxa; }
const char* app_name(int id){ if(id==1000)return "Python IDE"; if(id==1001)return "IDLE (Python 3.13)"; if(id>=1100&&id<1100+g_nxa)return g_xa[id-1100].name; return "?"; }
static u32* app_pix(int id){
    if(id==1001){ if(!g_idle_ok){ g_idle_ok=1; g_idle_pix=load_png_scaled("/usr/lib/python3.13/idlelib/Icons/idle_48.png"); } return g_idle_pix; }
    if(id>=1100&&id<1100+g_nxa){ xapp* a=&g_xa[id-1100]; if(!a->pixok)find_icon(a); return a->pix; }
    return 0; }
int app_icon_draw(int id,int x,int y,int sz){
    u32* p=app_pix(id); if(!p)return 0;
    for(int yy=0;yy<sz;yy++) for(int xx=0;xx<sz;xx++){ u32 c=p[(yy*ICS/sz)*ICS+(xx*ICS/sz)]; int a=(int)(c>>24); if(!a)continue; if(a>=250)putpx(x+xx,y+yy,c&0xFFFFFF); else pblend(x+xx,y+yy,c&0xFFFFFF,a); }
    return 1; }
static int xs_has(const char* s,const char* w){ for(int i=0;s[i];i++){ int k=0; while(w[k]&&s[i+k]==w[k])k++; if(!w[k])return 1; } return 0; }
extern volatile u64 g_ticks;
void open_win(int i); void tcol(u32 c); void term_puts(const char*s); void toast(const char*m);
static char g_wt[40]="Firefox", g_wlog[64]="/tmp/ff.log", g_wdone[64]="/tmp/ff.done"; static int g_wff=1;
static int g_ff_watch, g_ff_onwin, g_ff_warned, g_ff_full, g_ff_lost, g_ff_why; static u64 g_ff_t0, g_ff_poll, g_ff_lt;
static char* ux(char* p,u64 v){ int d=1; while(d<16&&(v>>(d*4)))d++; return uhex(p,v,d); }
static char* ff_where(char* p,u64 a){
    struct lx_mapname { u64 cr3,b,e; char nm[40]; }; extern struct lx_mapname g_mapn[2048]; extern int g_mapn_i;
    int n=g_mapn_i<2048?g_mapn_i:2048, hit=-1;
    for(int j=0;j<n;j++){ int i=(g_mapn_i-n+j)%2048; struct lx_mapname* m=&g_mapn[i]; if(m->e&&a>=m->b&&a<m->e)hit=i; }
    if(hit<0){ if(a>=0x40000400000ULL&&a<0x40002000000ULL){ p=scopy(p," in firefox-esr +"); return ux(p,a-0x40000400000ULL); } if(a>=0x40030000000ULL&&a<0x40030100000ULL){ p=scopy(p," in ld-linux +"); return ux(p,a-0x40030000000ULL); } return p; }
    u64 base=g_mapn[hit].b; for(int j=0;j<n;j++){ struct lx_mapname* m=&g_mapn[j]; if(m->e==base&&m->cr3==g_mapn[hit].cr3){ int same=1; for(int k=0;k<40;k++){ if(m->nm[k]!=g_mapn[hit].nm[k]){ same=0; break; } if(!m->nm[k])break; } if(same){ base=m->b; j=-1; } } }
    p=scopy(p," in "); p=scopy(p,g_mapn[hit].nm); p=scopy(p," +"); return ux(p,a-base); }
static const char g_scn[]="0 read,1 write,2 open,3 close,4 stat,5 fstat,6 lstat,7 poll,8 lseek,9 mmap,10 mprotect,11 munmap,12 brk,13 rt_sigaction,14 rt_sigprocmask,15 rt_sigreturn,16 ioctl,17 pread64,18 pwrite64,19 readv,20 writev,21 access,22 pipe,23 select,24 sched_yield,25 mremap,26 msync,27 mincore,28 madvise,29 shmget,30 shmat,31 shmctl,32 dup,33 dup2,35 nanosleep,39 getpid,41 socket,42 connect,44 sendto,45 recvfrom,46 sendmsg,47 recvmsg,49 bind,50 listen,51 getsockname,52 getpeername,53 socketpair,54 setsockopt,55 getsockopt,56 clone,57 fork,59 execve,61 wait4,62 kill,63 uname,67 shmdt,72 fcntl,73 flock,77 ftruncate,79 getcwd,80 chdir,82 rename,83 mkdir,87 unlink,89 readlink,90 chmod,95 umask,96 gettimeofday,97 getrlimit,98 getrusage,99 sysinfo,102 getuid,110 getppid,112 setsid,131 sigaltstack,137 statfs,138 fstatfs,140 getpriority,141 setpriority,144 sched_setscheduler,149 mlock,157 prctl,158 arch_prctl,160 setrlimit,165 mount,166 umount2,186 gettid,187 readahead,200 tkill,202 futex,203 sched_setaffinity,204 sched_getaffinity,213 epoll_create,217 getdents64,218 set_tid_address,221 fadvise64,228 clock_gettime,229 clock_getres,230 clock_nanosleep,231 exit_group,232 epoll_wait,233 epoll_ctl,234 tgkill,237 mbind,239 get_mempolicy,247 waitid,253 inotify_add_watch,254 inotify_rm_watch,257 openat,258 mkdirat,262 newfstatat,263 unlinkat,267 readlinkat,270 pselect6,271 ppoll,272 unshare,273 set_robust_list,280 utimensat,281 epoll_pwait,282 signalfd,283 timerfd_create,284 eventfd,285 fallocate,286 timerfd_settime,288 accept4,289 signalfd4,290 eventfd2,291 epoll_create1,292 dup3,293 pipe2,294 inotify_init1,302 prlimit64,308 setns,315 sched_getattr,317 seccomp,318 getrandom,319 memfd_create,324 membarrier,332 statx,334 rseq,425 io_uring_setup,434 pidfd_open,435 clone3,438 pidfd_getfd,439 faccessat2,441 epoll_pwait2,";
static const char* sc_name(u32 n){ static char out[24]; const char* q=g_scn; while(*q){ u32 v=0; while(*q>='0'&&*q<='9')v=v*10+(u32)(*q++-'0'); q++; int k=0; while(*q&&*q!=','){ if(v==n&&k<23)out[k++]=*q; q++; } if(*q)q++; if(v==n){ out[k]=0; return out; } } return 0; }
static char* g_rp; static int g_rpn, g_rpcap;
static void rp(const char* t){ term_puts(t); if(!g_rp)return; while(*t&&g_rpn<g_rpcap-1)g_rp[g_rpn++]=*t++; }
static const char* gap_kind(u32 k){ switch(k){ case 1: return "Unix socket type"; case 2: return "socket family"; case 3: return "internet socket type"; case 4: return "ioctl request"; case 5: return "fcntl command"; case 6: return "prctl option"; case 7: return "clone flags"; case 8: return "socketpair type"; } return "option"; }
static char* gap_val(char* p,u32 k,u64 v){
    if(k==2){ const char* n=v==10?"AF_INET6":v==16?"AF_NETLINK":v==17?"AF_PACKET":v==38?"AF_ALG":v==40?"AF_VSOCK":0; if(n)return scopy(p,n); return udec(p,(u32)v); }
    if(k==1||k==3||k==8){ const char* n=v==2?"SOCK_DGRAM":v==3?"SOCK_RAW":v==5?"SOCK_SEQPACKET":0; if(n)return scopy(p,n); return udec(p,(u32)v); }
    if(k==4||k==7){ p=scopy(p,"0x"); return ux(p,v); }
    return udec(p,(u32)v); }
static int gap_benign(u32 k,u64 v){ if(k==2&&(v==10||v==16))return 1; if(k==6)return 1; return 0; }
static void ff_cause(int exited,int code){
    extern u32 g_enosys_nr[48], g_enosys_last[48]; extern int g_enosys_n; extern u32 g_gap_kind[40], g_gap_last[40]; extern u64 g_gap_val[40]; extern int g_gap_n;
    static const u16 benign[]={334,425,426,427,444,445,446,435,437,439,0};
    char b[160],*p=b; tcol(rgb(240,190,90)); rp("Most likely cause:\n"); tcol(WHT);
    if(exited&&code==127){ int k=k_find_file(g_wlog); int ni=0; if(k>=0){ const char* d=fsd(k); int n=FS[k].len; for(int i=0;i+13<n;i++){ const char* w="not installed"; int j=0; while(w[j]&&d[i+j]==w[j])j++; if(!w[j]){ ni=1; break; } } }
        p=scopy(p,ni?"  ":"  the program or a library it needs was not found"); if(ni){ p=scopy(p,g_wt); p=scopy(p," is not installed (see the log lines below)"); } p=scopy(p,".\n"); *p=0; rp(b); return; }
    int bi=-1; u32 bs=0; for(int i=0;i<g_enosys_n;i++){ int ok=1; for(int k=0;benign[k];k++) if(g_enosys_nr[i]==benign[k])ok=0; if(ok&&g_enosys_last[i]>=bs){ bs=g_enosys_last[i]; bi=i; } }
    int gi=-1; u32 gs=0; for(int i=0;i<g_gap_n;i++) if(!gap_benign(g_gap_kind[i],g_gap_val[i])&&g_gap_last[i]>=gs){ gs=g_gap_last[i]; gi=i; }
    if(bi<0&&gi<0){ rp(g_gap_n||g_enosys_n?"  nothing important is missing in the Linux layer (only harmless probes, listed below).\n":"  nothing missing was detected in the Linux layer.\n"); return; }
    p=scopy(p,"  the last missing piece used was ");
    if(bi>=0&&(gi<0||bs>=gs)){ p=scopy(p,"the Linux syscall "); const char* nm=sc_name(g_enosys_nr[bi]); if(nm)p=scopy(p,nm); p=scopy(p," (number "); p=udec(p,g_enosys_nr[bi]); p=scopy(p,").\n"); }
    else { p=scopy(p,gap_kind(g_gap_kind[gi])); *p++=' '; p=gap_val(p,g_gap_kind[gi],g_gap_val[gi]); p=scopy(p,".\n  The syscall exists, this option does not yet.\n"); }
    *p=0; rp(b); }
static void ff_gaps(void){
    extern u32 g_gap_kind[40], g_gap_ct[40]; extern u64 g_gap_val[40]; extern int g_gap_n; char b[120],*p;
    tcol(GREY); rp("Supported syscalls called with an option NoovexOS does not have yet:\n"); tcol(WHT);
    if(!g_gap_n){ rp("  none\n"); return; }
    for(int i=0;i<g_gap_n;i++){ p=b; p=scopy(p,"  "); p=scopy(p,gap_kind(g_gap_kind[i])); *p++=' '; p=gap_val(p,g_gap_kind[i],g_gap_val[i]); p=scopy(p," x"); p=udec(p,g_gap_ct[i]); if(gap_benign(g_gap_kind[i],g_gap_val[i]))p=scopy(p,"  (usually harmless)"); *p++='\n'; *p=0; rp(b); } }
static int g_rep_no;
static void ff_save(void){
    if(!g_rp||g_rpn<=0)return; char nm[48],*q=nm; q=scopy(q,g_wff?"firefox":"app"); q=scopy(q,"-crash-"); q=udec(q,(u32)(++g_rep_no)); q=scopy(q,".txt"); *q=0;
    extern int fs_save_bin(const char*,const u8*,int,int); int r=fs_save_bin(nm,(const u8*)g_rp,g_rpn,0);
    char b[96],*p=b; tcol(GREY); if(r>=0){ p=scopy(p,"This report was saved as "); p=scopy(p,nm); p=scopy(p," in Files.\n"); } else p=scopy(p,"(could not save this report to Files)\n"); *p=0; term_puts(b); tcol(WHT); }
#define term_puts rp
static void ff_report(int exited,int code){
    extern u32 g_enosys_nr[48], g_enosys_ct[48]; extern int g_enosys_n; char b[160]; char* p;
    open_win(1);
    { extern void klog(const char*); char kb[48],*kp=kb; kp=scopy(kp,g_wt); kp=scopy(kp,exited?": exited, code ":": no window after 2 min"); if(exited)kp=udec(kp,(u32)code); *kp=0; klog(kb); }
    if(!g_rp){ g_rpcap=16384; g_rp=(char*)kmalloc((unsigned long)g_rpcap); } g_rpn=0;
    tcol(rgb(240,190,90));
    if(exited){ p=b; p=scopy(p,"\n"); p=scopy(p,g_wt); p=scopy(p," exited (code "); p=udec(p,(u32)code); p=scopy(p,") before or after opening a window.\n"); *p=0; term_puts(b); }
    else { p=b; p=scopy(p,"\n"); p=scopy(p,g_wt);
        p=scopy(p,g_ff_why==1?" opened a window, then the X display was lost (the X server unmapped or freed its screen). It has not exited yet.\n":g_ff_why==2?" opened a window and full screen was closed. It has not exited yet.\n":" is still running but has not drawn a window after 2 minutes.\n"); *p=0; term_puts(b); }
    ff_cause(exited,code);
    tcol(GREY); p=b; p=scopy(p,"Last lines of "); p=scopy(p,g_wlog); p=scopy(p,":\n"); *p=0; term_puts(b); tcol(WHT);
    int k=k_find_file(g_wlog);
    if(k<0||FS[k].len<=0)term_puts("  (empty)\n");
    else { const char* d=fsd(k); int n=FS[k].len, s=n, lines=0; while(s>0&&lines<26){ s--; if(d[s]=='\n'&&s<n-1)lines++; } if(s>0)s++;
        char ln[150]; int o=0; for(int i=s;i<n;i++){ char c=d[i]; if(c=='\n'||o>=146){ ln[o]=0; term_puts("  "); term_puts(ln); term_puts("\n"); o=0; if(c=='\n')continue; } if(c>=32&&c<127)ln[o++]=c; } if(o){ ln[o]=0; term_puts("  "); term_puts(ln); term_puts("\n"); } }
    { extern u64 g_segv_addr, g_segv_rip, g_segv_stk[96]; extern int g_segv_n, g_segv_nstk, g_segv_ncode; extern u8 g_segv_code[24]; extern char g_segv_msg[128]; extern u32 g_segv_line;
      struct lx_scr { u32 nr,pid; u64 a1,a2; long ret; }; extern struct lx_scr g_scr_snap[32]; extern int g_scr_sn;
      extern u32 g_scf_nr[40], g_scf_err[40], g_scf_ct[40]; extern u64 g_scf_a1[40], g_scf_a2[40]; extern int g_scf_n;
      if(g_segv_n){ tcol(GREY); term_puts("Crash: memory fault at "); p=b; p=uhex(p,g_segv_addr,12); *p=0; tcol(WHT); term_puts(b); tcol(GREY); term_puts(", code at "); p=b; p=uhex(p,g_segv_rip,12); *p=0; tcol(WHT); term_puts(b);
        p=b; p=ff_where(p,g_segv_rip); *p=0; term_puts(b); term_puts("\n");
        if(g_segv_addr<0x1000&&g_segv_ncode){ tcol(rgb(240,190,90)); term_puts(g_wff?"This is MOZ_CRASH: Firefox stopped itself on purpose.\n":"The program stopped itself on purpose (an internal check failed).\n"); }
        if(g_segv_msg[0]){ tcol(GREY); term_puts("Reason: "); tcol(WHT); term_puts(g_segv_msg); term_puts("\n"); }
        if(g_segv_line){ tcol(GREY); term_puts("Source line: "); p=b; p=udec(p,g_segv_line); *p=0; tcol(WHT); term_puts(b); term_puts("\n"); }
        if(g_segv_ncode){ tcol(GREY); term_puts("Code bytes: "); tcol(WHT); p=b; for(int i=0;i<g_segv_ncode;i++){ p=hx(p,g_segv_code[i],2); *p++=' '; } *p++='\n'; *p=0; term_puts(b); }
        int shown=0; tcol(GREY); term_puts("Call stack:\n"); tcol(WHT);
        for(int i=0;i<g_segv_nstk&&shown<8;i++){ char w[64]; char* e=ff_where(w,g_segv_stk[i]); if(e==w)continue; *e=0; p=b; p=scopy(p,"  "); p=uhex(p,g_segv_stk[i],12); p=scopy(p,w); p=scopy(p,"\n"); *p=0; term_puts(b); shown++; }
        if(!shown)term_puts("  (no library return addresses found)\n"); }
      { extern void lx_sc_snap(void); if(!g_segv_n)lx_sc_snap(); }
      if(!exited){ extern u16 g_lastsc[]; extern u32 g_sccount[]; tcol(GREY); term_puts("Linux processes (pid name state last-syscall calls):\n"); tcol(WHT);
        for(int i=0;i<JMAX;i++){ struct jproc* q=&g_jp[i]; if(q->state==S_FREE||q->state==S_ZOMB||!q->lx)continue;
          p=b; p=scopy(p,"  "); p=udec(p,(u32)q->pid); p=scopy(p," "); { char nm[20]; int z=0; for(;q->cmd[z]&&z<19;z++)nm[z]=q->cmd[z]; nm[z]=0; p=scopy(p,nm); }
          p=scopy(p,q->state==S_RUN?" run ":q->state==S_WAIT?" wait ":q->state==S_SLEEP?" sleep ":" stop ");
          { const char* nm=sc_name(g_lastsc[i]); if(nm)p=scopy(p,nm); else p=udec(p,g_lastsc[i]); } p=scopy(p," "); p=udec(p,g_sccount[i]); p=scopy(p,"\n"); *p=0; term_puts(b); } }
      { tcol(GREY); term_puts(g_segv_n?"Last syscalls before the crash:\n":"Last syscalls:\n"); tcol(WHT);
        for(int i=(g_scr_sn>8?g_scr_sn-8:0);i<g_scr_sn;i++){ struct lx_scr* c=&g_scr_snap[i]; p=b; p=scopy(p,"  pid "); p=udec(p,c->pid); p=scopy(p," "); p=udec(p,c->nr); const char* nm=sc_name(c->nr); if(nm){ p=scopy(p," "); p=scopy(p,nm); } p=scopy(p,"("); p=ux(p,c->a1); p=scopy(p,", "); p=ux(p,c->a2); p=scopy(p,") = "); if(c->ret<0&&c->ret>-4096){ p=scopy(p,"-"); p=udec(p,(u32)(-c->ret)); } else p=ux(p,(u64)c->ret); p=scopy(p,"\n"); *p=0; term_puts(b); } }
      if(g_scf_n){ tcol(GREY); term_puts("Syscalls that returned an error (nr name errno x count):\n"); tcol(WHT);
        for(int i=0;i<g_scf_n;i++){ p=b; p=scopy(p,"  "); p=udec(p,g_scf_nr[i]); const char* nm=sc_name(g_scf_nr[i]); if(nm){ p=scopy(p," "); p=scopy(p,nm); } p=scopy(p," -"); p=udec(p,g_scf_err[i]); p=scopy(p," x"); p=udec(p,g_scf_ct[i]); if(g_scf_nr[i]==9||g_scf_nr[i]==25||g_scf_nr[i]==59){ p=scopy(p," last("); p=ux(p,g_scf_a1[i]); p=scopy(p,", "); p=ux(p,g_scf_a2[i]); p=scopy(p,")"); } p=scopy(p,(i&1)?"\n":"   "); *p=0; term_puts(b); }
        if(g_scf_n&1)term_puts("\n"); } }
    tcol(GREY); p=b; p=scopy(p,"Unimplemented Linux syscalls "); p=scopy(p,g_wt); p=scopy(p," called:\n"); *p=0; term_puts(b); tcol(WHT);
    if(!g_enosys_n)term_puts("  none\n");
    for(int i=0;i<g_enosys_n;i++){ p=b; p=scopy(p,"  "); p=udec(p,g_enosys_nr[i]); const char* nm=sc_name(g_enosys_nr[i]); if(nm){ p=scopy(p," "); p=scopy(p,nm); } p=scopy(p," x"); p=udec(p,g_enosys_ct[i]); p=scopy(p,"\n"); *p=0; term_puts(b); }
    ff_gaps();
    tcol(GREY); p=b; p=scopy(p,"Send a photo of this to keep porting "); p=scopy(p,g_wt); p=scopy(p,".\n"); *p=0; term_puts(b); tcol(WHT);
    ff_save(); }
#undef term_puts
static void watch_reset(void){ extern int g_enosys_n; g_enosys_n=0; { extern int g_gap_n; g_gap_n=0; } extern int g_segv_n; g_segv_n=0; extern int g_scf_n; g_scf_n=0; k_unlink(g_wdone);
    g_ff_watch=1; g_ff_onwin=1; g_ff_warned=0; g_ff_full=0; g_ff_lost=0; g_ff_why=0; g_ff_t0=g_ticks; g_ff_poll=g_ticks; }
static int xr_skip(const char* b){ static const char* const no[]={"bash","sh","dash","env","python","python3","perl","wish","idle","idle3","sudo","apt","nvx-xrun",0}; for(int i=0;no[i];i++){ int k=0; while(no[i][k]&&b[k]==no[i][k])k++; if(!no[i][k]&&(!b[k]||b[k]==' '||(b[k]>='0'&&b[k]<='9')||b[k]=='.'))return 1; } return 0; }
static const char* xr_base(const char* l,int* len){ while(*l==' ')l++; const char* b=l; int n=0; while(l[n]&&l[n]!=' '){ if(l[n]=='/')b=l+n+1; n++; } *len=(int)(l+n-b); return b; }
void xrun_launch(const char* line){
    int bl; const char* bn=xr_base(line,&bl); char nm[32]; int o=0; for(int i=0;i<bl&&o<31;i++){ char c=bn[i]; if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.')nm[o++]=c; } nm[o]=0; if(!o){ nm[0]='a'; nm[1]='p'; nm[2]='p'; nm[3]=0; }
    char* q=g_wt; q=scopy(q,nm); *q=0; if(g_wt[0]>='a'&&g_wt[0]<='z')g_wt[0]=(char)(g_wt[0]-32);
    q=g_wlog; q=scopy(q,"/tmp/"); q=scopy(q,nm); q=scopy(q,".log"); *q=0; q=g_wdone; q=scopy(q,"/tmp/"); q=scopy(q,nm); q=scopy(q,".done"); *q=0; g_wff=0;
    watch_reset();
    static char cmd[300]; q=cmd; q=scopy(q,"bash /usr/bin/nvx-xrun "); q=scopy(q,nm); *q++=' '; for(int i=0;line[i]&&q<cmd+290;i++){ char c=line[i]; if(c=='&'||c==';'||c=='|'||c=='>'||c=='<'||c=='`'||c=='$')continue; *q++=c; } *q=0;
    { extern int g_xover; g_xover=1; }
    x_launch(cmd);
    char tb[64]; q=tb; q=scopy(q,"Starting "); q=scopy(q,g_wt); q=scopy(q,"..."); *q=0; toast(tb); }
int xapps_is_gui(const char* line){
    int bl; const char* b=xr_base(line,&bl); if(bl<=0||xr_skip(b))return 0; xapps_refresh();
    for(int i=0;i<g_nxa;i++){ if(g_xa[i].term)continue; int el; const char* e=xr_base(g_xa[i].exec,&el); if(el!=bl)continue; int same=1; for(int k=0;k<bl;k++) if(e[k]!=b[k]){ same=0; break; } if(same&&!xs_has(g_xa[i].exec,"firefox"))return 1; }
    return 0; }
void firefox_launch(void){
    { char* q=g_wt; q=scopy(q,"Firefox"); *q=0; q=g_wlog; q=scopy(q,"/tmp/ff.log"); *q=0; q=g_wdone; q=scopy(q,"/tmp/ff.done"); *q=0; g_wff=1; }
    extern int g_enosys_n; g_enosys_n=0; { extern int g_segv_n; g_segv_n=0; extern int g_scf_n; g_scf_n=0; extern int g_gap_n; g_gap_n=0; } k_unlink("/tmp/ff.done");
    { extern int g_xover; g_xover=1; }
    x_launch("bash /usr/bin/nvx-firefox");
    g_ff_watch=1; g_ff_onwin=1; g_ff_warned=0; g_ff_full=0; g_ff_lost=0; g_ff_why=0; g_ff_t0=g_ticks; g_ff_poll=g_ticks;
    toast("Starting Firefox - the first start can take a minute"); }
static void x_prestart(void){
    static u64 first; static int done; extern int g_xfb_live; if(done)return; if(!first){ first=g_ticks?g_ticks:1; return; } if(g_ticks-first<300)return; done=1;
    if(g_xfb_live)return; xapps_refresh(); int ff=0; for(int i=0;i<g_nxa;i++) if(xs_has(g_xa[i].exec,"firefox"))ff=1; if(!ff)return;
    extern int g_xover; extern void x_ensure(void); g_xover=1; x_ensure(); }
void firefox_tick(void){
    x_prestart();
    extern int g_xfb_live; extern void xfull_enter(void); extern void xfull_exit(void); extern int xfb_has_window(void);
    if(!g_ff_watch||g_ticks-g_ff_poll<50)return; g_ff_poll=g_ticks;
    extern int g_xfull;
    extern int g_xover; extern void xover_invalidate(void);
    if(g_xfb_live&&!g_xover&&!g_xfull){ g_xover=1; xover_invalidate(); }
    if(g_ff_onwin&&g_xfb_live&&xfb_has_window()){ g_ff_onwin=0; g_ff_full=1; { extern void wm_close_xdisp(void); wm_close_xdisp(); } }
    if(g_ff_full&&!g_xfb_live&&!g_ff_lost){ g_ff_lost=1; g_ff_lt=g_ticks; g_ff_why=1; }
    int k=k_find_file(g_wdone);
    if(k>=0){ int code=0; const char* d=fsd(k); for(int i=0;i<FS[k].len&&d[i]>='0'&&d[i]<='9';i++)code=code*10+(d[i]-'0'); k_unlink(g_wdone);
        if(code==0&&g_ff_full){ g_ff_watch=0; g_ff_onwin=0; xfull_exit(); { extern void xover_invalidate(void); xover_invalidate(); } return; }
        g_ff_watch=0; g_ff_onwin=0; xfull_exit(); ff_report(1,code); return; }
    if(g_ff_lost==1&&g_ticks-g_ff_lt>1500){ g_ff_lost=2; ff_report(0,0); }
    if(g_ff_onwin&&!g_ff_warned&&g_ticks-g_ff_t0>12000){ g_ff_warned=1; ff_report(0,0); } }
void app_launch(int id){
    if(id==1001){ x_launch("idle3"); return; }
    if(id>=1100&&id<1100+g_nxa){ xapp* a=&g_xa[id-1100]; if(xs_has(a->exec,"firefox")){ firefox_launch(); return; }
        if(a->term){ static char b[200]; int i=0; for(;a->exec[i]&&i<199;i++)b[i]=a->exec[i]; b[i]=0; run_cmd(b); } else xrun_launch(a->exec); } }
