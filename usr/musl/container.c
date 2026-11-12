#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sched.h>
#include <dirent.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define BASE "/var/lib/containers"

static void die(const char* m){ fprintf(stderr,"container: %s\n",m); exit(1); }
static void diee(const char* m){ fprintf(stderr,"container: %s: %s\n",m,strerror(errno)); exit(1); }
static int exists(const char* p){ struct stat st; return stat(p,&st)==0; }
static int isdir(const char* p){ struct stat st; return stat(p,&st)==0&&S_ISDIR(st.st_mode); }
static void mkdirs(const char* p){ char b[512]; snprintf(b,sizeof b,"%s",p); for(char* q=b+1;*q;q++) if(*q=='/'){ *q=0; mkdir(b,0755); *q='/'; } mkdir(b,0755); }
static int wfile(const char* p,const char* s){ int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd<0)return -1; int n=(int)strlen(s); int r=write(fd,s,n)==n?0:-1; close(fd); return r; }
static int wsys(const char* p,const char* s){ int fd=open(p,O_WRONLY); if(fd<0)return -1; int n=(int)strlen(s); int r=write(fd,s,n)==n?0:-1; close(fd); return r; }
static int rfile(const char* p,char* b,int cap){ int fd=open(p,O_RDONLY); if(fd<0)return -1; int n=read(fd,b,cap-1); close(fd); if(n<0)return -1; b[n]=0; return n; }
static int okname(const char* n){ if(!n[0]||strlen(n)>24)return 0; for(const char* q=n;*q;q++) if(!((*q>='a'&&*q<='z')||(*q>='0'&&*q<='9')||*q=='-'||*q=='_'))return 0; return 1; }
static void path(char* out,int cap,const char* name,const char* sub){ snprintf(out,cap,BASE "/%s%s%s",name,sub[0]?"/":"",sub); }
static int runpid(const char* name){ char p[256], b[32]; path(p,sizeof p,name,"pid"); if(rfile(p,b,32)<=0)return 0; int pid=atoi(b); if(pid<=0||kill(pid,0))return 0; return pid; }
static void rmtree(const char* p){
    struct stat st; if(lstat(p,&st))return;
    if(S_ISDIR(st.st_mode)){ DIR* d=opendir(p); if(d){ struct dirent* e; char c[1024]; while((e=readdir(d))){ if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue; snprintf(c,sizeof c,"%s/%s",p,e->d_name); rmtree(c); } closedir(d); } rmdir(p); }
    else unlink(p); }
static void usage(void){
    printf("usage:\n"
           "  container create NAME [ROOTFS-DIR]   make a container (no dir: uses the system's programs read-only)\n"
           "  container run [options] NAME [COMMAND [ARGS...]]\n"
           "      -m SIZE    memory limit (for example 256M)\n"
           "      -p N       process limit\n"
           "      -c PCT     CPU limit in percent of one core\n"
           "      -h NAME    host name inside (default: the container name)\n"
           "      -n         no network (own network namespace with only loopback)\n"
           "      -v HOST:DIR  share a host directory\n"
           "  container exec NAME COMMAND [ARGS...]  run a command in a running container\n"
           "  container ls                           list containers\n"
           "  container stop NAME                    stop a running container\n"
           "  container rm NAME                      delete a stopped container and its changes\n"); }
static int c_create(int argc,char** argv){
    if(argc<1)die("create needs a name");
    const char* n=argv[0]; if(!okname(n))die("names use a-z, 0-9, - and _ (up to 24 characters)");
    char d[256]; path(d,sizeof d,n,""); if(exists(d))die("a container with that name exists");
    const char* lower=argc>1?argv[1]:0; char lp[512];
    if(lower){ if(lower[0]!='/'){ char cwd[256]; if(!getcwd(cwd,sizeof cwd))diee("getcwd"); snprintf(lp,sizeof lp,"%s/%s",cwd,lower); } else snprintf(lp,sizeof lp,"%s",lower); if(!isdir(lp))die("the root file system directory does not exist"); }
    mkdirs(d); char p[512];
    const char* subs[]={"root","upper","work","base",0}; for(int i=0;subs[i];i++){ path(p,sizeof p,n,subs[i]); mkdir(p,0755); }
    if(lower){ path(p,sizeof p,n,"lower"); wfile(p,lp); }
    else { const char* bd[]={"bin","sbin","usr","lib","lib64","opt","etc","proc","sys","dev","tmp","root","home","var","run","mnt",0}; for(int i=0;bd[i];i++){ char b[512]; snprintf(b,sizeof b,BASE "/%s/base/%s",n,bd[i]); mkdir(b,0755); }
        char b[512]; snprintf(b,sizeof b,BASE "/%s/base/etc/hostname",n); char hn[64]; snprintf(hn,sizeof hn,"%s\n",n); wfile(b,hn);
        snprintf(b,sizeof b,BASE "/%s/base/etc/passwd",n); wfile(b,"root:x:0:0:root:/root:/bin/sh\nnobody:x:65534:65534:nobody:/:/bin/false\n");
        snprintf(b,sizeof b,BASE "/%s/base/etc/group",n); wfile(b,"root:x:0:\nnogroup:x:65534:\n");
        char rc[512]; if(rfile("/etc/resolv.conf",rc,sizeof rc)>0){ snprintf(b,sizeof b,BASE "/%s/base/etc/resolv.conf",n); wfile(b,rc); } }
    printf("container %s created%s%s\n",n,lower?" from ":" (uses the system's programs)",lower?lp:"");
    return 0; }
static void initloop(char** cmd){
    pid_t c=fork(); if(c<0)_exit(127);
    if(c==0){ setsid(); execvp(cmd[0],cmd); fprintf(stderr,"container: cannot run %s: %s\n",cmd[0],strerror(errno)); _exit(127); }
    for(;;){ int st; pid_t w=wait(&st); if(w==c)_exit(WIFEXITED(st)?WEXITSTATUS(st):128+WTERMSIG(st)); if(w<0&&errno==ECHILD)_exit(0); } }
static int c_run(int argc,char** argv){
    const char* mem=0; const char* pids=0; const char* cpu=0; const char* host=0; int nonet=0; const char* vols[8]; int nv=0; int i=0;
    for(;i<argc&&argv[i][0]=='-';i++){ const char* o=argv[i];
        if(!strcmp(o,"-n"))nonet=1;
        else if(i+1<argc&&!strcmp(o,"-m"))mem=argv[++i]; else if(i+1<argc&&!strcmp(o,"-p"))pids=argv[++i]; else if(i+1<argc&&!strcmp(o,"-c"))cpu=argv[++i];
        else if(i+1<argc&&!strcmp(o,"-h"))host=argv[++i]; else if(i+1<argc&&!strcmp(o,"-v")){ if(nv<8)vols[nv++]=argv[++i]; else i++; }
        else { usage(); return 1; } }
    if(i>=argc)die("run needs a name"); const char* n=argv[i++]; if(!okname(n))die("bad name");
    char d[256]; path(d,sizeof d,n,""); if(!isdir(d))die("no such container (use: container create NAME)");
    if(runpid(n))die("the container is already running (use: container exec)");
    char* defcmd[]={"/bin/sh",0}; char** cmd=i<argc?argv+i:defcmd;
    if(getuid()!=0)die("run needs root");
    char cg[256]; snprintf(cg,sizeof cg,"/sys/fs/cgroup/%s",n); mkdir(cg,0755); char f[320];
    if(pids){ snprintf(f,sizeof f,"%s/pids.max",cg); if(wsys(f,pids))diee("pids.max"); }
    if(mem){ snprintf(f,sizeof f,"%s/memory.max",cg); if(wsys(f,mem))diee("memory.max"); }
    if(cpu){ int pc=atoi(cpu); if(pc<1||pc>800)die("-c takes 1 to 800"); char v[64]; snprintf(v,sizeof v,"%d 100000",pc*1000); snprintf(f,sizeof f,"%s/cpu.max",cg); if(wsys(f,v))diee("cpu.max"); }
    char me[24]; snprintf(me,sizeof me,"%d",getpid()); snprintf(f,sizeof f,"%s/cgroup.procs",cg); if(wsys(f,me))diee("joining the cgroup");
    int fl=CLONE_NEWNS|CLONE_NEWUTS|CLONE_NEWIPC|CLONE_NEWPID|(nonet?CLONE_NEWNET:0);
    if(unshare(fl))diee("unshare");
    char root[256], up[256], wk[256], lowf[256], lower[512]; path(root,sizeof root,n,"root"); path(up,sizeof up,n,"upper"); path(wk,sizeof wk,n,"work"); path(lowf,sizeof lowf,n,"lower");
    int own=rfile(lowf,lower,sizeof lower)>0; if(!own)path(lower,sizeof lower,n,"base"); { int L=(int)strlen(lower); while(L>0&&(lower[L-1]=='\n'||lower[L-1]==' '))lower[--L]=0; }
    pid_t c=fork(); if(c<0)diee("fork");
    if(c==0){
        char opt[1200]; snprintf(opt,sizeof opt,"lowerdir=%s,upperdir=%s,workdir=%s",lower,up,wk);
        if(mount("overlay",root,"overlay",0,opt))diee("overlay mount");
        char p[512];
        if(!own){ const char* sys[]={"/bin","/sbin","/usr","/lib","/lib64","/opt",0}; for(int k=0;sys[k];k++){ if(!isdir(sys[k]))continue; snprintf(p,sizeof p,"%s%s",root,sys[k]); mkdir(p,0755); if(mount(sys[k],p,0,MS_BIND,0)==0)mount(0,p,0,MS_BIND|MS_REMOUNT|MS_RDONLY,0); } }
        for(int k=0;k<nv;k++){ char hv[256]; snprintf(hv,sizeof hv,"%s",vols[k]); char* col=strchr(hv,':'); if(!col||col[1]!='/'){ fprintf(stderr,"container: -v takes HOST:/DIR\n"); _exit(1); } *col=0; snprintf(p,sizeof p,"%s%s",root,col+1); mkdirs(p); if(mount(hv,p,0,MS_BIND,0)){ fprintf(stderr,"container: cannot share %s: %s\n",hv,strerror(errno)); _exit(1); } }
        const char* sp[][2]={{"proc","/proc"},{"sysfs","/sys"},{"devtmpfs","/dev"},{"tmpfs","/tmp"},{"tmpfs","/run"},{0,0}};
        for(int k=0;sp[k][0];k++){ snprintf(p,sizeof p,"%s%s",root,sp[k][1]); mkdir(p,0755); if(mount(sp[k][0],p,sp[k][0],0,0)){ fprintf(stderr,"container: mount %s: %s\n",sp[k][1],strerror(errno)); } }
        const char* hn=host?host:n; sethostname(hn,strlen(hn));
        if(chroot(root))diee("chroot"); if(chdir("/"))diee("chdir");
        clearenv(); setenv("PATH","/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",1); setenv("HOME","/root",1); setenv("TERM","xterm",1); setenv("HOSTNAME",hn,1); setenv("container","noovexos",1);
        if(isdir("/root"))chdir("/root");
        initloop(cmd); }
    char pf[256]; path(pf,sizeof pf,n,"pid"); char pb[24]; snprintf(pb,sizeof pb,"%d\n",c); wfile(pf,pb);
    signal(SIGINT,SIG_IGN); signal(SIGQUIT,SIG_IGN);
    int st=0; while(waitpid(c,&st,0)<0&&errno==EINTR){}
    unlink(pf); wsys("/sys/fs/cgroup/cgroup.procs",me);
    return WIFEXITED(st)?WEXITSTATUS(st):128+WTERMSIG(st); }
static int c_exec(int argc,char** argv){
    if(argc<2)die("exec needs a name and a command"); const char* n=argv[0]; int pid=runpid(n); if(!pid)die("the container is not running");
    if(getuid()!=0)die("exec needs root");
    char cg[256]; snprintf(cg,sizeof cg,"/sys/fs/cgroup/%s/cgroup.procs",n); char me[24]; snprintf(me,sizeof me,"%d",getpid()); wsys(cg,me);
    const char* ns[]={"uts","ipc","net","pid","mnt",0}; int fds[5];
    for(int i=0;ns[i];i++){ char p[64]; snprintf(p,sizeof p,"/proc/%d/ns/%s",pid,ns[i]); fds[i]=open(p,O_RDONLY); if(fds[i]<0)diee(p); }
    for(int i=0;ns[i];i++){ if(setns(fds[i],0))diee("setns"); close(fds[i]); }
    pid_t c=fork(); if(c<0)diee("fork");
    if(c==0){ clearenv(); setenv("PATH","/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin",1); setenv("HOME","/root",1); setenv("TERM","xterm",1); if(isdir("/root"))chdir("/root"); else chdir("/");
        execvp(argv[1],argv+1); fprintf(stderr,"container: cannot run %s: %s\n",argv[1],strerror(errno)); _exit(127); }
    int st=0; while(waitpid(c,&st,0)<0&&errno==EINTR){}
    return WIFEXITED(st)?WEXITSTATUS(st):128+WTERMSIG(st); }
static int c_ls(void){
    DIR* d=opendir(BASE); if(!d){ printf("no containers\n"); return 0; }
    struct dirent* e; int any=0; printf("%-24s %-9s %-7s %s\n","NAME","STATE","PID","ROOT");
    while((e=readdir(d))){ if(e->d_name[0]=='.')continue; char p[256]; path(p,sizeof p,e->d_name,"root"); if(!isdir(p))continue; any=1; int pid=runpid(e->d_name);
        char lf[256], lo[512]; path(lf,sizeof lf,e->d_name,"lower"); if(rfile(lf,lo,sizeof lo)<=0)strcpy(lo,"(system programs)"); else { int L=(int)strlen(lo); while(L>0&&lo[L-1]=='\n')lo[--L]=0; }
        char ps[16]; if(pid)snprintf(ps,sizeof ps,"%d",pid); else strcpy(ps,"-");
        printf("%-24s %-9s %-7s %s\n",e->d_name,pid?"running":"stopped",ps,lo); }
    closedir(d); if(!any)printf("no containers\n"); return 0; }
static int c_stop(int argc,char** argv){ if(argc<1)die("stop needs a name"); int pid=runpid(argv[0]); if(!pid)die("the container is not running"); kill(pid,SIGKILL); for(int i=0;i<50&&!kill(pid,0);i++)usleep(100000); printf("container %s stopped\n",argv[0]); return 0; }
static int c_rm(int argc,char** argv){ if(argc<1)die("rm needs a name"); const char* n=argv[0]; if(!okname(n))die("bad name"); char d[256]; path(d,sizeof d,n,""); if(!isdir(d))die("no such container"); if(runpid(n))die("stop the container first");
    rmtree(d); char cg[256]; snprintf(cg,sizeof cg,"/sys/fs/cgroup/%s",n); rmdir(cg); printf("container %s removed\n",n); return 0; }
int main(int argc,char** argv){
    if(argc<2||!strcmp(argv[1],"help")||!strcmp(argv[1],"--help")){ usage(); return argc<2; }
    const char* c=argv[1]; mkdirs(BASE);
    if(!strcmp(c,"create"))return c_create(argc-2,argv+2);
    if(!strcmp(c,"run"))return c_run(argc-2,argv+2);
    if(!strcmp(c,"exec"))return c_exec(argc-2,argv+2);
    if(!strcmp(c,"ls")||!strcmp(c,"ps")||!strcmp(c,"list"))return c_ls();
    if(!strcmp(c,"stop"))return c_stop(argc-2,argv+2);
    if(!strcmp(c,"rm"))return c_rm(argc-2,argv+2);
    usage(); return 1; }
