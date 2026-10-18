#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/random.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/time.h>

extern char** environ;
static int npass, nfail;
static const char* g_hello;
static void check(int ok,const char* name,const char* detail){
    if(ok){ npass++; printf("ok   %s\n",name); }
    else { nfail++; printf("FAIL %s%s%s\n",name,detail?": ":"",detail?detail:""); }
    fflush(stdout);
}
static char g_msg[640];
#define CHECK(c,name,...) do{ snprintf(g_msg,sizeof g_msg,__VA_ARGS__); check((c),(name),g_msg); }while(0)

static void t_args(int argc,char** argv){
    CHECK(argc>=1&&argv[0]&&argv[argc]==0,"argv","argc=%d",argc);
    CHECK(environ&&getenv("PATH"),"environ","PATH=%s",getenv("PATH")?getenv("PATH"):"(null)");
    setenv("MT_VAR","abc",1);
    CHECK(getenv("MT_VAR")&&!strcmp(getenv("MT_VAR"),"abc"),"setenv","%s",getenv("MT_VAR"));
}
static void t_printf(void){
    char b[128];
    snprintf(b,sizeof b,"%d|%x|%s|%5.2f|%e|%g|%lu",-42,255,"str",3.14159,12345.678,0.0001,(unsigned long)1<<40);
    CHECK(!strcmp(b,"-42|ff|str| 3.14|1.234568e+04|0.0001|1099511627776"),"printf","%s",b);
    double d=strtod("2.5e3",0); long l=strtol("-0x1f",0,16);
    CHECK(d==2500.0&&l==-31,"strtod/strtol","%f %ld",d,l);
}
static int cmpi(const void* a,const void* b){ return *(const int*)a-*(const int*)b; }
static void t_malloc(void){
    int ok=1;
    char* ps[200];
    for(int i=0;i<200;i++){ ps[i]=malloc(16+i*7); if(!ps[i]){ ok=0; break; } memset(ps[i],i,16+i*7); }
    for(int i=0;ok&&i<200;i++) for(int k=0;k<16+i*7;k++) if((unsigned char)ps[i][k]!=(unsigned char)i){ ok=0; break; }
    for(int i=0;i<200;i++)free(ps[i]);
    CHECK(ok,"malloc small","");
    size_t n=3*1024*1024; unsigned char* big=malloc(n);
    int ok2=big!=0; if(big){ for(size_t i=0;i<n;i+=4096)big[i]=(unsigned char)(i>>12); for(size_t i=0;i<n;i+=4096) if(big[i]!=(unsigned char)(i>>12))ok2=0; }
    char* r=malloc(10); strcpy(r,"123456789"); r=realloc(r,100000); ok2=ok2&&r&&!strcmp(r,"123456789");
    int* z=calloc(1000,sizeof(int)); int zs=0; for(int i=0;z&&i<1000;i++)zs+=z[i];
    free(big); free(r); free(z);
    CHECK(ok2&&z&&zs==0,"malloc large/realloc/calloc","");
    int a[]={5,3,9,1,7}; qsort(a,5,sizeof(int),cmpi); int key=7; int* f=bsearch(&key,a,5,sizeof(int),cmpi);
    CHECK(a[0]==1&&a[4]==9&&f&&*f==7,"qsort/bsearch","");
}
static __thread int tls_counter=5;
static void t_tls(void){
    tls_counter++;
    int fd=open("/nonexistent/zzz",O_RDONLY);
    CHECK(tls_counter==6&&fd<0&&errno==ENOENT,"tls/errno","counter=%d errno=%d",tls_counter,errno);
}
static void t_math(void){
    double a=sqrt(2.0), b=sin(M_PI/6), c=pow(2.0,10.0), d=log(exp(1.5));
    CHECK(fabs(a-1.41421356)<1e-6&&fabs(b-0.5)<1e-9&&c==1024.0&&fabs(d-1.5)<1e-12,"libm","%f %f %f %f",a,b,c,d);
}
static jmp_buf jb;
static void jumper(int v){ longjmp(jb,v); }
static void t_setjmp(void){
    volatile int n=0; int r=setjmp(jb); n++;
    if(r==0)jumper(42);
    CHECK(r==42&&n==2,"setjmp/longjmp","r=%d n=%d",r,n);
}
static void t_files(void){
    FILE* f=fopen("mt_a.txt","w"); int ok=f!=0;
    if(f){ for(int i=0;i<100;i++)fprintf(f,"line %d\n",i); fclose(f); }
    struct stat st; int sr=stat("mt_a.txt",&st);
    CHECK(ok&&sr==0&&S_ISREG(st.st_mode)&&st.st_size==790,"fopen/fprintf/stat","sr=%d size=%ld",sr,(long)st.st_size);
    f=fopen("mt_a.txt","r"); char line[64]={0}; int cnt=0; long pos=0;
    if(f){ while(fgets(line,sizeof line,f))cnt++; fseek(f,7,SEEK_SET); fgets(line,sizeof line,f); pos=ftell(f); fclose(f); }
    CHECK(cnt==100&&!strcmp(line,"line 1\n")&&pos==14,"fgets/fseek/ftell","cnt=%d line=%s pos=%ld",cnt,line,pos);
    f=fopen("mt_a.txt","a"); if(f){ fputs("tail\n",f); fclose(f); }
    stat("mt_a.txt",&st);
    CHECK(st.st_size==795,"append","size=%ld",(long)st.st_size);
    int rr=rename("mt_a.txt","mt_b.txt");
    CHECK(rr==0&&access("mt_a.txt",F_OK)!=0&&access("mt_b.txt",R_OK)==0,"rename/access","rr=%d errno=%d",rr,errno);
    int ur=unlink("mt_b.txt");
    CHECK(ur==0&&stat("mt_b.txt",&st)!=0&&errno==ENOENT,"unlink","ur=%d errno=%d",ur,errno);
    int fd=open("mt_c.bin",O_CREAT|O_RDWR|O_TRUNC,0644);
    char buf[32]={0}; int w=write(fd,"0123456789",10); off_t o=lseek(fd,2,SEEK_SET); int n=read(fd,buf,4);
    int pw=pwrite(fd,"AB",2,8); int pr=pread(fd,buf+10,10,0); int ft=ftruncate(fd,5); struct stat s2; fstat(fd,&s2); close(fd);
    CHECK(fd>=0&&w==10&&o==2&&n==4&&!memcmp(buf,"2345",4)&&pw==2&&pr==10&&!memcmp(buf+10,"01234567AB",10)&&ft==0&&s2.st_size==5,"open/read/write/lseek/pread/pwrite/ftruncate",
        "fd=%d w=%d o=%ld n=%d pw=%d pr=%d ft=%d sz=%ld",fd,w,(long)o,n,pw,pr,ft,(long)s2.st_size);
    int e=open("mt_c.bin",O_CREAT|O_EXCL|O_WRONLY,0644);
    CHECK(e<0&&errno==EEXIST,"O_EXCL","e=%d errno=%d",e,errno);
    unlink("mt_c.bin");
    void* m; fd=open("mt_d.txt",O_CREAT|O_RDWR|O_TRUNC,0644); write(fd,"mapped file data",16);
    m=mmap(0,4096,PROT_READ,MAP_PRIVATE,fd,0); close(fd);
    CHECK(m!=MAP_FAILED&&!memcmp(m,"mapped file data",16),"mmap file","m=%p",m);
    if(m!=MAP_FAILED)munmap(m,4096);
    unlink("mt_d.txt");
}
static void t_dirs(void){
    char cwd[256]; getcwd(cwd,sizeof cwd);
    int mk=mkdir("mt_dir",0755); int mk2=mkdir("mt_dir",0755); int e2=errno;
    int fd=open("mt_dir/inner.txt",O_CREAT|O_WRONLY,0644); write(fd,"x",1); close(fd);
    DIR* d=opendir("mt_dir"); int seen_dot=0, seen_dotdot=0, seen_f=0;
    if(d){ struct dirent* de; while((de=readdir(d))){ if(!strcmp(de->d_name,"."))seen_dot=1; else if(!strcmp(de->d_name,".."))seen_dotdot=1; else if(!strcmp(de->d_name,"inner.txt")&&de->d_type==DT_REG)seen_f=1; } closedir(d); }
    CHECK(mk==0&&mk2<0&&e2==EEXIST&&seen_dot&&seen_dotdot&&seen_f,"mkdir/readdir","mk=%d mk2=%d e2=%d %d%d%d",mk,mk2,e2,seen_dot,seen_dotdot,seen_f);
    int ch=chdir("mt_dir"); char now[256]; getcwd(now,sizeof now); int inner=access("inner.txt",F_OK);
    chdir(cwd);
    char want[300]; snprintf(want,sizeof want,"%s%smt_dir",cwd,strcmp(cwd,"/")?"/":"");
    CHECK(ch==0&&!strcmp(now,want)&&inner==0,"chdir/getcwd","now=%s want=%s",now,want);
    int rm1=rmdir("mt_dir"); int e3=errno; unlink("mt_dir/inner.txt"); int rm2=rmdir("mt_dir");
    struct stat st;
    CHECK(rm1<0&&e3==ENOTEMPTY&&rm2==0&&stat("mt_dir",&st)<0,"rmdir","rm1=%d e3=%d rm2=%d",rm1,e3,rm2);
}
static void t_fork(void){
    int p[2]; int pr=pipe(p);
    pid_t c=fork();
    if(c==0){ close(p[0]); char m[64]; int n=snprintf(m,sizeof m,"child %d of %d",(int)getpid(),(int)getppid()); write(p[1],m,n); close(p[1]); _exit(7); }
    close(p[1]); char buf[64]={0}; int got=0,n; while((n=read(p[0],buf+got,sizeof buf-1-got))>0)got+=n; close(p[0]);
    int st=0; pid_t w=waitpid(c,&st,0);
    char want[64]; snprintf(want,sizeof want,"child %d of %d",(int)c,(int)getpid());
    CHECK(pr==0&&c>0&&w==c&&WIFEXITED(st)&&WEXITSTATUS(st)==7&&!strcmp(buf,want),"pipe/fork/waitpid","buf=%s st=%x",buf,st);
    int st2; pid_t w2=waitpid(-1,&st2,WNOHANG);
    CHECK(w2<0&&errno==ECHILD,"ECHILD","w2=%d errno=%d",(int)w2,errno);
}
static volatile int g_sig, g_info_signo;
static void on_usr1(int s,siginfo_t* si,void* uc){ (void)uc; g_sig=s; g_info_signo=si?si->si_signo:-1; }
static void t_signals(void){
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=on_usr1; sa.sa_flags=SA_SIGINFO; sigemptyset(&sa.sa_mask);
    int r=sigaction(SIGUSR1,&sa,0); raise(SIGUSR1);
    CHECK(r==0&&g_sig==SIGUSR1&&g_info_signo==SIGUSR1,"sigaction/raise","g_sig=%d signo=%d",g_sig,g_info_signo);
    g_sig=0; sigset_t s,o,pend; sigemptyset(&s); sigaddset(&s,SIGUSR1); sigprocmask(SIG_BLOCK,&s,&o);
    raise(SIGUSR1); int before=g_sig; sigpending(&pend); int ispend=sigismember(&pend,SIGUSR1);
    sigprocmask(SIG_SETMASK,&o,0);
    CHECK(before==0&&ispend==1&&g_sig==SIGUSR1,"sigprocmask/sigpending","before=%d pend=%d after=%d",before,ispend,g_sig);
    pid_t c=fork();
    if(c==0){ for(;;)pause(); }
    int kr=kill(c,SIGTERM); int st=0; waitpid(c,&st,0);
    CHECK(kr==0&&WIFSIGNALED(st)&&WTERMSIG(st)==SIGTERM,"kill/WTERMSIG","kr=%d st=%x",kr,st);
    g_sig=0; sigaction(SIGUSR1,&sa,0); pid_t me=getpid();
    c=fork(); if(c==0){ struct timespec w={0,60000000}; nanosleep(&w,0); kill(me,SIGUSR1); _exit(0); }
    struct timespec lng={3,0}, rem={0,0}; struct timespec t0,t1; clock_gettime(CLOCK_MONOTONIC,&t0); int nr=nanosleep(&lng,&rem); int ne=errno; clock_gettime(CLOCK_MONOTONIC,&t1); waitpid(c,0,0);
    long el=(t1.tv_sec-t0.tv_sec)*1000+(t1.tv_nsec-t0.tv_nsec)/1000000;
    CHECK(nr==-1&&ne==EINTR&&g_sig==SIGUSR1&&el<2500,"nanosleep interrupted by a signal -> EINTR","nr=%d errno=%d sig=%d %ld ms",nr,ne,g_sig,el);
    g_sig=0; sigset_t bl,old2,cur; sigemptyset(&bl); sigaddset(&bl,SIGUSR1); sigprocmask(SIG_BLOCK,&bl,&old2);
    c=fork(); if(c==0){ struct timespec w={0,30000000}; nanosleep(&w,0); kill(me,SIGUSR1); _exit(0); }
    sigset_t none; sigemptyset(&none); int sr=sigsuspend(&none); int se=errno; sigprocmask(SIG_SETMASK,0,&cur); waitpid(c,0,0);
    CHECK(sr==-1&&se==EINTR&&g_sig==SIGUSR1&&sigismember(&cur,SIGUSR1)==1,"sigsuspend waits, runs the handler, restores the mask","sr=%d errno=%d sig=%d blocked=%d",sr,se,g_sig,sigismember(&cur,SIGUSR1));
    sigprocmask(SIG_SETMASK,&old2,0);
    g_sig=0; sigprocmask(SIG_BLOCK,&bl,&old2);
    c=fork(); if(c==0){ kill(me,SIGUSR1); _exit(0); }
    struct timespec w2={0,80000000}; int nr2=nanosleep(&w2,0); waitpid(c,0,0); int before2=g_sig; sigprocmask(SIG_SETMASK,&old2,0);
    CHECK(nr2==0&&before2==0&&g_sig==SIGUSR1,"a blocked signal does not cut a sleep short","nr=%d before=%d after=%d",nr2,before2,g_sig);
    signal(SIGUSR1,SIG_IGN); raise(SIGUSR1);
    CHECK(1,"SIG_IGN","");
}
static void t_time(void){
    struct timespec a,b; clock_gettime(CLOCK_MONOTONIC,&a);
    struct timespec d={0,30000000}; int nr=nanosleep(&d,0);
    clock_gettime(CLOCK_MONOTONIC,&b);
    long ms=(b.tv_sec-a.tv_sec)*1000+(b.tv_nsec-a.tv_nsec)/1000000;
    CHECK(nr==0&&ms>=25&&ms<2000,"nanosleep/clock_gettime","slept %ld ms",ms);
    time_t t=time(0); struct timespec rt; clock_gettime(CLOCK_REALTIME,&rt);
    CHECK(t!=(time_t)-1&&(rt.tv_sec-t)<=2&&(t-rt.tv_sec)<=2,"time/realtime","t=%ld rt=%ld",(long)t,(long)rt.tv_sec);
    time_t z=86400*365; struct tm g; gmtime_r(&z,&g); char sb[32]; strftime(sb,sizeof sb,"%Y-%m-%d %H:%M",&g);
    CHECK(!strcmp(sb,"1971-01-01 00:00"),"gmtime/strftime","%s",sb);
}
static void t_sys(void){
    struct utsname u; int ur=uname(&u);
    CHECK(ur==0&&u.sysname[0]&&!strcmp(u.machine,"x86_64"),"uname","%s %s %s",u.sysname,u.release,u.machine);
    CHECK(sysconf(_SC_PAGESIZE)==4096&&sysconf(_SC_NPROCESSORS_ONLN)>=1,"sysconf","page=%ld ncpu=%ld",sysconf(_SC_PAGESIZE),sysconf(_SC_NPROCESSORS_ONLN));
    unsigned char rb[64]; memset(rb,0,64); ssize_t g=getrandom(rb,64,0); int nz=0; for(int i=0;i<64;i++)nz+=rb[i]!=0;
    int fd=open("/dev/urandom",O_RDONLY); unsigned char ub[16]; ssize_t un=read(fd,ub,16); close(fd);
    CHECK(g==64&&nz>32&&un==16,"getrandom/urandom","g=%ld nz=%d un=%ld",(long)g,nz,(long)un);
    fd=open("/dev/null",O_RDONLY); char c; ssize_t nn=read(fd,&c,1); int tty=isatty(fd); int te=errno; close(fd);
    CHECK(nn==0&&tty==0&&te==ENOTTY,"/dev/null/isatty","nn=%ld tty=%d errno=%d",(long)nn,tty,te);
    int p[2]; pipe(p); int d2=dup2(p[1],20); write(20,"hi",2); close(20); close(p[1]); char b[4]={0}; read(p[0],b,3); close(p[0]);
    CHECK(d2==20&&!strcmp(b,"hi"),"dup2","d2=%d b=%s",d2,b);
    char* a=mmap(0,3*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); int ok=a!=MAP_FAILED;
    if(ok){ a[0]=1; a[3*4096-1]=2; ok=a[4096]==0&&a[0]==1&&a[3*4096-1]==2; munmap(a,3*4096); }
    CHECK(ok,"mmap anon","");
    p[0]=p[1]=-1; pipe(p); struct pollfd pf={p[0],POLLIN,0}; int r1=poll(&pf,1,0); write(p[1],"z",1); int r2=poll(&pf,1,100); short re=pf.revents; close(p[0]); close(p[1]);
    CHECK(r1==0&&r2==1&&(re&POLLIN),"poll","r1=%d r2=%d re=%x",r1,r2,re);
}
static int g_atexit_fd=-1;
static void on_exit_cb(void){ if(g_atexit_fd>=0)write(g_atexit_fd,"bye",3); }
static void t_exec(void){
    int st=0;
    pid_t c=fork();
    if(c==0){ char* av[]={(char*)"mhello",(char*)"x",0}; execve(g_hello,av,environ); _exit(99); }
    waitpid(c,&st,0);
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==3,"fork/execve","st=%x path=%s",st,g_hello);
    char* av2[]={(char*)"mhello",(char*)"env",0}; char* ev[]={(char*)"MT_VAR=fromspawn",(char*)"PATH=/bin",0};
    pid_t sp=0; int r=posix_spawn(&sp,g_hello,0,0,av2,ev); st=0; if(r==0)waitpid(sp,&st,0);
    CHECK(r==0&&WIFEXITED(st)&&WEXITSTATUS(st)==4,"posix_spawn+envp","r=%d st=%x",r,st);
    int e=execve("/no/such/prog",av2,ev);
    CHECK(e<0&&errno==ENOENT,"execve ENOENT","errno=%d",errno);
    int p[2]; pipe(p);
    c=fork();
    if(c==0){ close(p[0]); g_atexit_fd=p[1]; atexit(on_exit_cb); exit(0); }
    close(p[1]); char b[8]={0}; read(p[0],b,7); close(p[0]); waitpid(c,&st,0);
    CHECK(!strcmp(b,"bye")&&WIFEXITED(st),"atexit","b=%s",b);
}
#define ERR(expr,want,name) do{ errno=0; long _r=(long)(expr); int _e=errno; CHECK(_r==-1&&_e==(want),name,"r=%ld errno=%d want %d",_r,_e,(want)); }while(0)
static void t_errors(void){
    char* m=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); munmap(m+4096,4096);
    int fd=open("mt_e.txt",O_CREAT|O_RDWR|O_TRUNC,0644); write(fd,"abcdef",6); lseek(fd,0,SEEK_SET);
    struct stat st; char buf[64]; int p[2]; pipe2(p,O_NONBLOCK);
    ERR(read(fd,m+4096,4),EFAULT,"read into unmapped memory -> EFAULT");
    ERR(write(fd,(void*)0x100000,4),EFAULT,"write from kernel address -> EFAULT");
    ERR(stat(m+4096,&st),EFAULT,"stat with unmapped path -> EFAULT");
    ERR(lseek(p[0],0,SEEK_SET),ESPIPE,"lseek on pipe -> ESPIPE");
    ERR(read(p[0],buf,1),EAGAIN,"empty O_NONBLOCK pipe -> EAGAIN");
    ERR(dup2(50,51),EBADF,"dup2 closed fd -> EBADF");
    ERR(close(77),EBADF,"close closed fd -> EBADF");
    ERR(read(-1,buf,1),EBADF,"read fd -1 -> EBADF");
    ERR(open(".",O_WRONLY),EISDIR,"open dir for writing -> EISDIR");
    int dfd=open(".",O_RDONLY|O_DIRECTORY);
    ERR(read(dfd,buf,1),EISDIR,"read a directory fd -> EISDIR");
    close(dfd);
    ERR(syscall(SYS_getdents64,fd,buf,sizeof buf),ENOTDIR,"getdents64 on a file -> ENOTDIR");
    ERR(open("mt_e.txt",O_RDONLY|O_DIRECTORY),ENOTDIR,"O_DIRECTORY on a file -> ENOTDIR");
    ERR(open("mt_e.txt/x",O_RDONLY),ENOTDIR,"path through a file -> ENOTDIR");
    ERR(mkdir("mt_e.txt/x",0755),ENOTDIR,"mkdir under a file -> ENOTDIR");
    ERR(rmdir("mt_e.txt"),ENOTDIR,"rmdir a file -> ENOTDIR");
    ERR(chdir("mt_e.txt"),ENOTDIR,"chdir to a file -> ENOTDIR");
    ERR(unlink("."),EISDIR,"unlink a directory -> EISDIR");
    ERR(rename("mt_nonexist","mt_x"),ENOENT,"rename missing -> ENOENT");
    ERR(mmap(0,0,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0),EINVAL,"mmap length 0 -> EINVAL");
    ERR(munmap(m+1,4096),EINVAL,"munmap unaligned -> EINVAL");
    ERR(kill(999999,0),ESRCH,"kill missing pid -> ESRCH");
    ERR(waitpid(12345,0,0),ECHILD,"waitpid non-child -> ECHILD");
    ERR(ioctl(fd,TIOCGWINSZ,buf),ENOTTY,"ioctl TIOCGWINSZ on a file -> ENOTTY");
    ERR(fcntl(fd,9999),EINVAL,"fcntl bad command -> EINVAL");
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_handler=SIG_IGN;
    ERR(sigaction(SIGKILL,&sa,0),EINVAL,"sigaction SIGKILL -> EINVAL");
    ERR(getcwd(buf,1)?0:-1,ERANGE,"getcwd tiny buffer -> ERANGE");
    signal(SIGPIPE,SIG_IGN); close(p[0]);
    ERR(write(p[1],"x",1),EPIPE,"write to pipe without reader -> EPIPE");
    signal(SIGPIPE,SIG_DFL); close(p[1]);
    close(fd); unlink("mt_e.txt"); munmap(m,4096);
}
static void t_fixes(void){
    char buf[256]; int fd=open("mt_g.bin",O_CREAT|O_RDWR|O_TRUNC,0644);
    write(fd,"ab",2); lseek(fd,100,SEEK_SET); write(fd,"Z",1); lseek(fd,0,SEEK_SET);
    memset(buf,0x55,sizeof buf); int n=read(fd,buf,200); int zero=1; for(int i=2;i<100;i++)if(buf[i])zero=0;
    CHECK(n==101&&zero&&buf[100]=='Z',"write past end leaves a zero-filled gap","n=%d zero=%d",n,zero);
    ERR(pwrite(open("mt_g.bin",O_RDONLY),"x",1,0),EBADF,"pwrite on a read-only fd -> EBADF");
    int ro=open("mt_g.bin",O_RDONLY);
    ERR(ftruncate(ro,1),EINVAL,"ftruncate on a read-only fd -> EINVAL");
    close(ro); close(fd);
    for(int k=3;k<64;k++){ struct stat st; if(fstat(k,&st)==0&&S_ISREG(st.st_mode))close(k); }
    fd=open("mt_g.bin",O_RDONLY); unlink("mt_g.bin");
    int f2=open("mt_h.txt",O_CREAT|O_RDWR|O_TRUNC,0644); write(f2,"SECRETNEW",9); close(f2);
    memset(buf,0,sizeof buf); read(fd,buf,9); close(fd);
    CHECK(strcmp(buf,"SECRETNEW")!=0,"a deleted-but-open file never shows another file's data","read=%.9s",buf);
    unlink("mt_h.txt");
    f2=open("mt_i.txt",O_CREAT|O_WRONLY|O_TRUNC,0644); write(f2,"linked",6); close(f2);
    int lk=link("mt_i.txt","mt_j.txt"); unlink("mt_i.txt"); memset(buf,0,sizeof buf); f2=open("mt_j.txt",O_RDONLY); read(f2,buf,6); close(f2);
    CHECK(lk==0&&!strcmp(buf,"linked"),"hard link keeps the data after the first name is removed","lk=%d buf=%s",lk,buf);
    mkdir("mt_k",0755); link("mt_j.txt","mt_k/l");
    ERR(rmdir("mt_k"),ENOTEMPTY,"rmdir of a folder holding only a hard link -> ENOTEMPTY");
    unlink("mt_k/l"); rmdir("mt_k"); unlink("mt_j.txt");
    char l63[64], l70[71]; memset(l63,'q',63); l63[63]=0; memset(l70,'q',70); l70[70]=0;
    f2=open(l63,O_CREAT|O_WRONLY|O_TRUNC,0644); close(f2);
    errno=0; int lo=open(l70,O_RDONLY); int le=errno; if(lo>=0)close(lo);
    CHECK(lo<0&&(le==ENOENT||le==ENAMETOOLONG),"a 70-character name never opens the 63-character file","fd=%d errno=%d",lo,le);
    unlink(l63);
    char d31[32], d40[41]; memset(d31,'w',31); d31[31]=0; memset(d40,'w',40); d40[40]=0;
    mkdir(d31,0755); struct stat st; errno=0; int sr=stat(d40,&st); int se=errno;
    CHECK(sr<0&&(se==ENOENT||se==ENAMETOOLONG),"a 40-character folder name never matches a 31-character folder","sr=%d errno=%d",sr,se);
    rmdir(d31);
    f2=open("mt_flat.txt",O_CREAT|O_WRONLY|O_TRUNC,0644); close(f2); mkdir("mt_sub",0755); chdir("mt_sub");
    ERR(open("mt_flat.txt",O_RDONLY),ENOENT,"a bare name only looks in the current folder");
    chdir(".."); rmdir("mt_sub"); unlink("mt_flat.txt");
    struct timeval tv={-1,0}; fd_set rs; FD_ZERO(&rs);
    ERR(select(1,&rs,0,0,&tv),EINVAL,"select with a negative timeout -> EINVAL");
    f2=open("mt_s.bin",O_CREAT|O_RDWR|O_TRUNC,0644); for(int i=0;i<20000;i++)write(f2,"k",1); lseek(f2,0,SEEK_SET);
    int pp[2]; pipe(pp); pid_t c=fork();
    if(c==0){ close(pp[1]); long tot=0; int r; char b[512]; struct timespec w={0,2000000}; while((r=read(pp[0],b,sizeof b))>0){ tot+=r; nanosleep(&w,0); } _exit(tot==20000?0:1); }
    close(pp[0]); long sent=0; ssize_t r; while(sent<20000&&(r=sendfile(pp[1],f2,0,20000-sent))>0)sent+=r; close(pp[1]); close(f2); int cst=0; waitpid(c,&cst,0);
    CHECK(sent==20000&&WIFEXITED(cst)&&WEXITSTATUS(cst)==0,"sendfile into a full pipe waits instead of failing","sent=%ld st=%x",sent,cst);
    unlink("mt_s.bin");
    fd=open("/proc/version",O_RDONLY); if(fd>=0){ lseek(fd,0x7ffffff0,SEEK_SET); n=read(fd,buf,16); close(fd);
        CHECK(n==0,"reading /proc far past the end returns nothing","n=%d",n); }
    if(getuid()==0){
        f2=open("mt_root.txt",O_CREAT|O_WRONLY|O_TRUNC,0644); write(f2,"rootdata",8); close(f2);
        f2=open("mt_sec.txt",O_CREAT|O_WRONLY|O_TRUNC,0600); write(f2,"secret",6); close(f2);
        c=fork();
        if(c==0){ if(setuid(1000))_exit(64); int bad=0;
            if(open("mt_root.txt",O_RDONLY|O_TRUNC)>=0||errno!=EACCES)bad|=1;
            if(open("mt_root.txt",O_WRONLY)>=0||errno!=EACCES)bad|=2;
            if(open("mt_sec.txt",O_RDONLY)>=0||errno!=EACCES)bad|=4;
            if(unlink("mt_root.txt")==0)bad|=8;
            if(truncate("mt_root.txt",0)==0)bad|=16;
            int ok=open("mt_root.txt",O_RDONLY); if(ok<0)bad|=32; else close(ok);
            _exit(bad); }
        int cs=0; waitpid(c,&cs,0); struct stat s2; stat("mt_root.txt",&s2);
        CHECK(WIFEXITED(cs)&&WEXITSTATUS(cs)==0&&s2.st_size==8,"another user cannot read, empty, write or delete root's files","bits=%x size=%ld",WIFEXITED(cs)?WEXITSTATUS(cs):-1,(long)s2.st_size);
        unlink("mt_root.txt"); unlink("mt_sec.txt"); }
}
static int deep(int n,volatile char* prev){ volatile char pad[1024]; pad[0]=(char)n; pad[1023]=prev?prev[0]:0; if(!n)return 0; int r=deep(n-1,pad); return r+(pad[0]!=(char)n)+(prev&&pad[1023]!=prev[0]); }
static void t_cow(void){
    size_t n=1<<20; unsigned char* big=malloc(n); memset(big,'P',n);
    pid_t c=fork();
    if(c==0){ int ok=1; for(size_t i=0;i<n;i+=97)if(big[i]!='P')ok=0; memset(big,'C',n/2); for(size_t i=0;i<n/2;i+=89)if(big[i]!='C')ok=0; _exit(ok?0:1); }
    int st=0; waitpid(c,&st,0); int pok=1; for(size_t i=0;i<n;i+=101)if(big[i]!='P')pok=0;
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0&&pok,"fork: the child's writes never reach the parent","st=%x parent_ok=%d",st,pok);
    int p2[2]; pipe(p2);
    c=fork();
    if(c==0){ close(p2[1]); char go; read(p2[0],&go,1); int ok=1; for(size_t i=0;i<n;i+=97)if(big[i]!='P')ok=0; _exit(ok?0:1); }
    close(p2[0]); memset(big,'Q',n); write(p2[1],"g",1); close(p2[1]); waitpid(c,&st,0);
    CHECK(WIFEXITED(st)&&WEXITSTATUS(st)==0&&big[n-1]=='Q',"fork: the parent's later writes never reach the child","st=%x",st);
    pid_t cs[4]; for(int k=0;k<4;k++){ cs[k]=fork(); if(cs[k]==0){ big[k*4096]=(unsigned char)('a'+k); _exit(big[k*4096]=='a'+k&&big[(k+1)*4096]=='Q'?0:1); } }
    int allok=1; for(int k=0;k<4;k++){ waitpid(cs[k],&st,0); if(!WIFEXITED(st)||WEXITSTATUS(st))allok=0; }
    CHECK(allok&&big[0]=='Q'&&big[4096]=='Q',"four forked children share and then split the same pages","allok=%d",allok);
    free(big);
    size_t m=8u<<20; unsigned char* lazy=mmap(0,m,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); int lok=lazy!=MAP_FAILED;
    if(lok){ for(size_t i=0;i<m;i+=1<<20){ if(lazy[i])lok=0; lazy[i]=(unsigned char)(i>>20); } for(size_t i=0;i<m;i+=1<<20)if(lazy[i]!=(unsigned char)(i>>20))lok=0; munmap(lazy,m); }
    CHECK(lok,"an 8 MB mapping only uses memory where it is touched","ok=%d",lok);
    int d=deep(40,0);
    CHECK(d==0,"stack pages appear on demand (40 KB of recursion)","d=%d",d);
}
static void t_vfs(void){
    char buf[64]; int fd=open("mt_orph.txt",O_CREAT|O_RDWR|O_TRUNC,0644); write(fd,"still here",10);
    int u=unlink("mt_orph.txt"); struct stat st; int gone=stat("mt_orph.txt",&st)!=0;
    lseek(fd,0,SEEK_SET); memset(buf,0,sizeof buf); int n=read(fd,buf,10); fstat(fd,&st);
    CHECK(u==0&&gone&&n==10&&!memcmp(buf,"still here",10)&&st.st_nlink==0,"an unlinked file stays readable through an open fd","n=%d nlink=%ld",n,(long)st.st_nlink);
    write(fd," more",5); lseek(fd,0,SEEK_SET); n=read(fd,buf,15); close(fd);
    CHECK(n==15&&!memcmp(buf,"still here more",15),"an unlinked open file can still grow","n=%d",n);
    fd=open("mt_app.txt",O_CREAT|O_WRONLY|O_TRUNC|O_APPEND,0644); write(fd,"ab",2); lseek(fd,0,SEEK_SET); write(fd,"cd",2); close(fd);
    fd=open("mt_app.txt",O_RDONLY); memset(buf,0,sizeof buf); n=read(fd,buf,8); close(fd); unlink("mt_app.txt");
    CHECK(n==4&&!memcmp(buf,"abcd",4),"O_APPEND writes at the end even after lseek","n=%d %.4s",n,buf);
    const char* ln="mt_a_directory_name_longer_than_thirty_one_chars";
    int mk=mkdir(ln,0755); int ch=chdir(ln); char cw[256]; getcwd(cw,sizeof cw); chdir("..");
    CHECK(mk==0&&ch==0&&strstr(cw,ln)!=0,"directory names longer than 31 characters","mk=%d ch=%d",mk,ch);
    mkdir("mt_dsrc",0755); mkdir("mt_ddst",0755); fd=open("mt_dsrc/f",O_CREAT|O_WRONLY,0644); close(fd);
    int rr=rename("mt_dsrc","mt_ddst"); int moved=stat("mt_ddst/f",&st)==0&&stat("mt_dsrc",&st)!=0;
    CHECK(rr==0&&moved,"rename of a directory replaces an empty directory","r=%d moved=%d",rr,moved);
    unlink("mt_ddst/f"); rmdir("mt_ddst");
    mkdir(ln,0755); char sub[128]; snprintf(sub,sizeof sub,"%s/x",ln); mkdir(sub,0755); snprintf(sub,sizeof sub,"%s/y",ln); mkdir(sub,0755);
    stat(ln,&st); CHECK(st.st_nlink==4,"a directory's link count is 2 plus its subdirectories","nlink=%ld",(long)st.st_nlink);
    snprintf(sub,sizeof sub,"%s/x",ln); rmdir(sub); snprintf(sub,sizeof sub,"%s/y",ln); rmdir(sub); rmdir(ln);
    fd=open("mt_ino.txt",O_CREAT|O_WRONLY,0644); close(fd); struct stat fs1; stat("mt_ino.txt",&fs1);
    DIR* dp=opendir("."); struct dirent* de; long dino=-1; while(dp&&(de=readdir(dp))) if(!strcmp(de->d_name,"mt_ino.txt"))dino=(long)de->d_ino; if(dp)closedir(dp); unlink("mt_ino.txt");
    CHECK(dino==(long)fs1.st_ino,"readdir inode numbers match stat","d_ino=%ld st_ino=%ld",dino,(long)fs1.st_ino);
}
int main(int argc,char** argv){
    setvbuf(stdout,0,_IOLBF,0);
    g_hello=getenv("MT_HELLO"); if(!g_hello)g_hello="/bin/mhello";
    const char* dir=getenv("MT_DIR"); if(!dir)dir="/tmp";
    char wd[128]; snprintf(wd,sizeof wd,"%s/mt%d",dir,(int)getpid());
    if(mkdir(wd,0755)==0)chdir(wd);
    printf("musltest: musl libc self-test\n");
    t_args(argc,argv); t_printf(); t_malloc(); t_tls(); t_math(); t_setjmp();
    t_files(); t_dirs(); t_fork(); t_signals(); t_time(); t_sys(); t_exec(); t_errors(); t_fixes(); t_cow(); t_vfs();
    chdir(dir); rmdir(wd);
    printf("musltest: %d passed, %d failed\n",npass,nfail);
    return nfail;
}
