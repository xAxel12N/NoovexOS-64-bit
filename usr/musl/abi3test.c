#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/klog.h>
#include <sys/timex.h>

static int passed, failed;
static void ok(int c,const char* what){ if(c)passed++; else failed++; printf("abi3: %s %s\n",c?"ok":"FAIL",what); fflush(stdout); }
static long sc(long n,long a,long b,long c,long d,long e,long f){ return syscall(n,a,b,c,d,e,f); }
static void ms(int m){ struct timespec t={m/1000,(m%1000)*1000000L}; nanosleep(&t,0); }
union semun { int val; struct semid_ds* buf; unsigned short* array; };

static void t_sem(void){
    int id=semget(IPC_PRIVATE,3,0600); ok(id>=0,"semget creates a set of 3");
    union semun u; u.val=0; ok(semctl(id,0,SETVAL,u)==0&&semctl(id,0,GETVAL)==0,"SETVAL/GETVAL");
    struct sembuf d={0,-1,IPC_NOWAIT}; ok(semop(id,&d,1)==-1&&errno==EAGAIN,"semop would block: EAGAIN with IPC_NOWAIT");
    pid_t c=fork();
    if(c==0){ struct sembuf w={0,-1,0}; _exit(semop(id,&w,1)==0?7:1); }
    ms(300); ok(semctl(id,0,GETNCNT)==1,"GETNCNT counts the blocked child");
    struct sembuf up={0,1,0}; semop(id,&up,1);
    int st=0; waitpid(c,&st,0); ok(WIFEXITED(st)&&WEXITSTATUS(st)==7,"blocked semop completes after another process adds 1");
    struct timespec to={0,200000000}; struct sembuf w2={1,-1,0}; ok(semtimedop(id,&w2,1,&to)==-1&&errno==EAGAIN,"semtimedop times out with EAGAIN");
    c=fork(); if(c==0){ struct sembuf a={2,5,SEM_UNDO}; semop(id,&a,1); _exit(0); }
    waitpid(c,&st,0); ok(semctl(id,2,GETVAL)==0,"SEM_UNDO is undone when the process exits");
    struct sembuf two[2]={{1,1,0},{2,2,0}}; semop(id,two,2); unsigned short all[3]; u.array=all; ok(semctl(id,0,GETALL,u)==0&&all[1]==1&&all[2]==2,"several operations at once, GETALL");
    struct semid_ds ds; u.buf=&ds; ok(semctl(id,0,IPC_STAT,u)==0&&ds.sem_nsems==3,"IPC_STAT reports 3 semaphores");
    c=fork(); if(c==0){ struct sembuf w={0,-5,0}; int r=semop(id,&w,1); _exit(r==-1&&errno==EIDRM?9:1); }
    ms(300); semctl(id,0,IPC_RMID); waitpid(c,&st,0); ok(WIFEXITED(st)&&WEXITSTATUS(st)==9,"removing the set wakes a waiter with EIDRM");
    ok(semctl(id,0,GETVAL)==-1&&errno==EINVAL,"removed set is gone");
}
struct m1 { long t; char s[32]; };
static void t_msg(void){
    int q=msgget(IPC_PRIVATE,0600); ok(q>=0,"msgget");
    struct m1 a={3,"three"}, b={1,"one"}, c={2,"two"};
    ok(msgsnd(q,&a,6,0)==0&&msgsnd(q,&b,4,0)==0&&msgsnd(q,&c,4,0)==0,"msgsnd three messages");
    struct m1 r; ok(msgrcv(q,&r,32,2,0)==4&&r.t==2&&!strcmp(r.s,"two"),"msgrcv by type");
    ok(msgrcv(q,&r,32,-3,0)==4&&r.t==1,"negative type takes the lowest type");
    ok(msgrcv(q,&r,2,0,IPC_NOWAIT)==-1&&errno==E2BIG,"too small buffer: E2BIG, message kept");
    ok(msgrcv(q,&r,2,0,MSG_NOERROR)==2&&r.t==3,"MSG_NOERROR truncates");
    ok(msgrcv(q,&r,32,0,IPC_NOWAIT)==-1&&errno==ENOMSG,"empty queue: ENOMSG");
    pid_t k=fork(); if(k==0){ struct m1 x; long n=msgrcv(q,&x,32,5,0); _exit(n==3&&!strcmp(x.s,"hi")?4:1); }
    ms(300); struct m1 h={5,"hi"}; msgsnd(q,&h,3,0); int st; waitpid(k,&st,0); ok(WIFEXITED(st)&&WEXITSTATUS(st)==4,"blocked msgrcv gets a message from another process");
    msgsnd(q,&a,6,0); struct msqid_ds ds; ok(msgctl(q,IPC_STAT,&ds)==0&&ds.msg_qnum==1&&ds.msg_cbytes==6,"IPC_STAT counts messages and bytes");
    ok(msgctl(q,IPC_RMID,0)==0&&msgsnd(q,&a,6,0)==-1&&errno==EINVAL,"IPC_RMID removes the queue");
}
static volatile int g_mqsig;
static void onusr1(int s,siginfo_t* i,void* u){ (void)u; if(s==SIGUSR1&&i->si_code==SI_MESGQ)g_mqsig=1; }
static void t_mq(void){
    mq_unlink("/abi3q");
    struct mq_attr at={0,4,64,0}; mqd_t m=mq_open("/abi3q",O_CREAT|O_RDWR|O_EXCL,0600,&at); ok(m!=(mqd_t)-1,"mq_open creates a queue");
    ok(mq_send(m,"low",4,1)==0&&mq_send(m,"high",5,9)==0&&mq_send(m,"mid",4,5)==0,"mq_send with priorities");
    char b[64]; unsigned pr=0; ok(mq_receive(m,b,64,&pr)==5&&pr==9&&!strcmp(b,"high"),"highest priority first");
    ok(mq_receive(m,b,10,0)==-1&&errno==EMSGSIZE,"buffer smaller than the message size: EMSGSIZE");
    struct mq_attr g; ok(mq_getattr(m,&g)==0&&g.mq_curmsgs==2&&g.mq_maxmsg==4&&g.mq_msgsize==64,"mq_getattr");
    mq_receive(m,b,64,0); mq_receive(m,b,64,0);
    struct timespec ts; clock_gettime(CLOCK_REALTIME,&ts); ts.tv_nsec+=200000000; if(ts.tv_nsec>=1000000000){ ts.tv_sec++; ts.tv_nsec-=1000000000; }
    ok(mq_timedreceive(m,b,64,0,&ts)==-1&&errno==ETIMEDOUT,"mq_timedreceive times out");
    struct mq_attr nb={O_NONBLOCK,0,0,0}; mq_setattr(m,&nb,0); ok(mq_receive(m,b,64,0)==-1&&errno==EAGAIN,"O_NONBLOCK: EAGAIN");
    struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=onusr1; sa.sa_flags=SA_SIGINFO; sigaction(SIGUSR1,&sa,0);
    struct sigevent se; memset(&se,0,sizeof se); se.sigev_notify=SIGEV_SIGNAL; se.sigev_signo=SIGUSR1; ok(mq_notify(m,&se)==0,"mq_notify registers a signal");
    mq_send(m,"x",2,0); for(int i=0;i<50&&!g_mqsig;i++)ms(10); ok(g_mqsig==1,"a message to an empty queue sends the signal with SI_MESGQ");
    struct pollfd pf={m,POLLIN,0}; ok(poll(&pf,1,0)==1&&(pf.revents&POLLIN),"poll sees the waiting message");
    ok(mq_close(m)==0&&mq_unlink("/abi3q")==0&&mq_open("/abi3q",O_RDWR)==(mqd_t)-1&&errno==ENOENT,"mq_unlink removes the name");
}
static void t_pidfd(void){
    int p2[2]; pipe(p2);
    pid_t c=fork(); if(c==0){ close(p2[0]); int f=open("/tmp/abi3pf",O_CREAT|O_RDWR|O_TRUNC,0600); dup2(f,40); write(40,"pidfd",5); write(p2[1],"r",1); ms(1500); _exit(3); }
    close(p2[1]); char z; read(p2[0],&z,1); close(p2[0]);
    int pf=(int)sc(SYS_pidfd_open,c,0,0,0,0,0); ok(pf>=0,"pidfd_open");
    struct pollfd q={pf,POLLIN,0}; ok(poll(&q,1,0)==0,"pidfd not readable while the child runs");
    int g=(int)sc(438,pf,40,0,0,0,0); char b[8]={0}; ok(g>=0&&pread(g,b,5,0)==5&&!memcmp(b,"pidfd",5),"pidfd_getfd copies an open file from the child");
    ok(sc(SYS_kcmp,getpid(),c,0,g,40,0)==0,"kcmp: the copied fd is the same open file");
    ok(sc(SYS_pidfd_send_signal,pf,SIGTERM,0,0,0,0)==0,"pidfd_send_signal");
    ok(poll(&q,1,3000)==1&&(q.revents&POLLIN),"pidfd readable after the child exits");
    siginfo_t si; memset(&si,0,sizeof si); ok(waitid(3,pf,&si,WEXITED)==0&&si.si_pid==c&&si.si_code==CLD_KILLED&&si.si_status==SIGTERM,"waitid(P_PIDFD) reaps the child");
    ok(sc(SYS_pidfd_send_signal,pf,SIGTERM,0,0,0,0)==-1&&errno==ESRCH,"signal to a reaped process: ESRCH");
    close(pf); close(g); unlink("/tmp/abi3pf");
}
static void t_splice(void){
    int f=open("/tmp/abi3a",O_CREAT|O_RDWR|O_TRUNC,0600); char big[20000]; for(int i=0;i<20000;i++)big[i]=(char)(i*7); write(f,big,20000);
    int p[2]; pipe(p); loff_t off=100;
    ok(splice(f,&off,p[1],0,3000,0)==3000&&off==3100,"splice file to pipe with an offset");
    int o=open("/tmp/abi3b",O_CREAT|O_RDWR|O_TRUNC,0600);
    int q[2]; pipe(q); ok(tee(p[0],q[1],3000,0)==3000,"tee copies pipe data without using it up");
    ok(splice(p[0],0,o,0,3000,0)==3000,"splice pipe to file");
    char r[3000]; pread(o,r,3000,0); ok(!memcmp(r,big+100,3000),"spliced data is correct");
    ok(read(q[0],r,3000)==3000&&!memcmp(r,big+100,3000),"tee'd data is correct");
    struct iovec iv[2]={{"vm",2},{"splice",6}}; ok(vmsplice(q[1],iv,2,0)==8&&read(q[0],r,8)==8&&!memcmp(r,"vmsplice",8),"vmsplice into a pipe");
    loff_t a=1000, b2=0; int t=open("/tmp/abi3c",O_CREAT|O_RDWR|O_TRUNC,0600);
    ok(copy_file_range(f,&a,t,&b2,15000,0)==15000&&a==16000&&b2==15000,"copy_file_range with offsets");
    char *x=malloc(15000); pread(t,x,15000,0); ok(!memcmp(x,big+1000,15000),"copied data is correct"); free(x);
    ok(splice(f,0,o,0,10,0)==-1&&errno==EINVAL,"splice between two files: EINVAL");
    close(f); close(o); close(t); close(p[0]); close(p[1]); close(q[0]); close(q[1]); unlink("/tmp/abi3a"); unlink("/tmp/abi3b"); unlink("/tmp/abi3c");
}
struct oh { uint64_t flags, mode, resolve; };
static void t_openat2(void){
    mkdir("/tmp/abi3d",0700); mkdir("/tmp/abi3d/in",0700); int f=open("/tmp/abi3d/in/f",O_CREAT|O_WRONLY,0600); close(f); symlink("/tmp/abi3d/in/f","/tmp/abi3d/ln");
    int d=open("/tmp/abi3d",O_RDONLY|O_DIRECTORY);
    struct oh h={O_RDONLY,0,0}; int r=(int)sc(437,d,(long)"in/f",(long)&h,sizeof h,0,0); ok(r>=0,"openat2 plain"); if(r>=0)close(r);
    h.resolve=8; ok(sc(437,d,(long)"../abi3d/in/f",(long)&h,sizeof h,0,0)==-1&&errno==EXDEV,"RESOLVE_BENEATH refuses ..");
    ok(sc(437,d,(long)"/etc/passwd",(long)&h,sizeof h,0,0)==-1&&errno==EXDEV,"RESOLVE_BENEATH refuses an absolute path");
    h.resolve=0x10; r=(int)sc(437,d,(long)"/in/f",(long)&h,sizeof h,0,0); ok(r>=0,"RESOLVE_IN_ROOT treats / as the directory"); if(r>=0)close(r);
    h.resolve=4; ok(sc(437,d,(long)"ln",(long)&h,sizeof h,0,0)==-1&&errno==ELOOP,"RESOLVE_NO_SYMLINKS: ELOOP");
    symlink("/etc","/tmp/abi3d/in/esc"); h.resolve=8; ok(sc(437,d,(long)"in/esc/passwd",(long)&h,sizeof h,0,0)==-1&&errno==EXDEV,"RESOLVE_BENEATH refuses a symlink that leads out"); unlink("/tmp/abi3d/in/esc");
    h.resolve=0x100; ok(sc(437,d,(long)"in/f",(long)&h,sizeof h,0,0)==-1&&errno==EINVAL,"unknown resolve bit: EINVAL");
    ok(sc(437,d,(long)"in/f",(long)&h,16,0,0)==-1&&errno==EINVAL,"too small struct: EINVAL");
    close(d); unlink("/tmp/abi3d/ln"); unlink("/tmp/abi3d/in/f"); rmdir("/tmp/abi3d/in"); rmdir("/tmp/abi3d");
}
static void t_fifo(void){
    unlink("/tmp/abi3fifo"); ok(mkfifo("/tmp/abi3fifo",0600)==0,"mkfifo");
    struct stat st; ok(stat("/tmp/abi3fifo",&st)==0&&S_ISFIFO(st.st_mode),"stat shows a FIFO");
    ok(open("/tmp/abi3fifo",O_WRONLY|O_NONBLOCK)==-1&&errno==ENXIO,"write end without a reader: ENXIO");
    pid_t c=fork(); if(c==0){ ms(300); int w=open("/tmp/abi3fifo",O_WRONLY); write(w,"through fifo",12); close(w); _exit(0); }
    int r=open("/tmp/abi3fifo",O_RDONLY); char b[32]={0}; int n=0, k; while((k=(int)read(r,b+n,31-n))>0)n+=k;
    ok(r>=0&&n==12&&!memcmp(b,"through fifo",12),"reader blocks until a writer opens, then gets the data and EOF");
    int s2; waitpid(c,&s2,0); close(r);
    int rw=open("/tmp/abi3fifo",O_RDWR); char t2[4]={0}; ok(rw>=0&&write(rw,"abc",3)==3&&read(rw,t2,3)==3&&!memcmp(t2,"abc",3),"O_RDWR FIFO can be written and read"); close(rw);
    int w0=open("/tmp/abi3fifo",O_RDWR); int nbr=open("/tmp/abi3fifo",O_RDONLY|O_NONBLOCK); ok(nbr>=0&&read(nbr,t2,3)==-1&&errno==EAGAIN,"O_NONBLOCK FIFO read with a writer and no data: EAGAIN"); close(nbr); close(w0);
    unlink("/tmp/abi3fifo");
    ok(mknod("/tmp/abi3reg",S_IFREG|0600,0)==0&&stat("/tmp/abi3reg",&st)==0&&S_ISREG(st.st_mode),"mknod makes a regular file"); unlink("/tmp/abi3reg");
}
static int g_fw[2];
static void* waker(void* a){ (void)a; ms(200); __atomic_store_n(&g_fw[1],1,__ATOMIC_SEQ_CST); syscall(SYS_futex,&g_fw[1],129,1,0,0,0); return 0; }
struct fwv { uint64_t val, uaddr; uint32_t flags, res; };
static void t_misc(void){
    struct fwv w[2]={{0,(uint64_t)&g_fw[0],0x82,0},{0,(uint64_t)&g_fw[1],0x82,0}};
    pthread_t th; pthread_create(&th,0,waker,0);
    long r=sc(449,(long)w,2,0,0,0,0); pthread_join(th,0); ok(r==1,"futex_waitv returns the index of the woken futex");
    ok(sc(449,(long)w,2,0,0,0,0)==-1&&errno==EAGAIN,"futex_waitv with a changed value: EAGAIN");
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); ts.tv_nsec+=100000000; if(ts.tv_nsec>=1000000000){ ts.tv_sec++; ts.tv_nsec-=1000000000; } w[1].val=1;
    ok(sc(449,(long)w,2,0,(long)&ts,CLOCK_MONOTONIC,0)==-1&&errno==ETIMEDOUT,"futex_waitv times out");
    ok(sc(279,0,0x4000000000000001L,0,0,(long)&g_fw[0],0)==-1&&errno==EINVAL,"move_pages with a huge count: EINVAL");
    unsigned char* m=mmap(0,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); m[0]=1; unsigned char v[2];
    ok(mincore(m,8192,v)==0&&(v[0]&1),"mincore: a touched page is resident");
    munmap(m,8192); ok(mincore(m,4096,v)==-1&&errno==ENOMEM,"mincore on unmapped memory: ENOMEM");
    static volatile int shared=11; int p[2]; pipe(p);
    pid_t c=fork(); if(c==0){ shared=77; write(p[1],"g",1); ms(1500); _exit(0); }
    char z; read(p[0],&z,1); int got=0; struct iovec li={&got,4}, ri={(void*)&shared,4};
    ok(process_vm_readv(c,&li,1,&ri,1,0)==4&&got==77,"process_vm_readv reads the child's copy");
    int nv=99; struct iovec lw={&nv,4}; ok(process_vm_writev(c,&lw,1,&ri,1,0)==4,"process_vm_writev");
    kill(c,SIGKILL); waitpid(c,0,0); close(p[0]); close(p[1]);
    int f=open("/tmp/abi3m",O_CREAT|O_RDWR,0644); close(f);
    ok(sc(452,AT_FDCWD,(long)"/tmp/abi3m",0600,0,0,0)==0,"fchmodat2"); struct stat st; stat("/tmp/abi3m",&st); ok((st.st_mode&0777)==0600,"mode changed"); unlink("/tmp/abi3m");
    int sz=klogctl(10,0,0); char* kb=malloc(sz>0?sz:1); int kn=klogctl(3,kb,sz); ok(sz>0&&kn>0&&strchr(kb,'['),"klogctl reads the kernel log (dmesg)"); free(kb);
    struct timex tx; memset(&tx,0,sizeof tx); int ar=adjtimex(&tx); ok(ar==TIME_ERROR&&tx.time.tv_sec>1600000000,"adjtimex reports the clock as not synchronised");
}
struct sf { unsigned short code; unsigned char jt, jf; unsigned k; };
struct sfp { unsigned short len; struct sf* f; };
static volatile int g_sys;
static void onsys(int s,siginfo_t* i,void* u){ (void)u; if(s==SIGSYS&&i->si_code==1&&i->si_syscall==SYS_getppid)g_sys=1; }
static void t_seccomp(void){
    pid_t c=fork();
    if(c==0){
        struct sf f[]={ {0x20,0,0,4},{0x15,1,0,0xC000003E},{0x06,0,0,0x80000000u},{0x20,0,0,0},
            {0x15,0,1,SYS_getpid},{0x06,0,0,0x50000|1},{0x15,0,1,SYS_getppid},{0x06,0,0,0x30000},{0x15,0,1,SYS_uname},{0x06,0,0,0x80000000u},{0x06,0,0,0x7fff0000u} };
        struct sfp prog={sizeof f/sizeof f[0],f};
        if(getuid()!=0&&(syscall(SYS_seccomp,1,0,&prog)!=-1||errno!=EACCES))_exit(20);
        if(prctl(PR_SET_NO_NEW_PRIVS,1,0,0,0)||prctl(PR_GET_NO_NEW_PRIVS,0,0,0,0)!=1)_exit(21);
        struct sigaction sa; memset(&sa,0,sizeof sa); sa.sa_sigaction=onsys; sa.sa_flags=SA_SIGINFO; sigaction(SIGSYS,&sa,0);
        if(syscall(SYS_seccomp,1,0,&prog))_exit(22);
        if(prctl(PR_GET_SECCOMP,0,0,0,0)!=2)_exit(23);
        if(syscall(SYS_getpid)!=-1||errno!=EPERM)_exit(24);
        syscall(SYS_getppid); if(!g_sys)_exit(25);
        if(fork()==0)_exit(syscall(SYS_getpid)==-1?0:26);
        int st; wait(&st); if(!WIFEXITED(st)||WEXITSTATUS(st))_exit(27);
        char* un=malloc(400); syscall(SYS_uname,un); _exit(28);
    }
    int st=0; waitpid(c,&st,0);
    ok(WIFSIGNALED(st)&&WTERMSIG(st)==SIGSYS,"seccomp filter: EPERM for getpid, SIGSYS trap for getppid, inherited by fork, uname kills the process with SIGSYS");
    if(!WIFSIGNALED(st))printf("abi3: seccomp child status %d\n",WIFEXITED(st)?WEXITSTATUS(st):-1);
    c=fork(); if(c==0){ prctl(PR_SET_SECCOMP,1,0,0,0); write(1,"",0); syscall(SYS_getpid); _exit(5); }
    waitpid(c,&st,0); ok(WIFSIGNALED(st)&&WTERMSIG(st)==SIGKILL,"strict seccomp: only read, write and exit");
    unsigned act=0x50000; ok(syscall(SYS_seccomp,2,0,&act)==0,"SECCOMP_GET_ACTION_AVAIL");
}
static void t_execveat(void){
    int f=open("/tmp/abi3x.sh",O_CREAT|O_WRONLY|O_TRUNC,0755); write(f,"#!/bin/sh\nexit 42\n",18); close(f);
    for(int mode=0;mode<2;mode++){
        pid_t c=fork();
        if(c==0){ char* av[]={"abi3x.sh",0}; char* ev[]={0}; if(mode==0){ int fd=open("/tmp/abi3x.sh",O_RDONLY); syscall(SYS_execveat,fd,"",av,ev,AT_EMPTY_PATH); } else { int d=open("/tmp",O_RDONLY|O_DIRECTORY); syscall(SYS_execveat,d,"abi3x.sh",av,ev,0); } _exit(100+errno); }
        int st; waitpid(c,&st,0); ok(WIFEXITED(st)&&WEXITSTATUS(st)==42,mode?"execveat with a directory descriptor and a relative path":"execveat with AT_EMPTY_PATH runs the file behind a descriptor");
        if(!WIFEXITED(st)||WEXITSTATUS(st)!=42)printf("abi3: execveat status %d\n",WIFEXITED(st)?WEXITSTATUS(st):-1);
    }
    unlink("/tmp/abi3x.sh");
}
int main(int argc,char** argv){
    (void)argc; (void)argv;
    setvbuf(stdout,0,_IOLBF,0);
    t_sem(); t_msg(); t_mq(); t_pidfd(); t_splice(); t_openat2(); t_fifo(); t_misc(); t_seccomp();
    t_execveat();
    printf("abi3: done %d passed, %d failed\n",passed,failed);
    return failed?1:0;
}
