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
#include <sched.h>
#include <dirent.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/syscall.h>

static int passed, failed;
static void ok(int c,const char* what){ if(c)passed++; else failed++; printf("cont: %s %s\n",c?"ok":"FAIL",what); fflush(stdout); }
static void ms(int m){ struct timespec t={m/1000,(m%1000)*1000000L}; nanosleep(&t,0); }
static int wfile(const char* p,const char* s){ int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0)return -1; int n=(int)strlen(s); int r=write(fd,s,n)==n?0:-1; close(fd); return r; }
static int rfile(const char* p,char* b,int cap){ int fd=open(p,O_RDONLY); if(fd<0)return -1; int n=read(fd,b,cap-1); close(fd); if(n<0)return -1; b[n]=0; return n; }
static int wsys(const char* p,const char* s){ int fd=open(p,O_WRONLY); if(fd<0)return -errno; int n=(int)strlen(s); int r=write(fd,s,n); int e=errno; close(fd); return r==n?0:-e; }
static int exists(const char* p){ struct stat st; return lstat(p,&st)==0; }
static int indir(const char* d,const char* name){ DIR* x=opendir(d); if(!x)return -1; struct dirent* e; int f=0; while((e=readdir(x))) if(!strcmp(e->d_name,name))f=1; closedir(x); return f; }
static int countdir(const char* d){ DIR* x=opendir(d); if(!x)return -1; struct dirent* e; int n=0; while((e=readdir(x))) if(e->d_name[0]!='.')n++; closedir(x); return n; }
static int copyfile(const char* a,const char* b){ int i=open(a,O_RDONLY); if(i<0)return -1; int o=open(b,O_WRONLY|O_CREAT|O_TRUNC,0755); if(o<0){ close(i); return -1; } char buf[65536]; int n, r=0; while((n=read(i,buf,sizeof buf))>0) if(write(o,buf,n)!=n){ r=-1; break; } close(i); close(o); return r; }
static void runc(void (*fn)(void)){
    int p[2]; if(pipe(p)){ ok(0,"pipe for a test child"); return; }
    pid_t c=fork(); if(c<0){ ok(0,"fork for a test child"); return; }
    if(c==0){ close(p[0]); alarm(120); passed=failed=0; fn(); int v[2]={passed,failed}; write(p[1],v,sizeof v); _exit(0); }
    close(p[1]); int v[2]={0,0}; int n=read(p[0],v,sizeof v); close(p[0]); int st=0; waitpid(c,&st,0);
    if(n==(int)sizeof v){ passed+=v[0]; failed+=v[1]; } else ok(0,"test child ended early");
}
static void mkroot(void){
    mkdir("/tmp/ct",0755); mkdir("/tmp/ct/root",0755);
    const char* d[]={"bin","proc","tmp","etc","mnt","old","sys","dev",0}; char p[128];
    for(int i=0;d[i];i++){ snprintf(p,sizeof p,"/tmp/ct/root/%s",d[i]); mkdir(p,0755); }
    wfile("/tmp/ct/root/etc/hello","hi\n");
    if(copyfile("/proc/self/exe","/tmp/ct/root/bin/contest")||!exists("/tmp/ct/root/bin/contest"))copyfile("/usr/libexec/noovex/contest","/tmp/ct/root/bin/contest");
}

static void t_uts(void){
    struct utsname u0; uname(&u0); char host0[65]; strcpy(host0,u0.nodename);
    int p[2]; pipe(p); pid_t c=fork();
    if(c==0){ close(p[0]); int r=unshare(CLONE_NEWUTS); char ok1=r==0; sethostname("ctest",5); struct utsname u; uname(&u); char ok2=!strcmp(u.nodename,"ctest"); char b[2]={ok1,ok2}; write(p[1],b,2); ms(800); _exit(0); }
    close(p[1]); char b[2]={0,0}; read(p[0],b,2); close(p[0]);
    ok(b[0],"unshare(CLONE_NEWUTS)"); ok(b[1],"sethostname inside the new UTS namespace");
    char nsp[64]; snprintf(nsp,sizeof nsp,"/proc/%d/ns/uts",c); int nfd=open(nsp,O_RDONLY); ok(nfd>=0,"open /proc/<pid>/ns/uts");
    char l1[64]={0}, l2[64]={0}; readlink("/proc/self/ns/uts",l1,63); readlink(nsp,l2,63); ok(!strncmp(l1,"uts:[",5)&&strcmp(l1,l2)!=0,"ns links differ between namespaces");
    struct utsname u1; uname(&u1); ok(!strcmp(u1.nodename,host0),"the host name outside is unchanged");
    pid_t c2=fork(); if(c2==0){ int r=setns(nfd,CLONE_NEWUTS); struct utsname u; uname(&u); _exit(r==0&&!strcmp(u.nodename,"ctest")?3:1); }
    int st=0; waitpid(c2,&st,0); ok(WIFEXITED(st)&&WEXITSTATUS(st)==3,"setns joins the other UTS namespace");
    close(nfd); waitpid(c,&st,0);
}
static void t_pidns_child(void){
    ok(getpid()==1,"first process in a new PID namespace has pid 1");
    ok(getppid()==0,"its parent is outside: getppid is 0");
    int p[2]; pipe(p); pid_t g=fork();
    if(g==0){ close(p[0]); char b[2]={(char)getpid(),(char)getppid()}; write(p[1],b,2); pause(); _exit(0); }
    close(p[1]); char b[2]={0,0}; read(p[0],b,2); close(p[0]);
    ok(g==2&&b[0]==2&&b[1]==1,"second process gets pid 2, parent pid 1");
    ok(kill(g,0)==0,"kill(2,0) finds the process by its namespace pid");
    kill(g,SIGTERM); int st=0; pid_t w=waitpid(g,&st,0); ok(w==2&&WIFSIGNALED(st),"waitpid returns the namespace pid");
    ok(kill(99999,0)==-1&&errno==ESRCH,"pids outside the namespace are not visible");
}
static void t_pidns(void){
    ok(unshare(CLONE_NEWPID)==0,"unshare(CLONE_NEWPID)");
    ok(getpid()!=1,"the caller keeps its pid; only children move");
    int p[2]; pipe(p); pid_t c=fork();
    if(c==0){ close(p[0]); passed=failed=0; t_pidns_child(); int v[2]={passed,failed}; write(p[1],v,sizeof v); _exit(0); }
    close(p[1]); int v[2]={0,0}; read(p[0],v,sizeof v); close(p[0]); passed+=v[0]; failed+=v[1];
    ok(c>1,"the parent sees the child's real pid");
    int st=0; waitpid(c,&st,0);
    ok(fork()==-1&&errno==ENOMEM,"after pid 1 exits, fork into the namespace fails with ENOMEM");
}
static void t_pidinit(void){
    unshare(CLONE_NEWPID); int st=0;
    int q[2]; pipe(q); pid_t c2=fork();
    if(c2==0){ close(q[0]); pid_t g=fork(); if(g==0){ for(;;)pause(); } close(q[1]); ms(300); _exit(0); }
    close(q[1]); waitpid(c2,&st,0); char x; int t0=time(0); int n=read(q[0],&x,1); close(q[0]);
    ok(n==0&&time(0)-t0<5,"when pid 1 exits the other processes in the namespace are killed");
}
static void t_chroot(void){
    ok(unshare(CLONE_NEWNS)==0,"unshare(CLONE_NEWNS)");
    ok(chroot("/tmp/ct/root")==0,"chroot into the test root");
    ok(chdir("/")==0,"chdir / after chroot");
    char b[64]; ok(rfile("/etc/hello",b,64)==3&&!strcmp(b,"hi\n"),"files are read from inside the new root");
    char cwd[128]; ok(getcwd(cwd,sizeof cwd)&&!strcmp(cwd,"/"),"getcwd shows /");
    chdir("../../.."); ok(getcwd(cwd,sizeof cwd)&&!strcmp(cwd,"/"),".. cannot leave the root");
    ok(!exists("/tmp/ct"),"paths of the host are not visible");
    symlink("/etc/hello","/tmp/abs"); ok(rfile("/tmp/abs",b,64)==3,"absolute symlinks resolve inside the root");
    mkdir("/tmp/sub",0755); chdir("/tmp/sub"); ok(getcwd(cwd,sizeof cwd)&&!strcmp(cwd,"/tmp/sub"),"getcwd inside a subdirectory");
    ok(rfile("../../etc/hello",b,64)==3,"relative paths with .. work");
    chdir("/");
    ok(mount("tmpfs","/tmp","tmpfs",0,0)==0,"mount tmpfs on /tmp");
    ok(!exists("/tmp/abs"),"the tmpfs hides the files below it");
    wfile("/tmp/t1","x"); ok(exists("/tmp/t1"),"files can be created on the tmpfs");
    ok(umount("/tmp")==0&&!exists("/tmp/t1")&&exists("/tmp/abs"),"umount shows the old files again");
    ok(mount("/etc","/mnt",0,MS_BIND,0)==0,"bind mount /etc on /mnt");
    ok(rfile("/mnt/hello",b,64)==3,"the bind mount shows the same files");
    wfile("/mnt/new","y"); ok(exists("/etc/new"),"writes through a bind mount reach the source");
    umount("/mnt"); unlink("/etc/new");
    ok(mount("proc","/proc","proc",0,0)==0,"mount proc");
    ok(rfile("/proc/self/status",b,64)>0,"/proc/self/status is readable");
    char mb[2048]; ok(rfile("/proc/mounts",mb,sizeof mb)>0&&strstr(mb,"proc /proc")!=0,"/proc/mounts lists the container's mounts");
    ok(mount("x","/mnt","nosuchfs",0,0)==-1&&errno==ENODEV,"unknown file system types: ENODEV");
    char* av[]={"/bin/contest","--inner",0}; pid_t c=fork(); if(c==0){ execv("/bin/contest",av); _exit(99); }
    int st=0; waitpid(c,&st,0); ok(WIFEXITED(st)&&WEXITSTATUS(st)==42,"exec runs a program from inside the root");
    ok(execv("/bin/nonexistent",av)==-1&&errno==ENOENT,"exec does not find host programs");
}
static void t_procns(void){
    ok(unshare(CLONE_NEWNS|CLONE_NEWPID)==0,"unshare mount and pid namespaces together");
    pid_t c=fork();
    if(c==0){ passed=failed=0; chroot("/tmp/ct/root"); chdir("/"); int r=mount("proc","/proc","proc",0,0);
        int n=countdir("/proc"); int has1=indir("/proc","1"); char b[256]; int s=rfile("/proc/1/status",b,256);
        _exit((r==0)|((has1==1)<<1)|((s>0)<<2)|((n>=1&&n<=40)<<3)); }
    int st=0; waitpid(c,&st,0); int v=WIFEXITED(st)?WEXITSTATUS(st):0;
    ok(v&1,"proc mounts inside a new pid namespace"); ok(v&2,"/proc lists pid 1"); ok(v&4,"/proc/1/status is the namespace's init"); ok(v&8,"host processes are not listed");
}
static void t_overlay(void){
    mkdir("/tmp/ct/lower",0755); mkdir("/tmp/ct/upper",0755); mkdir("/tmp/ct/work",0755); mkdir("/tmp/ct/merged",0755); mkdir("/tmp/ct/lower/d",0755);
    wfile("/tmp/ct/lower/a","lower"); wfile("/tmp/ct/lower/b","bbb"); wfile("/tmp/ct/lower/d/f","fff");
    ok(unshare(CLONE_NEWNS)==0,"unshare(CLONE_NEWNS) for overlay");
    ok(mount("overlay","/tmp/ct/merged","overlay",0,"lowerdir=/tmp/ct/lower,upperdir=/tmp/ct/upper,workdir=/tmp/ct/work")==0,"mount overlay");
    char b[64]; ok(rfile("/tmp/ct/merged/a",b,64)==5&&!strcmp(b,"lower"),"lower files are visible");
    ok(wfile("/tmp/ct/merged/a","upper!")==0,"writing a lower file");
    ok(rfile("/tmp/ct/merged/a",b,64)==6&&!strcmp(b,"upper!"),"the merged view shows the new data");
    ok(rfile("/tmp/ct/lower/a",b,64)==5&&!strcmp(b,"lower"),"the lower file is unchanged (copy-up)");
    ok(rfile("/tmp/ct/upper/a",b,64)==6,"the copy is in the upper directory");
    ok(unlink("/tmp/ct/merged/b")==0,"unlink a lower file");
    ok(!exists("/tmp/ct/merged/b")&&exists("/tmp/ct/lower/b"),"it is gone from the merged view, kept below");
    ok(exists("/tmp/ct/upper/.wh.b"),"a whiteout marks it in the upper directory");
    ok(wfile("/tmp/ct/merged/c","new")==0&&exists("/tmp/ct/upper/c")&&!exists("/tmp/ct/lower/c"),"new files go to the upper directory");
    ok(indir("/tmp/ct/merged","a")==1&&indir("/tmp/ct/merged","b")==0&&indir("/tmp/ct/merged","c")==1&&indir("/tmp/ct/merged","d")==1,"readdir merges both layers without whiteouts");
    ok(countdir("/tmp/ct/merged")==3,"each name is listed once");
    ok(wfile("/tmp/ct/merged/d/g","g")==0&&exists("/tmp/ct/upper/d/g"),"files in lower directories are created in a copied-up directory");
    ok(rfile("/tmp/ct/merged/d/f",b,64)==3,"other files in that directory still come from below");
    ok(wfile("/tmp/ct/merged/b","again")==0&&rfile("/tmp/ct/merged/b",b,64)==5&&!strcmp(b,"again"),"a whited-out name can be created again");
    ok(rename("/tmp/ct/merged/d/f","/tmp/ct/merged/f2")==0&&exists("/tmp/ct/merged/f2")&&!exists("/tmp/ct/merged/d/f"),"rename of a lower file");
    ok(umount("/tmp/ct/merged")==0&&countdir("/tmp/ct/merged")==0,"umount the overlay");
}
static void t_pivot(void){
    ok(unshare(CLONE_NEWNS)==0,"unshare for pivot_root");
    ok(syscall(SYS_pivot_root,"/tmp/ct/root","/tmp/ct/root/old")==0,"pivot_root");
    chdir("/"); char b[64]; ok(rfile("/etc/hello",b,64)==3,"the new root is /");
    ok(exists("/old/tmp/ct/root/etc/hello"),"the old root is under /old");
    ok(umount2("/old",MNT_DETACH)==0&&!exists("/old/tmp"),"umount the old root");
}
static void t_cgroup(void){
    ok(mkdir("/sys/fs/cgroup/ct1",0755)==0,"mkdir /sys/fs/cgroup/ct1");
    ok(exists("/sys/fs/cgroup/ct1/pids.max")&&exists("/sys/fs/cgroup/ct1/cgroup.procs"),"control files appear");
    char b[256]; ok(rfile("/sys/fs/cgroup/cgroup.controllers",b,256)>0&&strstr(b,"pids")&&strstr(b,"memory")&&strstr(b,"cpu"),"cgroup.controllers lists cpu memory pids");
    ok(wsys("/sys/fs/cgroup/ct1/pids.max","4")==0,"write pids.max");
    char me[24]; snprintf(me,sizeof me,"%d",getpid()); ok(wsys("/sys/fs/cgroup/ct1/cgroup.procs",me)==0,"move self into the cgroup");
    ok(rfile("/proc/self/cgroup",b,256)>0&&!strcmp(b,"0::/ct1\n"),"/proc/self/cgroup shows /ct1");
    ok(rfile("/sys/fs/cgroup/ct1/cgroup.procs",b,256)>0&&atoi(b)==getpid(),"cgroup.procs lists the process");
    pid_t k[8]; int made=0; int eag=0; for(int i=0;i<8;i++){ k[i]=fork(); if(k[i]==0){ pause(); _exit(0); } if(k[i]<0){ eag=errno==EAGAIN; break; } made++; }
    ok(made==3&&eag,"pids.max=4 allows 3 more processes, then fork fails with EAGAIN");
    ok(rfile("/sys/fs/cgroup/ct1/pids.current",b,256)>0&&atoi(b)==4,"pids.current counts 4");
    for(int i=0;i<made;i++){ kill(k[i],SIGKILL); waitpid(k[i],0,0); }
    ok(wsys("/sys/fs/cgroup/ct1/memory.max","24M")==0,"write memory.max 24M");
    ok(rfile("/sys/fs/cgroup/ct1/memory.max",b,256)>0&&atol(b)==24L*1024*1024,"memory.max reads back");
    long got=0; for(int i=0;i<200;i++){ char* m=mmap(0,1<<20,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); if(m==MAP_FAILED)break; memset(m,1,1<<20); got++; }
    ok(got>=4&&got<60,"memory.max stops anonymous memory near the limit");
    ok(rfile("/sys/fs/cgroup/ct1/memory.current",b,256)>0&&atol(b)>4L*1024*1024,"memory.current reports the use");
    ok(wsys("/sys/fs/cgroup/ct1/memory.max","max")==0,"memory.max back to max");
    struct timespec a,z; long n1=0, n2=0; clock_gettime(CLOCK_MONOTONIC,&a); do{ for(volatile int i=0;i<20000;i++); n1++; clock_gettime(CLOCK_MONOTONIC,&z); }while((z.tv_sec-a.tv_sec)*1000+(z.tv_nsec-a.tv_nsec)/1000000<1500);
    ok(wsys("/sys/fs/cgroup/ct1/cpu.max","20000 100000")==0,"write cpu.max 20000 100000");
    clock_gettime(CLOCK_MONOTONIC,&a); do{ for(volatile int i=0;i<20000;i++); n2++; clock_gettime(CLOCK_MONOTONIC,&z); }while((z.tv_sec-a.tv_sec)*1000+(z.tv_nsec-a.tv_nsec)/1000000<1500);
    printf("cont: cpu work in 1.5 s: %ld without limit, %ld with cpu.max 20%%\n",n1,n2);
    ok(n2*10<n1*6,"cpu.max slows the process down");
    ok(rfile("/sys/fs/cgroup/ct1/cpu.stat",b,256)>0&&strstr(b,"nr_throttled")&&!strstr(b,"nr_throttled 0\n"),"cpu.stat counts throttling");
    wsys("/sys/fs/cgroup/ct1/cpu.max","max");
    ok(rmdir("/sys/fs/cgroup/ct1")==-1&&errno==EBUSY,"rmdir of a used cgroup: EBUSY");
    ok(wsys("/sys/fs/cgroup/cgroup.procs",me)==0,"move back to the root cgroup");
    ok(rmdir("/sys/fs/cgroup/ct1")==0&&!exists("/sys/fs/cgroup/ct1"),"rmdir of the empty cgroup");
}
static void t_user(void){
    setgid(1000); setuid(1000); ok(getuid()==1000,"running as uid 1000");
    ok(unshare(CLONE_NEWUTS)==-1&&errno==EPERM,"an ordinary user cannot unshare the UTS namespace");
    ok(unshare(CLONE_NEWUSER)==0,"an ordinary user can create a user namespace");
    ok(getuid()==65534,"before uid_map the user is nobody (65534)");
    ok(wsys("/proc/self/uid_map","0 1000 1")==0,"write uid_map 0 1000 1");
    ok(wsys("/proc/self/uid_map","0 1000 1")!=0,"uid_map can only be written once");
    ok(wsys("/proc/self/setgroups","deny")==0&&wsys("/proc/self/gid_map","0 1000 1")==0,"setgroups deny, then gid_map");
    ok(getuid()==0&&geteuid()==0&&getgid()==0,"inside the user namespace the user is root");
    char b[128]; ok(rfile("/proc/self/uid_map",b,128)>0&&strstr(b,"1000"),"uid_map reads back");
    ok(unshare(CLONE_NEWUTS)==0&&sethostname("userns",6)==0,"namespace root may create a UTS namespace");
    ok(unshare(CLONE_NEWNS)==0,"and a mount namespace");
    ok(setuid(0)==0,"setuid to a mapped id");
    ok(setuid(5)==-1&&errno==EINVAL,"setuid to an unmapped id: EINVAL");
}
static void t_net(void){
    ok(unshare(CLONE_NEWNET)==0,"unshare(CLONE_NEWNET)");
    int s=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a={0}; a.sin_family=AF_INET; a.sin_port=htons(80); a.sin_addr.s_addr=inet_addr("10.0.2.2");
    ok(connect(s,(struct sockaddr*)&a,sizeof a)==-1&&errno==ENETUNREACH,"outside addresses: ENETUNREACH"); close(s);
    struct if_nameindex* ni=if_nameindex(); int n=0, lo=0; for(int i=0;ni&&ni[i].if_index;i++){ n++; if(!strcmp(ni[i].if_name,"lo"))lo=1; } if(ni)if_freenameindex(ni);
    ok(n==1&&lo,"only the loopback interface is visible");
    int l=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in b={0}; b.sin_family=AF_INET; b.sin_addr.s_addr=htonl(INADDR_LOOPBACK); b.sin_port=htons(47123);
    int one=1; setsockopt(l,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    int r1=bind(l,(struct sockaddr*)&b,sizeof b); listen(l,2); int c=socket(AF_INET,SOCK_STREAM,0); int r2=connect(c,(struct sockaddr*)&b,sizeof b);
    ok(r1==0&&r2==0,"loopback still works"); close(c); close(l);
    a.sin_port=htons(47124); int u=socket(AF_INET,SOCK_DGRAM,0); ok(bind(u,(struct sockaddr*)&a,sizeof a)==-1&&errno==EADDRNOTAVAIL,"binding an outside address: EADDRNOTAVAIL"); close(u);
}
static void t_ipc(void){
    int id=semget(0x4E5658,1,IPC_CREAT|0600); ok(id>=0,"semget with a key in the host namespace");
    pid_t c=fork(); if(c==0){ unshare(CLONE_NEWIPC); int r=semget(0x4E5658,1,0); int e=errno; int id2=semget(0x4E5658,1,IPC_CREAT|0600); _exit((r==-1&&e==ENOENT)|((id2>=0&&id2!=id)<<1)); }
    int st=0; waitpid(c,&st,0); int v=WIFEXITED(st)?WEXITSTATUS(st):0;
    ok(v&1,"the key is not visible in a new IPC namespace"); ok(v&2,"the same key makes a separate set there");
    semctl(id,0,IPC_RMID);
}
static void t_clone(void){
    char* stk=malloc(65536); int fl=CLONE_NEWUTS|CLONE_NEWPID|CLONE_NEWNS|SIGCHLD;
    static int fnres; (void)fnres;
    int p[2]; pipe(p);
    pid_t c=syscall(SYS_clone,fl,0,0,0,0);
    if(c==0){ close(p[0]); char b[3]; b[0]=getpid()==1; sethostname("clone",5); struct utsname u; uname(&u); b[1]=!strcmp(u.nodename,"clone"); b[2]=1; write(p[1],b,3); _exit(0); }
    close(p[1]); char b[3]={0,0,0}; read(p[0],b,3); close(p[0]); int st=0; waitpid(c,&st,0);
    ok(c>0&&b[2],"clone with namespace flags"); ok(b[0],"clone(CLONE_NEWPID) child is pid 1"); ok(b[1],"clone(CLONE_NEWUTS) child has its own host name");
    struct utsname u; uname(&u); ok(strcmp(u.nodename,"clone")!=0,"the parent's host name is unchanged");
    free(stk);
}
int main(int argc,char** argv){
    if(argc>1&&!strcmp(argv[1],"--inner")){ char b[8]; return rfile("/etc/hello",b,8)==3?42:1; }
    setvbuf(stdout,0,_IONBF,0);
    if(getuid()!=0){ printf("contest: run as root\n"); return 1; }
    mkroot();
    runc(t_uts); runc(t_pidns); runc(t_pidinit); runc(t_chroot); runc(t_procns); runc(t_overlay); runc(t_pivot); runc(t_cgroup); runc(t_user); runc(t_net); runc(t_ipc); runc(t_clone);
    printf("contest: done %d passed, %d failed\n",passed,failed);
    return failed?1:0;
}
