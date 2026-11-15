#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mount.h>

static int g_fail, g_ok;
static void ck(int c,const char* what,const char* det){ if(c){ g_ok++; printf("ok   %s\n",what); } else { g_fail++; printf("FAIL %s%s%s\n",what,det?" : ":"",det?det:""); } }
static int rdf(const char* p,char* b,int cap){ int fd=open(p,O_RDONLY); if(fd<0)return -errno; int n=(int)read(fd,b,cap-1); close(fd); if(n<0)return -errno; b[n]=0; return n; }

static void t_loginuid(void){
    char b[64], d[128];
    int fd=open("/proc/self/loginuid",O_RDWR|O_NOFOLLOW);
    snprintf(d,sizeof d,"fd=%d errno=%d",fd,errno); ck(fd>=0,"loginuid: open O_RDWR succeeds (pam_loginuid)",d); if(fd<0)return;
    int n=(int)read(fd,b,sizeof b-1); ck(n>0&&b[0]>='0'&&b[0]<='9',"loginuid: read returns a number",0);
    ck(lseek(fd,0,SEEK_SET)==0,"loginuid: lseek 0",0);
    ck(ftruncate(fd,0)==0,"loginuid: ftruncate 0",strerror(errno));
    n=(int)write(fd,"1234",4); snprintf(d,sizeof d,"n=%d errno=%d",n,errno); ck(n==4,"loginuid: write 1234",d);
    errno=0; n=(int)write(fd,"x1",2); ck(n<0&&errno==EINVAL,"loginuid: write of a non-number is EINVAL",0);
    close(fd);
    n=rdf("/proc/self/loginuid",b,sizeof b); ck(n==4&&!strcmp(b,"1234"),"loginuid: value reads back",b);
    char s1[32]; rdf("/proc/self/sessionid",s1,sizeof s1); ck(s1[0]>='0'&&s1[0]<='9'&&strcmp(s1,"4294967295"),"sessionid: set together with loginuid",s1);
    char pp[64]; snprintf(pp,sizeof pp,"/proc/%d/loginuid",getpid()); n=rdf(pp,b,sizeof b); ck(n==4&&!strcmp(b,"1234"),"loginuid: /proc/<pid>/loginuid matches",b);
    pid_t c=fork(); if(c==0){ char x[32]; int k=rdf("/proc/self/loginuid",x,sizeof x); _exit(k==4&&!strcmp(x,"1234")?0:1); }
    int st=0; waitpid(c,&st,0); ck(WIFEXITED(st)&&WEXITSTATUS(st)==0,"loginuid: inherited by a forked child",0);
    fd=open("/proc/self/loginuid",O_WRONLY); n=fd>=0?(int)write(fd,"4294967295\n",11):-1; if(fd>=0)close(fd); ck(n==11,"loginuid: writing 4294967295 unsets it",0);
}

static void t_cpudirs(void){
    char b[64], d[160]; int n=rdf("/sys/devices/system/cpu/online",b,sizeof b); ck(n>0,"sysfs: cpu/online readable",b);
    int last=0; char* dash=strchr(b,'-'); if(dash)last=atoi(dash+1); int want=last+1;
    DIR* D=opendir("/sys/devices/system/cpu"); int cnt=0, other=0; struct dirent* e;
    if(D){ while((e=readdir(D))){ if(!strncmp(e->d_name,"cpu",3)&&e->d_name[3]>='0'&&e->d_name[3]<='9'){ cnt++; if(e->d_type!=DT_DIR)other++; } } closedir(D); }
    snprintf(d,sizeof d,"online=%s cpuN dirs=%d want=%d nondir=%d",b,cnt,want,other); ck(D&&cnt==want&&!other,"sysfs: one cpuN directory per online CPU (htop CPU count)",d);
    struct stat st; ck(stat("/sys/devices/system/cpu/cpu0",&st)==0&&S_ISDIR(st.st_mode),"sysfs: cpu0 is a directory",0);
    n=rdf("/sys/devices/system/cpu/cpu0/online",b,sizeof b); ck(n==2&&!strcmp(b,"1\n"),"sysfs: cpu0/online reads 1",b);
    snprintf(d,sizeof d,"/sys/devices/system/cpu/cpu%d",want); ck(stat(d,&st)<0&&errno==ENOENT,"sysfs: cpu beyond the last one does not exist",d);
    ck(stat("/sys/devices/system/cpu/cpu00",&st)<0,"sysfs: cpu00 does not exist",0);
    D=opendir("/sys/devices/system/cpu/cpu0"); cnt=0; if(D){ while((e=readdir(D))) if(!strcmp(e->d_name,"online"))cnt++; closedir(D); } ck(cnt==1,"sysfs: cpu0 lists online",0);
}

static void t_cwd(void){
    char b[256], c[256], d[600]; ssize_t n;
    mkdir("/tmp/kgapd",0755); chdir("/tmp");
    n=readlink("/proc/self/cwd",b,sizeof b-1); if(n>0)b[n]=0; getcwd(c,sizeof c);
    snprintf(d,sizeof d,"n=%zd link=%s getcwd=%s",n,n>0?b:"",c); ck(n>0&&!strcmp(b,c),"proc: /proc/self/cwd matches getcwd",d);
    int p1[2], p2[2]; pipe(p1); pipe(p2);
    pid_t ch=fork(); if(ch==0){ char x; chdir("/tmp/kgapd"); write(p1[1],"r",1); read(p2[0],&x,1); _exit(0); }
    char x; read(p1[0],&x,1);
    char pp[64]; snprintf(pp,sizeof pp,"/proc/%d/cwd",ch); n=readlink(pp,b,sizeof b-1); if(n>0)b[n]=0;
    snprintf(d,sizeof d,"n=%zd errno=%d link=%s",n,errno,n>0?b:""); ck(n>0&&!strcmp(b,"/tmp/kgapd"),"proc: /proc/<other pid>/cwd (tmux pane_current_path)",d);
    snprintf(pp,sizeof pp,"/proc/%d/root",ch); n=readlink(pp,b,sizeof b-1); if(n>0)b[n]=0; ck(n==1&&b[0]=='/',"proc: /proc/<other pid>/root is /",0);
    snprintf(pp,sizeof pp,"/proc/%d/exe",ch); n=readlink(pp,b,sizeof b-1); if(n>0)b[n]=0; ssize_t m=readlink("/proc/self/exe",c,sizeof c-1); if(m>0)c[m]=0;
    snprintf(d,sizeof d,"child=%zd %s self=%zd %s",n,n>0?b:"",m,m>0?c:""); ck(n==m&&(n<0||!strcmp(b,c)),"proc: /proc/<other pid>/exe same as the parent's after fork",d);
    write(p2[1],"g",1); int st; waitpid(ch,&st,0); close(p1[0]); close(p1[1]); close(p2[0]); close(p2[1]);
    ck(readlink("/proc/999999/cwd",b,sizeof b)<0&&errno==ENOENT,"proc: cwd of a missing pid is ENOENT",0);
    DIR* D=opendir("/proc/self"); int f=0; struct dirent* e; if(D){ while((e=readdir(D))) if(!strcmp(e->d_name,"cwd"))f=1; closedir(D); } ck(f,"proc: /proc/self lists cwd",0);
    rmdir("/tmp/kgapd");
}

static int ewait(int ep,struct epoll_event* ev){ return epoll_wait(ep,ev,4,0); }
static void t_epoll(void){
    struct epoll_event ev, out[4]; int p[2]; char b[16], d[96]; int n;
    pipe2(p,O_NONBLOCK); int ep=epoll_create1(0);
    ev.events=EPOLLIN|EPOLLET|EPOLLRDHUP; ev.data.u64=7; epoll_ctl(ep,EPOLL_CTL_ADD,p[0],&ev);
    ck(ewait(ep,out)==0,"epoll ET: nothing ready at first",0);
    write(p[1],"ab",2); n=ewait(ep,out); snprintf(d,sizeof d,"n=%d ev=%x",n,n>0?out[0].events:0);
    ck(n==1&&(out[0].events&EPOLLIN)&&out[0].data.u64==7,"epoll ET: data arrival is reported (EPOLLRDHUP in the mask is accepted)",d);
    n=ewait(ep,out); ck(n==0,"epoll ET: same readiness is not reported twice",0);
    ev.events=EPOLLIN|EPOLLET; epoll_ctl(ep,EPOLL_CTL_MOD,p[0],&ev); n=ewait(ep,out); ck(n==1,"epoll ET: EPOLL_CTL_MOD re-arms",0);
    while(read(p[0],b,sizeof b)>0); ck(errno==EAGAIN,"epoll ET: drained to EAGAIN",0);
    ck(ewait(ep,out)==0,"epoll ET: nothing after draining",0);
    write(p[1],"c",1); n=ewait(ep,out); ck(n==1&&(out[0].events&EPOLLIN),"epoll ET: new data after EAGAIN is reported",0);
    while(read(p[0],b,sizeof b)>0);
    write(p[1],"d",1); n=ewait(ep,out); ck(n==1,"epoll ET: drain and refill between two waits is reported (Go netpoller pattern)",0);
    int ep2=epoll_create1(0); ev.events=EPOLLOUT|EPOLLET; ev.data.u64=9; epoll_ctl(ep2,EPOLL_CTL_ADD,p[1],&ev);
    n=ewait(ep2,out); int n2=ewait(ep2,out); snprintf(d,sizeof d,"first=%d second=%d",n,n2); ck(n==1&&n2==0,"epoll ET: always-writable pipe reported once, not every wait",d);
    int ep3=epoll_create1(0); ev.events=EPOLLOUT; epoll_ctl(ep3,EPOLL_CTL_ADD,p[1],&ev);
    n=ewait(ep3,out); n2=ewait(ep3,out); ck(n==1&&n2==1,"epoll LT: level-triggered still reports every wait",0);
    int ep4=epoll_create1(0); ev.events=EPOLLIN; epoll_ctl(ep4,EPOLL_CTL_ADD,ep,&ev);
    write(p[1],"e",1); n=ewait(ep4,out); ck(n==1,"epoll ET: nested epoll fd sees the new event",0);
    close(ep); close(ep2); close(ep3); close(ep4); close(p[0]); close(p[1]);
    int sv[2]; socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK,0,sv); ep=epoll_create1(0); ev.events=EPOLLIN|EPOLLOUT|EPOLLET; epoll_ctl(ep,EPOLL_CTL_ADD,sv[0],&ev);
    n=ewait(ep,out); n2=ewait(ep,out); snprintf(d,sizeof d,"first=%d ev=%x second=%d",n,n>0?out[0].events:0,n2); ck(n==1&&(out[0].events&EPOLLOUT)&&n2==0,"epoll ET: Unix socket writable reported once",d);
    write(sv[1],"x",1); n=ewait(ep,out); ck(n==1&&(out[0].events&EPOLLIN),"epoll ET: Unix socket data reported",0);
    write(sv[1],"y",1); n=ewait(ep,out); ck(n==1&&(out[0].events&EPOLLIN),"epoll ET: more data while still readable is reported",0);
    ck(ewait(ep,out)==0,"epoll ET: then quiet again",0);
    close(sv[0]); close(sv[1]); close(ep);
    int efd=eventfd(0,EFD_NONBLOCK); ep=epoll_create1(0); ev.events=EPOLLIN|EPOLLET; epoll_ctl(ep,EPOLL_CTL_ADD,efd,&ev);
    unsigned long long one=1; write(efd,&one,8); n=ewait(ep,out); write(efd,&one,8); n2=ewait(ep,out); int n3=ewait(ep,out);
    snprintf(d,sizeof d,"%d %d %d",n,n2,n3); ck(n==1&&n2==1&&n3==0,"epoll ET: eventfd signalled twice gives two events",d);
    close(efd); close(ep);
}

static void t_times(void){
    char d[200]; struct stat st; const char* f="/tmp/kgap_f"; const char* l="/tmp/kgap_l"; unlink(f); unlink(l);
    int fd=open(f,O_RDWR|O_CREAT|O_TRUNC,0644); write(fd,"x",1);
    struct timespec ts[2]={{1000,0},{2000,0}}; int r=futimens(fd,ts); fstat(fd,&st);
    snprintf(d,sizeof d,"r=%d errno=%d atime=%ld mtime=%ld",r,errno,(long)st.st_atime,(long)st.st_mtime); ck(r==0&&st.st_atime==1000&&st.st_mtime==2000,"futimens sets atime and mtime",d);
    struct timespec t2[2]={{0,UTIME_OMIT},{3000,0}}; futimens(fd,t2); fstat(fd,&st); snprintf(d,sizeof d,"atime=%ld mtime=%ld",(long)st.st_atime,(long)st.st_mtime); ck(st.st_atime==1000&&st.st_mtime==3000,"futimens: UTIME_OMIT keeps atime",d);
    struct timespec t3[2]={{5,0},{6,2000000000L}}; errno=0; ck(futimens(fd,t3)<0&&errno==EINVAL,"futimens: bad tv_nsec is EINVAL",0);
    time_t now=time(0); r=utimensat(AT_FDCWD,f,0,0); stat(f,&st); snprintf(d,sizeof d,"r=%d mtime=%ld now=%ld",r,(long)st.st_mtime,(long)now); ck(r==0&&st.st_mtime>=now-2&&st.st_mtime<=now+2,"utimensat(NULL) sets now",d);
    struct utimbuf { time_t a, m; } ub={500,600}; r=(int)syscall(132,f,&ub); stat(f,&st); snprintf(d,sizeof d,"r=%d atime=%ld mtime=%ld",r,(long)st.st_atime,(long)st.st_mtime); ck(r==0&&st.st_atime==500&&st.st_mtime==600,"utime() honours the time buffer",d);
    symlink(f,l); struct timespec t4[2]={{7000,0},{8000,0}}; r=utimensat(AT_FDCWD,l,t4,AT_SYMLINK_NOFOLLOW); struct stat sl; lstat(l,&sl); stat(f,&st);
    snprintf(d,sizeof d,"r=%d errno=%d link=%ld target=%ld",r,errno,(long)sl.st_mtime,(long)st.st_mtime); ck(r==0&&sl.st_mtime==8000&&st.st_mtime==600,"utimensat AT_SYMLINK_NOFOLLOW changes the link, not the target",d);
    errno=0; ck(utimensat(AT_FDCWD,f,t4,0x4)<0&&errno==EINVAL,"utimensat: unknown flag is EINVAL",0);
    if(geteuid()==0){
        r=fchown(fd,1234,5678); fstat(fd,&st); snprintf(d,sizeof d,"r=%d uid=%d gid=%d",r,(int)st.st_uid,(int)st.st_gid); ck(r==0&&st.st_uid==1234&&st.st_gid==5678,"fchown stores the owner",d);
        r=fchown(fd,-1,42); fstat(fd,&st); ck(r==0&&st.st_uid==1234&&st.st_gid==42,"fchown -1 keeps the uid",0);
        fchown(fd,0,0);
        pid_t c=fork(); if(c==0){ if(setuid(1000))_exit(9); struct timespec t5[2]={{1,0},{2,0}}; int a=utimensat(AT_FDCWD,f,t5,0)<0&&errno==EPERM; int b=fchown(fd,1000,1000)<0&&errno==EPERM; int e=utimensat(AT_FDCWD,f,0,0)<0&&errno==EACCES; struct timeval tv[2]={{1,0},{2,0}}; int u1=syscall(SYS_utimes,f,tv)<0&&errno==EPERM; int u2=syscall(SYS_futimesat,AT_FDCWD,f,0)<0&&errno==EACCES;
            _exit(a&&b&&e&&u1&&u2?0:(a?0:1)|(b?0:2)|(e?0:4)|(u1?0:8)|(u2?0:16)); }
        int ws=0; waitpid(c,&ws,0); snprintf(d,sizeof d,"status=%d",WEXITSTATUS(ws)); ck(WIFEXITED(ws)&&WEXITSTATUS(ws)==0,"non-owner: explicit times EPERM, fchown EPERM, now EACCES",d);
    }
    { struct timeval tv[2]={{0,0},{0,0}}; r=(int)syscall(SYS_utimes,f,tv); stat(f,&st); snprintf(d,sizeof d,"r=%d mtime=%ld",r,(long)st.st_mtime); ck(r==0&&st.st_mtime==0,"utimes can set the epoch (0 is not 'now')",d);
      struct timeval bad[2]={{1,1000000},{1,0}}; errno=0; ck(syscall(SYS_utimes,f,bad)<0&&errno==EINVAL,"utimes: tv_usec >= 1000000 is EINVAL",0); }
    mkdir("/tmp/kgap_d",0755); int dfd=open("/tmp/kgap_d",O_RDONLY|O_DIRECTORY); struct timespec t6[2]={{9000,0},{9100,0}}; r=futimens(dfd,t6); stat("/tmp/kgap_d",&st);
    snprintf(d,sizeof d,"fd=%d r=%d errno=%d mtime=%ld",dfd,r,errno,(long)st.st_mtime); ck(r==0&&st.st_mtime==9100,"futimens on a directory fd",d);
    close(dfd); rmdir("/tmp/kgap_d"); close(fd); unlink(l); unlink(f);
}

static void t_nofile(void){
    struct rlimit rl, o; char d[200], b[4096]; getrlimit(RLIMIT_NOFILE,&o); snprintf(d,sizeof d,"cur=%lu max=%lu",(unsigned long)o.rlim_cur,(unsigned long)o.rlim_max);
    ck(o.rlim_cur<=o.rlim_max&&o.rlim_cur>=64,"getrlimit NOFILE is sane",d);
    char nb[32]; rdf("/proc/sys/fs/nr_open",nb,sizeof nb); unsigned long nro=strtoul(nb,0,10);
    rl.rlim_cur=16; rl.rlim_max=o.rlim_max; int r=setrlimit(RLIMIT_NOFILE,&rl); struct rlimit g; getrlimit(RLIMIT_NOFILE,&g);
    snprintf(d,sizeof d,"r=%d cur=%lu",r,(unsigned long)g.rlim_cur); ck(r==0&&g.rlim_cur==16&&g.rlim_max==o.rlim_max,"setrlimit lowers the soft NOFILE limit",d);
    int fds[64], n=0, hi=-1, e=0; for(;n<64;n++){ fds[n]=open("/dev/null",O_RDONLY); if(fds[n]<0){ e=errno; break; } if(fds[n]>hi)hi=fds[n]; }
    snprintf(d,sizeof d,"opened=%d highest=%d errno=%d",n,hi,e); ck(hi==15&&e==EMFILE,"open stops at the soft limit with EMFILE",d);
    for(int i=0;i<n;i++)close(fds[i]);
    errno=0; ck(dup2(0,20)<0&&errno==EBADF,"dup2 to an fd above the limit is EBADF",0);
    errno=0; ck(fcntl(0,F_DUPFD,20)<0&&errno==EINVAL,"F_DUPFD above the limit is EINVAL",0);
    rdf("/proc/self/limits",b,sizeof b); char* l=strstr(b,"Max open files"); snprintf(d,sizeof d,"%.60s",l?l:"none"); ck(l&&atoi(l+26)==16,"/proc/self/limits shows the soft limit",d);
    pid_t c=fork(); if(c==0){ struct rlimit x; getrlimit(RLIMIT_NOFILE,&x); _exit(x.rlim_cur==16?0:1); } int ws; waitpid(c,&ws,0); ck(WIFEXITED(ws)&&WEXITSTATUS(ws)==0,"NOFILE limit is inherited on fork",0);
    rl.rlim_cur=32; rl.rlim_max=16; errno=0; ck(setrlimit(RLIMIT_NOFILE,&rl)<0&&errno==EINVAL,"setrlimit cur>max is EINVAL",0);
    if(nro<=4096){ rl.rlim_cur=RLIM_INFINITY; rl.rlim_max=RLIM_INFINITY; r=setrlimit(RLIMIT_NOFILE,&rl); getrlimit(RLIMIT_NOFILE,&g); snprintf(d,sizeof d,"r=%d errno=%d cur=%lu max=%lu nr_open=%lu",r,errno,(unsigned long)g.rlim_cur,(unsigned long)g.rlim_max,nro);
        ck(r==0&&g.rlim_cur==nro&&g.rlim_max==nro,"setrlimit above nr_open is clamped to nr_open",d);
        rl.rlim_cur=16; rl.rlim_max=16; setrlimit(RLIMIT_NOFILE,&rl); pid_t c2=fork(); if(c2==0){ setuid(1000); struct rlimit x={1024,1024}; _exit(setrlimit(RLIMIT_NOFILE,&x)<0&&errno==EPERM?0:1); } waitpid(c2,&ws,0);
        ck(WIFEXITED(ws)&&WEXITSTATUS(ws)==0,"unprivileged raise above the current hard limit is EPERM",0);
        rl.rlim_cur=16; rl.rlim_max=o.rlim_max; setrlimit(RLIMIT_NOFILE,&rl); }
    struct rlimit nw={24,o.rlim_max}, old; r=prlimit(0,RLIMIT_NOFILE,&nw,&old); getrlimit(RLIMIT_NOFILE,&g);
    snprintf(d,sizeof d,"r=%d old=%lu now=%lu",r,(unsigned long)old.rlim_cur,(unsigned long)g.rlim_cur); ck(r==0&&old.rlim_cur==16&&g.rlim_cur==24,"prlimit64 returns the old and sets the new limit",d);
    int pp[2]; pipe(pp); c=fork(); if(c==0){ char x; read(pp[0],&x,1); _exit(0); }
    struct rlimit cl; r=prlimit(c,RLIMIT_NOFILE,0,&cl); ck(r==0&&cl.rlim_cur==24,"prlimit64 reads another process",0);
    write(pp[1],"g",1); waitpid(c,&ws,0); close(pp[0]); close(pp[1]);
    r=setrlimit(RLIMIT_NOFILE,&o); getrlimit(RLIMIT_NOFILE,&g); ck(r==0&&g.rlim_cur==o.rlim_cur,"limit restored",0);
}

static void t_cmsg(void){
    int sv[2]; socketpair(AF_UNIX,SOCK_STREAM,0,sv);
    unsigned long long cb[4]={16,1ULL|(1ULL<<32),~15ULL,1ULL|(1ULL<<32)};
    struct iovec io={(void*)"z",1}; struct msghdr mh; memset(&mh,0,sizeof mh); mh.msg_iov=&io; mh.msg_iovlen=1; mh.msg_control=cb; mh.msg_controllen=32;
    long r=sendmsg(sv[0],&mh,0); char b[8]; long n=r>0?read(sv[1],b,sizeof b):0;
    char d[64]; snprintf(d,sizeof d,"r=%ld errno=%d n=%ld",r,errno,n); ck(r==1||r<0,"sendmsg: control length 2^64-16 does not hang the kernel",d);
    close(sv[0]); close(sv[1]);
}

static void t_et_sendfile(void){
    int f=open("/tmp/kgap_sf",O_RDWR|O_CREAT|O_TRUNC,0644); static char z[16384]; memset(z,'q',sizeof z); write(f,z,sizeof z);
    int sv[2]; socketpair(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK,0,sv); int ep=epoll_create1(0); struct epoll_event ev={EPOLLOUT|EPOLLET,{.u64=5}}, out[4]; epoll_ctl(ep,EPOLL_CTL_ADD,sv[0],&ev);
    int n1=epoll_wait(ep,out,4,0); off_t off=0; long tot=0; int eag=0;
    for(int i=0;i<20000;i++){ if(off>=(off_t)sizeof z)off=0; long r=sendfile(sv[0],f,&off,sizeof z); if(r<0){ eag=errno==EAGAIN; break; } tot+=r; }
    char b[4096]; while(read(sv[1],b,sizeof b)>0);
    int n2=epoll_wait(ep,out,4,0); char d[96]; snprintf(d,sizeof d,"first=%d sent=%ld eagain=%d after-drain=%d",n1,tot,eag,n2);
    ck(n1==1&&eag&&n2==1&&(out[0].events&EPOLLOUT),"epoll ET: writable again after sendfile hit EAGAIN and the peer drained (nginx sendfile)",d);
    close(ep); close(sv[0]); close(sv[1]);
    socketpair(AF_UNIX,SOCK_STREAM,0,sv); lseek(f,0,SEEK_SET); write(f,"hello",5); off=0; long r=sendfile(sv[0],f,&off,5); char g[8]={0}; long k=read(sv[1],g,5);
    snprintf(d,sizeof d,"r=%ld k=%ld off=%ld g=%s",r,k,(long)off,g); ck(r==5&&k==5&&off==5&&!memcmp(g,"hello",5),"sendfile to a Unix socket delivers the bytes",d);
    close(sv[0]); close(sv[1]); close(f); unlink("/tmp/kgap_sf");
}

static int g_tp[2], g_tres=-1;
static void* thr_fork(void* a){ (void)a; char x; read(g_tp[0],&x,1); pid_t c=fork(); if(c==0){ struct rlimit r; getrlimit(RLIMIT_NOFILE,&r); _exit((int)r.rlim_cur==20?0:1); } int ws; waitpid(c,&ws,0); g_tres=WIFEXITED(ws)?WEXITSTATUS(ws):9; return 0; }
static void t_thread_fork(void){
    struct rlimit o; getrlimit(RLIMIT_NOFILE,&o); pipe(g_tp); pthread_t t; pthread_create(&t,0,thr_fork,0);
    struct rlimit n={20,o.rlim_max}; setrlimit(RLIMIT_NOFILE,&n); write(g_tp[1],"g",1); pthread_join(t,0);
    char d[32]; snprintf(d,sizeof d,"res=%d",g_tres); ck(g_tres==0,"fork from a worker thread inherits the process NOFILE limit set later in main",d);
    setrlimit(RLIMIT_NOFILE,&o); close(g_tp[0]); close(g_tp[1]);
}

static void t_ns_cwd(void){
    pid_t c=fork(); if(c==0){ if(unshare(CLONE_NEWNS)<0)_exit(7); mkdir("/tmp/kgap_m",0755); mount("none","/tmp/kgap_m","tmpfs",0,0); pid_t me=getpid(); pid_t g=fork(); if(g==0){ if(setuid(1000))_exit(8); char pp[64], b[256]; snprintf(pp,sizeof pp,"/proc/%d/cwd",(int)me); int r=(int)readlink(pp,b,sizeof b); int e=errno;
            snprintf(pp,sizeof pp,"/proc/%d/exe",(int)me); int r2=(int)readlink(pp,b,sizeof b); int e2=errno; _exit(r<0&&e==EACCES&&r2<0&&e2==EACCES?0:1); }
        int ws; waitpid(g,&ws,0); _exit(WIFEXITED(ws)?WEXITSTATUS(ws):9); }
    int ws; waitpid(c,&ws,0); rmdir("/tmp/kgap_m"); char d[32]; snprintf(d,sizeof d,"status=%d",WEXITSTATUS(ws)); ck(WIFEXITED(ws)&&WEXITSTATUS(ws)==0,"inside a mount namespace: other user's /proc/<pid>/cwd and exe are EACCES",d);
}

int main(int argc,char** argv){
    const char* only=argc>1?argv[1]:"";
    if(!*only||strstr(only,"loginuid"))t_loginuid();
    if(!*only||strstr(only,"cpu"))t_cpudirs();
    if(!*only||strstr(only,"cwd"))t_cwd();
    if(!*only||strstr(only,"epoll"))t_epoll();
    if(!*only||strstr(only,"times"))t_times();
    if(!*only||strstr(only,"nofile"))t_nofile();
    if(!*only||strstr(only,"cmsg"))t_cmsg();
    if(!*only||strstr(only,"epoll"))t_et_sendfile();
    if(!*only||strstr(only,"nofile"))t_thread_fork();
    if(!*only||strstr(only,"cwd"))t_ns_cwd();
    printf("kgap: done %d passed, %d failed\n",g_ok,g_fail);
    return g_fail?1:0;
}
