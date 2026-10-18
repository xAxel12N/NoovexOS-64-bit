#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <sys/syscall.h>
#include <termios.h>
#include <sys/ioctl.h>

static int fails, passes;
static int g_defer_on; static char g_defer[8192]; static int g_defer_n;
static void out(const char* s){
    if(g_defer_on&&isatty(1)){ int L=(int)strlen(s); if(g_defer_n+L<(int)sizeof g_defer){ memcpy(g_defer+g_defer_n,s,(size_t)L); g_defer_n+=L; } }
    else { fputs(s,stdout); fflush(stdout); }
    int k=open("/dev/kmsg",O_WRONLY); if(k>=0){ if(write(k,s,strlen(s))<0){} close(k); }
}
static void res(const char* name,int ok,const char* detail){
    char b[300]; snprintf(b,sizeof b,"featest: %s %s%s%s\n",ok?"ok  ":"FAIL",name,detail&&*detail?" - ":"",detail?detail:""); out(b);
    if(ok)passes++; else fails++;
}
static long now_ms(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec*1000L+t.tv_nsec/1000000L; }

static void t_fds(void){
    char d[120]; int fds[200]; int n=0;
    for(;n<200;n++){ fds[n]=open("/dev/null",O_RDONLY); if(fds[n]<0)break; }
    snprintf(d,sizeof d,"opened %d (errno %d)",n,n<200?errno:0); res("open 200 files in one process",n==200,d);
    int hi=n>0?fds[n-1]:-1;
    for(int i=0;i<n;i++)close(fds[i]);
    snprintf(d,sizeof d,"highest fd %d",hi); res("fd numbers above 64",hi>64,d);
    int p[2]; if(pipe(p)){ res("select on fd 150",0,"pipe failed"); return; }
    int r=dup2(p[0],150); int w=dup2(p[1],151); close(p[0]); close(p[1]);
    if(write(w,"x",1)!=1){}
    fd_set rs; FD_ZERO(&rs); FD_SET(150,&rs); struct timeval tv={1,0};
    int s=select(152,&rs,0,0,&tv);
    snprintf(d,sizeof d,"dup2 %d/%d, select=%d set=%d",r,w,s,FD_ISSET(150,&rs)?1:0); res("select on fd 150",r==150&&w==151&&s==1&&FD_ISSET(150,&rs),d);
    struct pollfd pf={150,POLLIN,0}; s=poll(&pf,1,0);
    res("poll on fd 150",s==1&&(pf.revents&POLLIN),0);
    close(150); close(151);
    int many[60]; int ok=1;
    for(int i=0;i<60;i++){ int q[2]; if(pipe(q)){ ok=0; many[i]=-1; continue; } many[i]=q[0]; close(q[1]); }
    for(int i=0;i<60;i++) if(many[i]>=0)close(many[i]);
    res("60 pipes (120 open files)",ok,0);
}
static void t_mem(void){
    char d[120]; size_t sz=96u<<20;
    unsigned char* p=malloc(sz);
    if(!p){ res("malloc 96 MiB and touch it",0,"malloc returned NULL"); return; }
    for(size_t i=0;i<sz;i+=4096)p[i]=(unsigned char)(i>>12);
    int good=1; for(size_t i=0;i<sz;i+=4096) if(p[i]!=(unsigned char)(i>>12)){ good=0; break; }
    free(p);
    res("malloc 96 MiB and touch it",good,0);
    void* m=mmap(0,64u<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(m==MAP_FAILED){ res("mmap 64 MiB twice",0,"first mmap failed"); return; }
    void* m2=mmap(0,64u<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    good=m2!=MAP_FAILED;
    if(good){ memset(m,1,64u<<20); memset(m2,2,64u<<20); good=((char*)m)[12345]==1&&((char*)m2)[(64u<<20)-1]==2; munmap(m2,64u<<20); }
    munmap(m,64u<<20);
    snprintf(d,sizeof d,"%s",good?"128 MiB resident":"failed"); res("mmap 64 MiB twice",good,d);
}

static volatile int g_alrm;
static void on_alrm(int s){ (void)s; g_alrm++; }
static void t_timers(void){
    char d[120];
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_handler=on_alrm; sigaction(SIGALRM,&sa,0);
    g_alrm=0; long t0=now_ms();
    unsigned prev=alarm(1);
    while(!g_alrm&&now_ms()-t0<3000){ }
    long dt=now_ms()-t0;
    snprintf(d,sizeof d,"fired after %ld ms (prev %u)",dt,prev); res("alarm(1) delivers SIGALRM",g_alrm==1&&dt>=900&&dt<1600,d);
    alarm(5); unsigned left=alarm(0);
    snprintf(d,sizeof d,"alarm(0) returned %u",left); res("alarm(0) cancels and reports time left",left>=4&&left<=5,d);
    g_alrm=0; t0=now_ms();
    alarm(1); int pr=pause(); int pe=errno; dt=now_ms()-t0;
    snprintf(d,sizeof d,"pause=%d errno=%d after %ld ms",pr,pe,dt); res("pause() wakes on SIGALRM",pr==-1&&pe==EINTR&&g_alrm==1,d);
    struct itimerval it; memset(&it,0,sizeof it); it.it_value.tv_usec=50000; it.it_interval.tv_usec=50000;
    g_alrm=0; t0=now_ms();
    int sr=setitimer(ITIMER_REAL,&it,0);
    while(g_alrm<5&&now_ms()-t0<3000){ }
    dt=now_ms()-t0;
    struct itimerval cur; int gr=getitimer(ITIMER_REAL,&cur);
    memset(&it,0,sizeof it); setitimer(ITIMER_REAL,&it,0);
    int c0=g_alrm; long t1=now_ms(); while(now_ms()-t1<200){ } int c1=g_alrm;
    snprintf(d,sizeof d,"5 ticks in %ld ms, interval %ld us",dt,(long)cur.it_interval.tv_usec);
    res("setitimer 50 ms interval",sr==0&&gr==0&&c0>=5&&dt>=200&&dt<800&&cur.it_interval.tv_usec==50000,d);
    res("setitimer zero disarms",c1==c0,0);
    pid_t c=fork();
    if(c==0){ signal(SIGALRM,SIG_DFL); alarm(1); for(;;)pause(); }
    int st=0; waitpid(c,&st,0);
    snprintf(d,sizeof d,"status 0x%x",st); res("default SIGALRM ends the process",WIFSIGNALED(st)&&WTERMSIG(st)==SIGALRM,d);
    signal(SIGALRM,SIG_DFL);
}

static int sa_lo(struct sockaddr_in* a,int port){ memset(a,0,sizeof *a); a->sin_family=AF_INET; a->sin_port=htons(port); a->sin_addr.s_addr=htonl(INADDR_LOOPBACK); return 0; }
static void t_server(void){
    char d[160]; struct sockaddr_in a; sa_lo(&a,9100);
    int L=socket(AF_INET,SOCK_STREAM,0);
    int one=1; setsockopt(L,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    int b=bind(L,(struct sockaddr*)&a,sizeof a); int l=listen(L,8);
    snprintf(d,sizeof d,"socket=%d bind=%d listen=%d errno=%d",L,b,l,errno); res("listen on 127.0.0.1:9100",L>=0&&b==0&&l==0,d);
    if(L<0||b||l)return;
    int fl=fcntl(L,F_GETFL); fcntl(L,F_SETFL,fl|O_NONBLOCK);
    int e=accept(L,0,0); int ee=errno;
    snprintf(d,sizeof d,"accept=%d errno=%d",e,ee); res("non-blocking accept with no client",e<0&&ee==EAGAIN,d);
    fcntl(L,F_SETFL,fl);
    pid_t c=fork();
    if(c==0){
        close(L);
        int s=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in t; sa_lo(&t,9100);
        if(connect(s,(struct sockaddr*)&t,sizeof t))_exit(2);
        if(write(s,"ping",4)!=4)_exit(3);
        char r[8]={0}; int n=0; while(n<4){ int k=read(s,r+n,4-n); if(k<=0)_exit(4); n+=k; }
        static char big[200000]; long got=0; int k;
        while((k=read(s,big+got,sizeof big-got))>0)got+=k;
        for(long i=0;i<got;i++) if(big[i]!=(char)(i*13))_exit(6);
        close(s);
        _exit(memcmp(r,"pong",4)?5:(got==150000?0:7));
    }
    struct pollfd pf={L,POLLIN,0}; int pr=poll(&pf,1,3000);
    res("poll reports a waiting connection",pr==1&&(pf.revents&POLLIN),0);
    struct sockaddr_in peer; socklen_t pl=sizeof peer;
    int s=accept(L,(struct sockaddr*)&peer,&pl);
    snprintf(d,sizeof d,"fd=%d peer %s:%d",s,s>=0?inet_ntoa(peer.sin_addr):"-",s>=0?ntohs(peer.sin_port):0);
    res("accept a client",s>=0&&peer.sin_addr.s_addr==htonl(INADDR_LOOPBACK),d);
    if(s>=0){
        char r[8]={0}; int n=0; while(n<4){ int k=read(s,r+n,4-n); if(k<=0)break; n+=k; }
        res("server reads request",n==4&&!memcmp(r,"ping",4),0);
        int w=write(s,"pong",4);
        static char big[150000]; for(long i=0;i<(long)sizeof big;i++)big[i]=(char)(i*13);
        long sent=0; while(sent<(long)sizeof big){ int k=write(s,big+sent,sizeof big-sent); if(k<=0)break; sent+=k; }
        close(s);
        int st=0; waitpid(c,&st,0);
        snprintf(d,sizeof d,"write=%d sent=%ld child exit %d",w,sent,WIFEXITED(st)?WEXITSTATUS(st):-1);
        res("server sends 150000 bytes to client",w==4&&sent==150000&&WIFEXITED(st)&&WEXITSTATUS(st)==0,d);
    } else { kill(c,SIGKILL); waitpid(c,0,0); }
    int k=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK,0); struct sockaddr_in t; sa_lo(&t,9100);
    int cr=connect(k,(struct sockaddr*)&t,sizeof t); int ce=errno;
    int a2=accept4(L,0,0,SOCK_CLOEXEC|SOCK_NONBLOCK);
    int fdfl=a2>=0?fcntl(a2,F_GETFD):-1, stfl=a2>=0?fcntl(a2,F_GETFL):-1;
    snprintf(d,sizeof d,"connect=%d/%d accept4=%d cloexec=%d nonblock=%d",cr,ce,a2,fdfl&FD_CLOEXEC,(stfl&O_NONBLOCK)!=0);
    res("accept4 in the same process",(cr==0||ce==EINPROGRESS)&&a2>=0&&(fdfl&FD_CLOEXEC)&&(stfl&O_NONBLOCK),d);
    if(a2>=0)close(a2); close(k); close(L);
    int z=socket(AF_INET,SOCK_STREAM,0); sa_lo(&t,9199);
    int zr=connect(z,(struct sockaddr*)&t,sizeof t); int ze=errno; close(z);
    snprintf(d,sizeof d,"connect=%d errno=%d",zr,ze); res("connect to a closed port is refused",zr<0&&ze==ECONNREFUSED,d);
    int u1=socket(AF_INET,SOCK_DGRAM,0), u2=socket(AF_INET,SOCK_DGRAM,0);
    struct sockaddr_in ua; sa_lo(&ua,9101); bind(u1,(struct sockaddr*)&ua,sizeof ua);
    int sn=sendto(u2,"hello udp",9,0,(struct sockaddr*)&ua,sizeof ua);
    char ub[32]={0}; struct sockaddr_in from; socklen_t fl2=sizeof from;
    struct pollfd up={u1,POLLIN,0}; poll(&up,1,2000);
    int rn=(up.revents&POLLIN)?(int)recvfrom(u1,ub,sizeof ub,0,(struct sockaddr*)&from,&fl2):-1;
    snprintf(d,sizeof d,"sent %d got %d '%s'",sn,rn,rn>0?ub:"");
    res("UDP over loopback",sn==9&&rn==9&&!memcmp(ub,"hello udp",9),d);
    close(u1); close(u2);
}

static pthread_mutex_t g_mx=PTHREAD_MUTEX_INITIALIZER;
static long g_count;
static __thread int g_tls=5;
static void* th_count(void* a){
    long n=(long)a; for(long i=0;i<n;i++){ pthread_mutex_lock(&g_mx); g_count++; pthread_mutex_unlock(&g_mx); }
    g_tls+=(int)(n&7); return (void*)(long)g_tls;
}
static pthread_cond_t g_cv=PTHREAD_COND_INITIALIZER; static int g_q[64], g_qn, g_done;
static void* th_prod(void* a){ (void)a; for(int i=1;i<=1000;i++){ pthread_mutex_lock(&g_mx); while(g_qn==64)pthread_cond_wait(&g_cv,&g_mx); g_q[g_qn++]=i; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mx); }
    pthread_mutex_lock(&g_mx); g_done=1; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mx); return 0; }
static void* th_cons(void* a){ long sum=0; for(;;){ pthread_mutex_lock(&g_mx); while(!g_qn&&!g_done)pthread_cond_wait(&g_cv,&g_mx); if(!g_qn&&g_done){ pthread_mutex_unlock(&g_mx); break; } sum+=g_q[--g_qn]; pthread_cond_broadcast(&g_cv); pthread_mutex_unlock(&g_mx); }
    *(long*)a+=sum; return 0; }
static volatile int g_ids[4]; static volatile int g_cpu_seen[64]; static volatile int g_go;
static void* th_ids(void* a){ long k=(long)a; g_ids[k]=(int)syscall(SYS_gettid); g_ids[k+2]=getpid(); return 0; }
static void* th_spin(void* a){ (void)a; long t0=now_ms(); while(!g_go&&now_ms()-t0<2000){} t0=now_ms(); while(now_ms()-t0<300){ int c=sched_getcpu(); if(c>=0&&c<64)g_cpu_seen[c]=1; } return 0; }
static volatile int g_sig_tid, g_sig_n;
static void on_usr1(int s){ (void)s; g_sig_tid=(int)syscall(SYS_gettid); g_sig_n++; }
static void* th_wait_sig(void* a){ (void)a; long t0=now_ms(); while(!g_sig_n&&now_ms()-t0<3000)usleep(1000); return 0; }
static sem_t g_sem;
static void* th_sem(void* a){ (void)a; for(int i=0;i<100;i++)sem_post(&g_sem); return 0; }
static int g_pfd[2];
static void* th_pipe(void* a){ (void)a; usleep(50000); if(write(g_pfd[1],"thread",6)!=6){} return 0; }
static void* th_sleepy(void* a){ (void)a; for(;;)pause(); return 0; }
static void* th_exit5(void* a){ (void)a; usleep(100000); exit(5); return 0; }
static void* th_fork(void* a){ (void)a; pid_t c=fork(); if(c==0)_exit(9); int st=0; waitpid(c,&st,0); return (void*)(long)(WIFEXITED(st)?WEXITSTATUS(st):-1); }
static char* volatile g_shared;
static void* th_map(void* a){ (void)a; char* m=mmap(0,1<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); if(m==MAP_FAILED)return 0; strcpy(m+4096,"from thread"); g_shared=m; return 0; }
static void* th_nop(void* a){ return a; }
static void* th_tw(void* a){ (void)a; struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts); ts.tv_nsec+=200000000; if(ts.tv_nsec>=1000000000){ ts.tv_sec++; ts.tv_nsec-=1000000000; }
    pthread_mutex_lock(&g_mx); long t0=now_ms(); int r=pthread_cond_timedwait(&g_cv,&g_mx,&ts); long dt=now_ms()-t0; pthread_mutex_unlock(&g_mx); return (void*)(long)(r==ETIMEDOUT?dt:-1); }

static void t_threads(void){
    char d[160]; pthread_t t[8]; void* rv[8];
    g_count=0; int ok=1;
    for(int i=0;i<4;i++) if(pthread_create(&t[i],0,th_count,(void*)(long)(20000+i)))ok=0;
    int tls_ok=1; for(int i=0;i<4&&ok;i++){ pthread_join(t[i],&rv[i]); if((long)rv[i]!=5+((20000+i)&7))tls_ok=0; }
    snprintf(d,sizeof d,"counter %ld (want 80006)",g_count); res("4 threads share a mutex-protected counter",ok&&g_count==80006,d);
    res("thread-local variables are per thread",ok&&tls_ok&&g_tls==5,0);
    long sum=0; g_qn=0; g_done=0;
    pthread_create(&t[0],0,th_prod,0); pthread_create(&t[1],0,th_cons,&sum); pthread_create(&t[2],0,th_cons,&sum);
    for(int i=0;i<3;i++)pthread_join(t[i],0);
    snprintf(d,sizeof d,"sum %ld (want 500500)",sum); res("condition variable producer and 2 consumers",sum==500500,d);
    pthread_create(&t[0],0,th_ids,(void*)0); pthread_create(&t[1],0,th_ids,(void*)1); pthread_join(t[0],0); pthread_join(t[1],0);
    int me=getpid(), mt=(int)syscall(SYS_gettid);
    snprintf(d,sizeof d,"pid %d tid %d, threads %d/%d see pid %d/%d",me,mt,g_ids[0],g_ids[1],g_ids[2],g_ids[3]);
    res("getpid is shared, gettid is per thread",mt==me&&g_ids[2]==me&&g_ids[3]==me&&g_ids[0]!=me&&g_ids[1]!=me&&g_ids[0]!=g_ids[1],d);
    long ncpu=sysconf(_SC_NPROCESSORS_ONLN);
    for(int i=0;i<64;i++)g_cpu_seen[i]=0; g_go=0;
    int nsp=ncpu>4?4:(int)ncpu; for(int i=0;i<nsp;i++)pthread_create(&t[i],0,th_spin,0);
    g_go=1; for(int i=0;i<nsp;i++)pthread_join(t[i],0);
    int seen=0; for(int i=0;i<64;i++)seen+=g_cpu_seen[i];
    snprintf(d,sizeof d,"%d spinning threads ran on %d of %ld cpus",nsp,seen,ncpu); res("threads run on several cpus",ncpu<2||seen>=2,d);
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_handler=on_usr1; sigaction(SIGUSR1,&sa,0);
    g_sig_n=0; pthread_create(&t[0],0,th_wait_sig,0); usleep(20000);
    pthread_kill(t[0],SIGUSR1); pthread_join(t[0],0);
    snprintf(d,sizeof d,"handler ran %d time(s)",g_sig_n); res("pthread_kill reaches the thread",g_sig_n==1&&g_sig_tid!=mt,d);
    sigset_t ms; sigemptyset(&ms); sigaddset(&ms,SIGUSR1);
    g_sig_n=0; pthread_create(&t[0],0,th_wait_sig,0); pthread_sigmask(SIG_BLOCK,&ms,0); usleep(20000);
    kill(getpid(),SIGUSR1); pthread_join(t[0],0); pthread_sigmask(SIG_UNBLOCK,&ms,0);
    snprintf(d,sizeof d,"handled by tid %d, main is %d",g_sig_tid,mt); res("process signal goes to a thread that does not block it",g_sig_n==1&&g_sig_tid!=mt,d);
    signal(SIGUSR1,SIG_DFL);
    sem_init(&g_sem,0,0); pthread_create(&t[0],0,th_sem,0); int got=0; for(int i=0;i<100;i++){ if(sem_wait(&g_sem)==0)got++; } pthread_join(t[0],0);
    res("semaphore posted from another thread",got==100,0);
    if(pipe(g_pfd)==0){ pthread_create(&t[0],0,th_pipe,0); char b[16]={0}; int n=read(g_pfd[0],b,sizeof b-1); pthread_join(t[0],0); close(g_pfd[0]); close(g_pfd[1]);
        snprintf(d,sizeof d,"read %d '%s'",n,n>0?b:""); res("blocking read woken by a write from another thread",n==6&&!memcmp(b,"thread",6),d); }
    g_shared=0; pthread_create(&t[0],0,th_map,0); pthread_join(t[0],0);
    res("memory mapped by a thread is visible to the others",g_shared&&!strcmp(g_shared+4096,"from thread"),0);
    if(g_shared)munmap(g_shared,1<<20);
    pthread_create(&t[0],0,th_fork,0); pthread_join(t[0],&rv[0]);
    snprintf(d,sizeof d,"child status %ld",(long)rv[0]); res("fork from a thread",(long)rv[0]==9,d);
    pthread_create(&t[0],0,th_tw,0); pthread_join(t[0],&rv[0]);
    snprintf(d,sizeof d,"timed out after %ld ms",(long)rv[0]); res("pthread_cond_timedwait times out",(long)rv[0]>=150&&(long)rv[0]<600,d);
    int made=0; for(int round=0;round<10;round++){ pthread_t w[20]; int n=0; for(int i=0;i<20;i++) if(!pthread_create(&w[i],0,th_nop,(void*)(long)i))n++; for(int i=0;i<n;i++){ void* x; pthread_join(w[i],&x); if((long)x==i)made++; } }
    snprintf(d,sizeof d,"%d of 200 joined",made); res("200 short threads, 20 at a time",made==200,d);
    pid_t c=fork();
    if(c==0){ pthread_t a,b; pthread_create(&a,0,th_sleepy,0); pthread_create(&b,0,th_sleepy,0); usleep(50000); exit(7); }
    int st=0; long t0=now_ms(); waitpid(c,&st,0);
    snprintf(d,sizeof d,"status %d after %ld ms",WIFEXITED(st)?WEXITSTATUS(st):-1,now_ms()-t0); res("exit() ends all threads",WIFEXITED(st)&&WEXITSTATUS(st)==7,d);
    c=fork();
    if(c==0){ pthread_t a; pthread_create(&a,0,th_exit5,0); pthread_exit(0); }
    st=0; waitpid(c,&st,0);
    snprintf(d,sizeof d,"status %d",WIFEXITED(st)?WEXITSTATUS(st):-1); res("main thread exits first, last thread sets status",WIFEXITED(st)&&WEXITSTATUS(st)==5,d);
    c=fork();
    if(c==0){ pthread_t a; pthread_create(&a,0,th_sleepy,0); for(;;)pause(); }
    usleep(50000); kill(c,SIGTERM); st=0; waitpid(c,&st,0);
    snprintf(d,sizeof d,"status 0x%x",st); res("SIGTERM ends a threaded process",WIFSIGNALED(st)&&WTERMSIG(st)==SIGTERM,d);
    c=fork();
    if(c==0){ pthread_t a; pthread_create(&a,0,th_sleepy,0); char* av[]={"mhello",0}; execv("/bin/mhello",av); _exit(99); }
    st=0; waitpid(c,&st,0);
    snprintf(d,sizeof d,"status %d",WIFEXITED(st)?WEXITSTATUS(st):-1); res("exec from a threaded process",WIFEXITED(st)&&WEXITSTATUS(st)==0,d);
}

static void tw(const char* s){ if(write(0,s,strlen(s))<0){} }
static int dsr(int* row,int* col){
    tw("\033[6n"); char b[32]; int n=0; long t0=now_ms();
    while(n<31&&now_ms()-t0<2000){ int k=read(0,b+n,31-n); if(k>0){ n+=k; if(b[n-1]=='R')break; } }
    b[n]=0; return sscanf(b,"\033[%d;%dR",row,col)==2?0:-1;
}
static void t_tty(void){
    char d[160]; struct termios o,t,c;
    if(!isatty(0)){ res("terminal tests need the console on stdin",1,"skipped"); return; }
    int g=tcgetattr(0,&o);
    res("tcgetattr",g==0&&(o.c_lflag&ICANON)&&(o.c_lflag&ECHO),0);
    t=o; t.c_lflag&=~(ICANON|ECHO); t.c_iflag&=~ICRNL; t.c_cc[VMIN]=0; t.c_cc[VTIME]=10;
    int sr=tcsetattr(0,TCSAFLUSH,&t); tcgetattr(0,&c);
    res("tcsetattr switches to raw mode",sr==0&&!(c.c_lflag&ICANON)&&!(c.c_lflag&ECHO)&&c.c_cc[VTIME]==10,0);
    struct winsize ws; int wr=ioctl(0,TIOCGWINSZ,&ws);
    snprintf(d,sizeof d,"%dx%d",ws.ws_col,ws.ws_row); res("window size",wr==0&&ws.ws_col>=40&&ws.ws_row>=10,d);
    tw("\033[?1049h\033[2J\033[5;10H"); int r=0,col=0; int e=dsr(&r,&col);
    snprintf(d,sizeof d,"reported %d;%d",r,col); res("cursor position report after CUP 5;10",e==0&&r==5&&col==10,d);
    tw("hello"); e=dsr(&r,&col); snprintf(d,sizeof d,"reported %d;%d",r,col); res("text advances the cursor",e==0&&r==5&&col==15,d);
    tw("\033[3D\033[A"); e=dsr(&r,&col); snprintf(d,sizeof d,"reported %d;%d",r,col); res("cursor left 3, up 1",e==0&&r==4&&col==12,d);
    tw("\033[3;6r\033[6;1H\n\n"); e=dsr(&r,&col); tw("\033[r"); snprintf(d,sizeof d,"reported %d;%d",r,col); res("line feed at the bottom of a scroll region stays there",e==0&&r==6,d);
    tw("\033[1;31mred\033[0m \033[42mgreen bg\033[0m \033[38;5;208m256\033[0m \033[38;2;80;160;255mrgb\033[0m \033[7mreverse\033[0m");
    tw("\033[10;1H\033[K\033[1;1H"); e=dsr(&r,&col); res("home",e==0&&r==1&&col==1,0);
    tw("\033[s\033[20;30H\033[u"); e=dsr(&r,&col); res("save and restore cursor",e==0&&r==1&&col==1,0);
    t.c_cc[VTIME]=0; tcsetattr(0,TCSANOW,&t); char b[8]; long t0=now_ms(); int n=read(0,b,8); long dt=now_ms()-t0;
    snprintf(d,sizeof d,"read=%d after %ld ms",n,dt); res("VMIN=0 VTIME=0 read returns at once",n==0&&dt<200,d);
    t.c_cc[VTIME]=5; tcsetattr(0,TCSANOW,&t); t0=now_ms(); n=read(0,b,8); dt=now_ms()-t0;
    snprintf(d,sizeof d,"read=%d after %ld ms",n,dt); res("VTIME=5 read times out after 0.5 s",n==0&&dt>=400&&dt<1200,d);
    tw("\033[?1049l"); e=dsr(&r,&col);
    tcsetattr(0,TCSANOW,&o); tcgetattr(0,&c);
    res("leaving the alternate screen restores the cursor",e==0,0);
    res("tcsetattr restores cooked mode",(c.c_lflag&ICANON)&&(c.c_lflag&ECHO),0);
    tw("\033[1;32mcolour test:\033[0m \033[31mred \033[32mgreen \033[33myellow \033[34mblue \033[35mmagenta \033[36mcyan \033[0m\033[41m \033[42m \033[43m \033[44m \033[45m \033[46m \033[47m \033[0m\n");
}

static void t_listen(void){
    char d[160]; struct sockaddr_in a; memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_port=htons(9200); a.sin_addr.s_addr=htonl(INADDR_ANY);
    int L=socket(AF_INET,SOCK_STREAM,0); int one=1; setsockopt(L,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    int b=bind(L,(struct sockaddr*)&a,sizeof a), l=listen(L,4);
    res("listen on port 9200 for a remote client",L>=0&&b==0&&l==0,0);
    struct pollfd pf={L,POLLIN,0}; int pr=poll(&pf,1,60000);
    if(pr!=1){ res("a remote client connects",0,"no connection within 60 s"); close(L); return; }
    struct sockaddr_in peer; socklen_t pl=sizeof peer; int s=accept(L,(struct sockaddr*)&peer,&pl);
    snprintf(d,sizeof d,"from %s:%d",s>=0?inet_ntoa(peer.sin_addr):"-",s>=0?ntohs(peer.sin_port):0); res("a remote client connects",s>=0,d);
    if(s>=0){ char r[64]={0}; int n=0; while(n<63){ int k=read(s,r+n,63-n); if(k<=0)break; n+=k; if(r[n-1]=='\n')break; }
        snprintf(d,sizeof d,"got '%.*s'",n>0&&r[n-1]=='\n'?n-1:n,r); res("read the remote request",n>0&&!strncmp(r,"ping from host",14),d);
        const char* rep="pong from noovex\n"; res("reply to the remote client",write(s,rep,strlen(rep))==(ssize_t)strlen(rep),0); close(s); }
    close(L);
}

int main(int argc,char** argv){
    const char* only=argc>1?argv[1]:0;
    out("featest: start\n");
    if(!only||!strcmp(only,"fds"))t_fds();
    if(!only||!strcmp(only,"mem"))t_mem();
    if(!only||!strcmp(only,"timers"))t_timers();
    if(!only||!strcmp(only,"server"))t_server();
    if(!only||!strcmp(only,"threads"))t_threads();
    if(only&&!strcmp(only,"listen"))t_listen();
    if(!only||!strcmp(only,"tty")){ g_defer_on=1; t_tty(); g_defer_on=0; if(g_defer_n){ fwrite(g_defer,1,(size_t)g_defer_n,stdout); fflush(stdout); } }
    char b[100]; snprintf(b,sizeof b,"featest: done %d passed, %d failed\n",passes,fails); out(b);
    return fails?1:0;
}
