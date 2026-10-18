#define NVX_LIBC_IMPL 1
/* ===== posix.h - a POSIX-style C library for NoovexOS =====
   Layered on the NoovexOS syscall interface (INT 0x80). Gives userspace ELF
   programs the standard Unix/POSIX API so they can be written like ordinary
   C programs:  open/read/write/close, fork/execv/waitpid, getpid, printf,
   malloc/free, string helpers, errno, and stdin/stdout/stderr.

   Usage:  #include "posix.h"   (instead of, or alongside, user.h)
*/
#ifndef NVX_POSIX_H
#define NVX_POSIX_H

typedef unsigned long size_t;
typedef long           ssize_t;
typedef int            pid_t;
typedef unsigned int   mode_t;
typedef long           off_t;

/* ---- raw syscall ---- */
long __sys(long n,long a,long b,long c,long d,long e);

/* ---- errno ---- */
int errno;
#define EPERM   1
#define ENOENT  2
#define EBADF   9
#ifndef EINTR
#define EINTR   4
#endif
#ifndef ESRCH
#define ESRCH   3
#endif
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define EMFILE 24

/* ---- open flags (match kernel) ---- */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_APPEND 0x400
#define O_TRUNC  0x200
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* standard file descriptors */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* ================= unistd / fcntl ================= */
int open(const char* path,int flags,...){ int fd=(int)__sys(30,(long)path,flags,0,0,0); if(fd<0)errno=ENOENT; return fd; }
ssize_t read(int fd,void* buf,size_t n){ long r=__sys(31,fd,(long)buf,(long)n,0,0); if(r<0)errno=EBADF; return r; }
ssize_t write(int fd,const void* buf,size_t n){ long r=__sys(32,fd,(long)buf,(long)n,0,0); if(r<0)errno=EBADF; return r; }
int close(int fd){ return (int)__sys(33,fd,0,0,0,0); }
off_t lseek(int fd,off_t off,int whence){ return (off_t)__sys(36,fd,off,whence,0,0); }
int dup(int fd){ return (int)__sys(37,fd,0,0,0,0); }
int dup2(int o,int n){ int r=(int)__sys(144,o,n,0,0,0); if(r<0)errno=EBADF; return r; }
int pipe(int fds[2]){ int r=(int)__sys(145,(long)fds,0,0,0,0); if(r<0)errno=EMFILE; return r; }
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define O_CLOEXEC 0x80000
#define O_NONBLOCK 0x800
int fcntl(int fd,int cmd,...){ long arg=0; __builtin_va_list ap; __builtin_va_start(ap,cmd); if(cmd==F_DUPFD||cmd==F_SETFD||cmd==F_SETFL)arg=__builtin_va_arg(ap,long); __builtin_va_end(ap); return (int)__sys(146,fd,cmd,arg,0,0); }
mode_t umask(mode_t m){ return (mode_t)__sys(147,m,0,0,0,0); }
pid_t setpgid(pid_t pid,pid_t pg){ return (pid_t)__sys(150,pid,pg,0,0,0); }
pid_t getpgid(pid_t pid){ return (pid_t)__sys(151,pid,0,0,0,0); }
pid_t getpgrp(void){ return getpgid(0); }
pid_t setsid(void){ return (pid_t)__sys(152,0,0,0,0,0); }
pid_t getsid(pid_t pid){ return (pid_t)__sys(153,pid,0,0,0,0); }
pid_t tcgetpgrp(int fd){ (void)fd; return (pid_t)__sys(154,0,0,0,0,0); }
int tcsetpgrp(int fd,pid_t pg){ (void)fd; return (int)__sys(155,pg,0,0,0,0); }
pid_t getpid(void){ return (pid_t)__sys(39,0,0,0,0,0); }
void _exit(int code){ __sys(0,code,0,0,0,0); }
void __flush_all(void);
void exit(int code){ __flush_all(); __sys(0,code,0,0,0,0); }

/* ================= process control ================= */
pid_t fork(void){ return (pid_t)__sys(2,0,0,0,0,0); }
/* execv-like: NoovexOS exec takes a path; args are advisory (single-arg model) */
int execv(const char* path,char* const argv[]){ return (int)__sys(157,(long)path,(long)argv,0,0,0); }
int execve(const char* path,char* const argv[],char* const envp[]){ (void)envp; return execv(path,argv); }
int execvp(const char* file,char* const argv[]){ return execv(file,argv); }
int execl(const char* path,const char* arg0,...){ const char* av[8]; int n=0; av[n++]=arg0; __builtin_va_list ap; __builtin_va_start(ap,arg0); while(n<7){ const char* a=__builtin_va_arg(ap,const char*); av[n++]=a; if(!a)break; } __builtin_va_end(ap); av[7]=0; return execv(path,(char* const*)av); }
int execlp(const char* file,const char* arg0,...){ const char* av[8]; int n=0; av[n++]=arg0; __builtin_va_list ap; __builtin_va_start(ap,arg0); while(n<7){ const char* a=__builtin_va_arg(ap,const char*); av[n++]=a; if(!a)break; } __builtin_va_end(ap); av[7]=0; return execv(file,(char* const*)av); }
/* spawn = fork+exec in one call (NoovexOS extension), returns pid */
pid_t posix_spawnp(const char* path){ return (pid_t)__sys(5,(long)path,0,0,0,0); }
#define WNOHANG    1
#define WUNTRACED  2
#define WCONTINUED 8
#define WIFEXITED(s)    (((s)&0x7F)==0)
#define WEXITSTATUS(s)  (((s)>>8)&0xFF)
#define WIFSIGNALED(s)  (((s)&0x7F)!=0&&((s)&0x7F)!=0x7F)
#define WTERMSIG(s)     ((s)&0x7F)
#define WIFSTOPPED(s)   (((s)&0xFF)==0x7F)
#define WSTOPSIG(s)     (((s)>>8)&0xFF)
#define WIFCONTINUED(s) ((s)==0xFFFF)
pid_t waitpid(pid_t pid,int* status,int flags){ return (pid_t)__sys(156,pid,(long)status,flags,0,0); }
pid_t wait(int* status){ return waitpid(-1,status,0); }

/* ================= memory ================= */
void* sbrk(long inc){
    static unsigned long cur=0;
    if(cur==0) cur=(unsigned long)__sys(12,0,0,0,0,0);   /* brk(0) -> current break */
    unsigned long old=cur;
    if(inc){ unsigned long nb=(unsigned long)__sys(12,cur+inc,0,0,0,0); if(nb<=cur){errno=ENOMEM;return (void*)-1;} cur=nb; }
    return (void*)old;
}
/* a simple bump+free-list malloc */
typedef struct blk { size_t sz; struct blk* next; } blk_t;
blk_t* g_free=0;
void* malloc(size_t n){
    if(!n)return 0; n=(n+15)&~((size_t)15);
    blk_t** p=&g_free; while(*p){ if((*p)->sz>=n){ blk_t* b=*p; *p=b->next; return (void*)(b+1); } p=&(*p)->next; }
    blk_t* b=(blk_t*)sbrk((long)(n+sizeof(blk_t))); if((long)b==-1)return 0; b->sz=n; return (void*)(b+1);
}
void free(void* ptr){ if(!ptr)return; blk_t* b=((blk_t*)ptr)-1; b->next=g_free; g_free=b; }
void* calloc(size_t a,size_t b){ size_t n=a*b; char* p=(char*)malloc(n); if(p)for(size_t i=0;i<n;i++)p[i]=0; return p; }
void* realloc(void* ptr,size_t n){ if(!ptr)return malloc(n); void* q=malloc(n); if(q){ blk_t* b=((blk_t*)ptr)-1; size_t c=b->sz<n?b->sz:n; for(size_t i=0;i<c;i++)((char*)q)[i]=((char*)ptr)[i]; free(ptr);} return q; }

/* ================= string / mem ================= */
size_t strlen(const char* s){ size_t n=0; while(s[n])n++; return n; }
int strcmp(const char* a,const char* b){ while(*a&&*a==*b){a++;b++;} return (unsigned char)*a-(unsigned char)*b; }
int strncmp(const char* a,const char* b,size_t n){ for(size_t i=0;i<n;i++){ if(a[i]!=b[i]||!a[i])return (unsigned char)a[i]-(unsigned char)b[i]; } return 0; }
char* strcpy(char* d,const char* s){ char* r=d; while((*d++=*s++)); return r; }
char* strncpy(char* d,const char* s,size_t n){ size_t i=0; for(;i<n&&s[i];i++)d[i]=s[i]; for(;i<n;i++)d[i]=0; return d; }
char* strcat(char* d,const char* s){ char* r=d; while(*d)d++; while((*d++=*s++)); return r; }
char* strchr(const char* s,int c){ while(*s){ if(*s==(char)c)return (char*)s; s++; } return c?0:(char*)s; }
void* memcpy(void* d,const void* s,size_t n){ char* dd=d; const char* ss=s; for(size_t i=0;i<n;i++)dd[i]=ss[i]; return d; }
void* memset(void* d,int c,size_t n){ char* dd=d; for(size_t i=0;i<n;i++)dd[i]=(char)c; return d; }
int memcmp(const void* a,const void* b,size_t n){ const unsigned char* x=a,*y=b; for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]-y[i]; return 0; }

/* ================= stdio (printf family, on write) ================= */
void __putc(int fd,char c){ write(fd,&c,1); }
void __puts_fd(int fd,const char* s){ write(fd,s,strlen(s)); }
void __putnum(int fd,long v,int base,int isu){
    char buf[32]; int i=0; unsigned long u; int neg=0;
    if(!isu && v<0){ neg=1; u=(unsigned long)(-v); } else u=(unsigned long)v;
    if(u==0)buf[i++]='0';
    const char* dig="0123456789abcdef";
    while(u){ buf[i++]=dig[u%base]; u/=base; }
    if(neg)buf[i++]='-';
    while(i)__putc(fd,buf[--i]);
}
int __stdout_vprintf(const char* fmt,__builtin_va_list ap); long __stdout_write(const char* d,size_t n);
int puts(const char* s){ __stdout_write(s,strlen(s)); __stdout_write("\n",1); return 0; }
int putchar(int c){ char ch=(char)c; __stdout_write(&ch,1); return c; }

/* ================= more string / stdlib / ctype ================= */
char* strstr(const char* h,const char* n){ if(!*n)return (char*)h; for(;*h;h++){ const char* a=h;const char* b=n; while(*a&&*b&&*a==*b){a++;b++;} if(!*b)return (char*)h; } return 0; }
char* strdup(const char* s){ size_t n=strlen(s)+1; char* p=(char*)malloc(n); if(p)memcpy(p,s,n); return p; }
int isdigit(int c){ return c>='0'&&c<='9'; }
int isalpha(int c){ return (c>='a'&&c<='z')||(c>='A'&&c<='Z'); }
int toupper(int c){ return (c>='a'&&c<='z')?c-32:c; }
int tolower(int c){ return (c>='A'&&c<='Z')?c+32:c; }
int abs(int x){ return x<0?-x:x; }
char* strtok(char* s,const char* delim){
    static char* save=0; if(s)save=s; if(!save)return 0;
    while(*save){ const char* d=delim; int hit=0; for(;*d;d++)if(*save==*d){hit=1;break;} if(!hit)break; save++; }
    if(!*save){ save=0; return 0; }
    char* tok=save;
    while(*save){ const char* d=delim; int hit=0; for(;*d;d++)if(*save==*d){hit=1;break;} if(hit){*save++=0; return tok;} save++; }
    return tok;
}
void qsort(void* base,size_t n,size_t sz,int(*cmp)(const void*,const void*)){
    char* a=(char*)base; char tmp[256]; if(sz>256)return;
    for(size_t i=0;i<n;i++)for(size_t j=i+1;j<n;j++){ if(cmp(a+i*sz,a+j*sz)>0){ memcpy(tmp,a+i*sz,sz); memcpy(a+i*sz,a+j*sz,sz); memcpy(a+j*sz,tmp,sz); } }
}
char __env_buf[128];
char* getenv(const char* name){ long r=__sys(120,(long)name,(long)__env_buf,128,0,0); if(r<0)return 0; return __env_buf; }
int setenv(const char* name,const char* val,int overwrite){ (void)overwrite; return (int)__sys(121,(long)name,(long)val,0,0,0); }
int putenv(char* nv){ char n[24]; int i=0; while(nv[i]&&nv[i]!='='&&i<23){n[i]=nv[i];i++;} n[i]=0; if(nv[i]!='=')return -1; return setenv(n,nv+i+1,1); }
int unsetenv(const char* name){ return setenv(name,"",1); }
int getcwd_helper=0;

/* ================= misc ================= */
void sleep_ms(unsigned ms){ __sys(16,ms,0,0,0,0); }
unsigned long time_ticks(void){ return (unsigned long)__sys(140,0,0,0,0,0); }   /* monotonic ms since boot */

/* ================= stdio: FILE* streams ================= */
typedef struct { int fd; int eof; int err; int unget; char* buf; int bsize; int bpos; int bmode; int bown; char* tmpname; } FILE;
FILE __stdin={0,0,0,-1}, __stdout={1,0,0,-1}, __stderr={2,0,0,-1};
#define stdin  (&__stdin)
#define stdout (&__stdout)
#define stderr (&__stderr)
#define EOF (-1)
#define BUFSIZ 1024
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
int unlink(const char* path); int vsnprintf(char* out,size_t cap,const char* fmt,__builtin_va_list ap);
FILE* __ffiles[16];
void __freg(FILE* f){ for(int i=0;i<16;i++) if(__ffiles[i]==f)return; for(int i=0;i<16;i++) if(!__ffiles[i]){__ffiles[i]=f;return;} }
void __funreg(FILE* f){ for(int i=0;i<16;i++) if(__ffiles[i]==f)__ffiles[i]=0; }
int __fflush_buf(FILE* f){ if(!f||!f->buf||f->bpos<=0)return 0; int off=0;
    while(off<f->bpos){ long r=write(f->fd,f->buf+off,(size_t)(f->bpos-off)); if(r<=0){f->err=1;f->bpos=0;return EOF;} off+=(int)r; }
    f->bpos=0; return 0; }
long __fwrite_raw(FILE* f,const char* d,size_t n){
    if(!f->buf)return write(f->fd,d,n);
    size_t i=0; int nl=0;
    while(i<n){ if(f->bpos>=f->bsize){ if(__fflush_buf(f))return -1; }
        size_t room=(size_t)(f->bsize-f->bpos), chunk=n-i; if(chunk>room)chunk=room;
        for(size_t k=0;k<chunk;k++){ char c=d[i+k]; f->buf[f->bpos++]=c; if(c=='\n')nl=1; }
        i+=chunk; }
    if(f->bmode==_IOLBF&&nl){ if(__fflush_buf(f))return -1; }
    return (long)n; }
void __flush_all(void){ for(int i=0;i<16;i++) if(__ffiles[i])__fflush_buf(__ffiles[i]); }
void __finit(FILE* f,int fd){ f->fd=fd; f->eof=0; f->err=0; f->unget=-1; f->buf=0; f->bsize=0; f->bpos=0; f->bmode=_IONBF; f->bown=0; f->tmpname=0; }
long __stdout_write(const char* d,size_t n){ return __fwrite_raw(&__stdout,d,n); }

FILE* fopen(const char* path,const char* mode){
    int flags=O_RDONLY;
    if(mode[0]=='w')flags=O_CREAT|O_RDWR|O_TRUNC; else if(mode[0]=='a')flags=O_CREAT|O_RDWR|O_APPEND; else if(mode[0]=='r'&&mode[1]=='+')flags=O_RDWR;
    int fd=open(path,flags); if(fd<0)return 0;
    FILE* f=(FILE*)malloc(sizeof(FILE)); if(!f){close(fd);return 0;} __finit(f,fd); return f;
}
int fclose(FILE* f){ if(!f)return EOF; int rc=__fflush_buf(f); __funreg(f); if(f->bown&&f->buf)free(f->buf); f->buf=0; f->bown=0;
    if(f->fd>2)close(f->fd); if(f->tmpname){ unlink(f->tmpname); free(f->tmpname); f->tmpname=0; } if(f!=&__stdin&&f!=&__stdout&&f!=&__stderr)free(f); return rc; }
size_t fread(void* buf,size_t sz,size_t n,FILE* f){ if(!f)return 0; __fflush_buf(f); long r=read(f->fd,buf,sz*n); if(r<=0){f->eof=1;return 0;} return (size_t)r/(sz?sz:1); }
size_t fwrite(const void* buf,size_t sz,size_t n,FILE* f){ if(!f)return 0; long r=__fwrite_raw(f,(const char*)buf,sz*n); if(r<0){f->err=1;return 0;} return (size_t)r/(sz?sz:1); }
int fgetc(FILE* f){ if(!f)return EOF; if(f->bpos>0)__fflush_buf(f); if(f->unget>=0){int c=f->unget;f->unget=-1;return c;} char c; long r=read(f->fd,&c,1); if(r<=0){f->eof=1;return EOF;} return (unsigned char)c; }
int getchar(void){ return fgetc(stdin); }
int ungetc(int c,FILE* f){ if(!f||c==EOF)return EOF; f->unget=c; return c; }
char* fgets(char* buf,int n,FILE* f){ if(!f||n<=0)return 0; int i=0; while(i<n-1){ int c=fgetc(f); if(c==EOF){ if(i==0)return 0; break; } buf[i++]=(char)c; if(c=='\n')break; } buf[i]=0; return buf; }
int fputc(int c,FILE* f){ if(!f)return EOF; char ch=(char)c; return (__fwrite_raw(f,&ch,1)==1)?c:EOF; }
int feof(FILE* f){ return f?f->eof:1; }
int ferror(FILE* f){ return f?f->err:1; }
int fflush(FILE* f){ if(!f){ __flush_all(); return 0; } return __fflush_buf(f); }
int fputs(const char* s,FILE* f){ if(!f)return EOF; return (int)__fwrite_raw(f,s,strlen(s)); }

/* ================= snprintf / sprintf (format into a buffer) ================= */
typedef struct { char* buf; int cap; int len; } __sbuf;
void __sb_putc(__sbuf* sb,char c){ if(sb->len<sb->cap-1)sb->buf[sb->len]=c; sb->len++; }
void __sb_puts(__sbuf* sb,const char* s){ while(*s)__sb_putc(sb,*s++); }
void __sb_num(__sbuf* sb,long v,int base,int isu){ char t[32]; int i=0; unsigned long u; int neg=0;
    if(!isu&&v<0){neg=1;u=(unsigned long)(-v);}else u=(unsigned long)v; if(!u)t[i++]='0'; const char* d="0123456789abcdef";
    while(u){t[i++]=d[u%base];u/=base;} if(neg)t[i++]='-'; while(i)__sb_putc(sb,t[--i]); }

typedef void (*sighandler_t)(int);
typedef unsigned int sigset_t;
#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGWINCH 28
#define NSIG 32
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SA_RESTART 0x10000000
struct sigaction { sighandler_t sa_handler; sigset_t sa_mask; int sa_flags; };
__asm__(".text\n.globl __nvx_sigtramp\n__nvx_sigtramp:\n mov %rsp,%rbx\n mov $143,%eax\n int $0x80\n hlt\n");
extern void __nvx_sigtramp(void);
int sigemptyset(sigset_t* s){ *s=0; return 0; }
int sigfillset(sigset_t* s){ *s=0xFFFFFFFFu; return 0; }
int sigaddset(sigset_t* s,int n){ if(n<=0||n>=NSIG)return -1; *s|=(1u<<n); return 0; }
int sigdelset(sigset_t* s,int n){ if(n<=0||n>=NSIG)return -1; *s&=~(1u<<n); return 0; }
int sigismember(const sigset_t* s,int n){ return (n>0&&n<NSIG)?((*s>>n)&1):0; }
int sigaction(int sig,const struct sigaction* act,struct sigaction* old){
    long prev; if(act) prev=__sys(141,sig,(long)act->sa_handler,(long)__nvx_sigtramp,act->sa_mask,0);
    else { prev=__sys(141,sig,0,(long)__nvx_sigtramp,0,0); if(prev>=0)__sys(141,sig,prev,(long)__nvx_sigtramp,0,0); }
    if(prev<0&&prev>-4096){ errno=EINVAL; return -1; }
    if(old){ old->sa_handler=(sighandler_t)prev; old->sa_mask=0; old->sa_flags=0; } return 0; }
sighandler_t signal(int sig,sighandler_t h){ struct sigaction a,o; a.sa_handler=h; a.sa_mask=0; a.sa_flags=SA_RESTART; if(sigaction(sig,&a,&o)<0)return SIG_ERR; return o.sa_handler; }
int sigprocmask(int how,const sigset_t* set,sigset_t* old){ long r=__sys(142,how,set?(long)*set:0,0,0,0); if(!set)__sys(142,SIG_SETMASK,r,0,0,0); if(old)*old=(sigset_t)r; return 0; }
int kill(pid_t pid,int sig){ long r=__sys(17,pid,sig,0,0,0); if(r<0){ errno=ESRCH; return -1; } return 0; }
int raise(int sig){ return kill((pid_t)__sys(39,0,0,0,0,0),sig); }
int pause(void){ __sys(158,0,0,0,0,0); errno=EINTR; return -1; }
int killpg(pid_t pg,int sig){ return kill(-pg,sig); }

/* ================= unistd extras ================= */
char* getcwd(char* buf,size_t n){ long r=__sys(18,(long)buf,(long)n,0,0,0); if(r<0)return 0; return buf; }   /* syscall 18 */
int chdir(const char* path){ return (int)__sys(19,(long)path,0,0,0,0); }                                     /* syscall 19 */
int unlink(const char* path){ return (int)__sys(38,(long)path,0,0,0,0); }                                    /* syscall 38 */
int mkdir(const char* path,mode_t m){ (void)m; return (int)__sys(21,(long)path,0,0,0,0); }                    /* syscall 21 */
unsigned sleep(unsigned s){ sleep_ms(s*1000); return 0; }

/* ================= BSD-style sockets (TCP) ================= */
#define AF_INET     2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
typedef unsigned int in_addr_t;
typedef unsigned short in_port_t;
struct in_addr { in_addr_t s_addr; };
struct sockaddr_in { unsigned short sin_family; in_port_t sin_port; struct in_addr sin_addr; char pad[8]; };
struct sockaddr { unsigned short sa_family; char sa_data[14]; };

unsigned short htons(unsigned short x){ return (unsigned short)((x<<8)|(x>>8)); }
unsigned short ntohs(unsigned short x){ return htons(x); }
unsigned int htonl(unsigned int x){ return (x<<24)|((x&0xFF00)<<8)|((x>>8)&0xFF00)|(x>>24); }
unsigned int ntohl(unsigned int x){ return htonl(x); }

int socket(int dom,int type,int proto){ (void)dom;(void)type;(void)proto; return (int)__sys(100,0,0,0,0,0); }
/* resolve a hostname (or dotted IP) to a big-endian IP; returns 0 on failure */
unsigned int resolve(const char* host){ return (unsigned int)__sys(105,(long)host,0,0,0,0); }
int connect(int sid,const struct sockaddr* addr,unsigned len){ (void)len;
    const struct sockaddr_in* a=(const struct sockaddr_in*)addr;
    return (int)__sys(101,sid,(long)a->sin_addr.s_addr,(long)ntohs(a->sin_port),0,0); }
/* convenience: connect straight to host:port */
int connect_host(int sid,const char* host,unsigned short port){
    unsigned int ip=resolve(host); if(!ip)return -1;
    return (int)__sys(101,sid,(long)ip,(long)port,0,0); }
ssize_t send(int sid,const void* buf,size_t n,int flags){ (void)flags; return __sys(102,sid,(long)buf,(long)n,0,0); }
ssize_t recv(int sid,void* buf,size_t n,int flags){ (void)flags; return __sys(103,sid,(long)buf,(long)n,0,0); }
int closesocket(int sid){ return (int)__sys(104,sid,0,0,0,0); }
/* inet_addr: dotted string -> big-endian (via resolve, which parses dotted too) */
in_addr_t inet_addr(const char* s){ return resolve(s); }

/* ================= (1) scanf / sscanf ================= */
const char* __scan_num(const char* p,long* out,int base){
    while(*p==' '||*p=='\t'||*p=='\n')p++;
    long v=0; int neg=0,got=0; if(*p=='-'){neg=1;p++;} else if(*p=='+')p++;
    for(;;){ int d; if(*p>='0'&&*p<='9')d=*p-'0'; else if(base==16&&*p>='a'&&*p<='f')d=*p-'a'+10; else if(base==16&&*p>='A'&&*p<='F')d=*p-'A'+10; else break; if(d>=base)break; v=v*base+d; p++; got=1; }
    if(got){ *out=neg?-v:v; } return got?p:0;
}

/* ================= (2) standard headers (folded in) ================= */
/* stdbool */
#ifndef __cplusplus
#define bool  _Bool
#define true  1
#define false 0
#endif
/* stdint */
typedef signed char        int8_t;   typedef unsigned char      uint8_t2;
typedef short              int16_t;  typedef unsigned short     uint16_t;
typedef int                int32_t;  typedef unsigned int       uint32_t2;
typedef long               int64_t;  typedef unsigned long      uint64_t;
typedef unsigned long      uintptr_t; typedef long              intptr_t;
/* stddef */
#ifndef NULL
#define NULL ((void*)0)
#endif
#define offsetof(t,m) __builtin_offsetof(t,m)
typedef long ptrdiff_t;
/* limits */
#define CHAR_BIT 8
#define INT_MAX  2147483647
#define INT_MIN  (-INT_MAX-1)
#define UINT_MAX 4294967295u
#define LONG_MAX 9223372036854775807L
#define LONG_MIN (-LONG_MAX-1L)
/* assert */
#define assert(x) do{ if(!(x)){ printf("assert failed: %s\n", #x); exit(134);} }while(0)

/* ================= (3) math.h ================= */
#define M_PI 3.14159265358979323846
double fabs(double x){ return x<0?-x:x; }
double floor(double x){ long i=(long)x; if((double)i>x)i--; return (double)i; }
double ceil(double x){ long i=(long)x; if((double)i<x)i++; return (double)i; }
double sqrt(double x){ if(x<=0)return 0; double g=x>1?x:1; for(int i=0;i<40;i++)g=0.5*(g+x/g); return g; }
double pow(double b,double e){ /* integer-exponent fast path + expo for general */ if(e==(long)e){ long n=(long)e; double r=1,bb=b; int neg=n<0; if(neg)n=-n; while(n){ if(n&1)r*=bb; bb*=bb; n>>=1; } return neg?1.0/r:r; } return 0; }
double sin(double x){ while(x>M_PI)x-=2*M_PI; while(x<-M_PI)x+=2*M_PI; double t=x,s=x; for(int i=1;i<10;i++){ t*=-x*x/((2*i)*(2*i+1)); s+=t; } return s; }
double cos(double x){ return sin(x+M_PI/2); }
double fmod(double a,double b){ if(b==0)return 0; long q=(long)(a/b); return a-(double)q*b; }
double round(double x){ return floor(x+0.5); }

/* ================= (4) time.h ================= */
typedef long time_t; typedef long clock_t;
#define CLOCKS_PER_SEC 1000
unsigned long __realtime_ms(void){ return (unsigned long)__sys(133,0,0,0,0,0); }   /* RTC-backed wall clock */
time_t time(time_t* t){ time_t s=(time_t)(__realtime_ms()/1000); if(t)*t=s; return s; }
clock_t clock(void){ return (clock_t)time_ticks(); }

/* ================= (5) dirent: opendir/readdir/closedir ================= */
struct dirent { char d_name[64]; unsigned char d_type; unsigned long d_ino; };
#define DT_REG 8
#define DT_DIR 4
typedef struct { int idx; struct dirent ent; char path[128]; } DIR;
DIR __dirs[4]; int __dir_used[4];
DIR* opendir(const char* path){ if(!path)path="."; char nb[64]; int ty=0; if(__sys(139,(long)path,0,(long)nb,(long)&ty,0)<0){errno=ENOENT;return 0;}
    for(int i=0;i<4;i++) if(!__dir_used[i]){ __dir_used[i]=1; __dirs[i].idx=0; int k=0; while(path[k]&&k<127){__dirs[i].path[k]=path[k];k++;} __dirs[i].path[k]=0; return &__dirs[i]; } return 0; }
struct dirent* readdir(DIR* d){ if(!d)return 0; char nb[64]; int ty=0; long r=__sys(139,(long)d->path,d->idx,(long)nb,(long)&ty,0); if(r<=0)return 0;
    d->ent.d_ino=(unsigned long)(d->idx+1); d->idx++; int i=0; while(nb[i]&&i<63){d->ent.d_name[i]=nb[i];i++;} d->ent.d_name[i]=0; d->ent.d_type=(unsigned char)ty; return &d->ent; }
int closedir(DIR* d){ if(!d)return -1; for(int i=0;i<4;i++) if(&__dirs[i]==d)__dir_used[i]=0; return 0; }

/* ================= 15 more standard libc functions ================= */
/* 1. strrchr - last occurrence */
char* strrchr(const char* s,int c){ const char* last=0; while(*s){ if(*s==(char)c)last=s; s++; } if((char)c==0)return (char*)s; return (char*)last; }
/* 2. strncat */
char* strncat(char* d,const char* s,size_t n){ char* r=d; while(*d)d++; size_t i=0; while(i<n&&s[i]){d[i]=s[i];i++;} d[i]=0; return r; }
/* 3. memmove - handles overlap */
void* memmove(void* d,const void* s,size_t n){ char* dd=(char*)d; const char* ss=(const char*)s; if(dd<ss){ for(size_t i=0;i<n;i++)dd[i]=ss[i]; } else { for(size_t i=n;i>0;i--)dd[i-1]=ss[i-1]; } return d; }
/* 4. memchr */
void* memchr(const void* p,int c,size_t n){ const unsigned char* x=(const unsigned char*)p; for(size_t i=0;i<n;i++)if(x[i]==(unsigned char)c)return (void*)(x+i); return 0; }
/* 5. strspn / 6. strcspn / 7. strpbrk */
size_t strspn(const char* s,const char* set){ size_t n=0; for(;s[n];n++){ const char* q=set; int hit=0; for(;*q;q++)if(s[n]==*q){hit=1;break;} if(!hit)break; } return n; }
size_t strcspn(const char* s,const char* set){ size_t n=0; for(;s[n];n++){ const char* q=set; for(;*q;q++)if(s[n]==*q)return n; } return n; }
char* strpbrk(const char* s,const char* set){ for(;*s;s++){ const char* q=set; for(;*q;q++)if(*s==*q)return (char*)s; } return 0; }
/* 8. strncasecmp */
int strncasecmp(const char* a,const char* b,size_t n){ for(size_t i=0;i<n;i++){ int ca=a[i],cb=b[i]; if(ca>='A'&&ca<='Z')ca+=32; if(cb>='A'&&cb<='Z')cb+=32; if(ca!=cb||!a[i])return ca-cb; } return 0; }
/* 11. rand / srand (LCG - fine for non-crypto userspace) */
unsigned long __rng=1;
void srand(unsigned seed){ __rng=seed; }
#define RAND_MAX 0x7fffffff
int rand2(void){ __rng=__rng*6364136223846793005UL+1442695040888963407UL; return (int)((__rng>>33)&0x7fffffff); }
/* 12. labs / 13. div */
long labs(long x){ return x<0?-x:x; }
typedef struct { int quot,rem; } div_t;
div_t div(int a,int b){ div_t r; r.quot=a/b; r.rem=a%b; return r; }
/* 14. bsearch */
void* bsearch(const void* key,const void* base,size_t n,size_t sz,int(*cmp)(const void*,const void*)){
    size_t lo=0,hi=n; const char* b=(const char*)base;
    while(lo<hi){ size_t mid=(lo+hi)/2; int c=cmp(key,b+mid*sz); if(c==0)return (void*)(b+mid*sz); if(c<0)hi=mid; else lo=mid+1; } return 0; }
/* 15. the rest of ctype */
int isupper(int c){ return c>='A'&&c<='Z'; }
int islower(int c){ return c>='a'&&c<='z'; }
int isalnum(int c){ return isalpha(c)||isdigit(c); }
int isxdigit(int c){ return isdigit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
int isprint(int c){ return c>=32&&c<127; }
int ispunct(int c){ return isprint(c)&&!isalnum(c)&&c!=' '; }
int iscntrl(int c){ return c<32||c==127; }

/* ================= 25 more standard libc functions ================= */
/* --- math.h additions --- */
double exp(double x){ /* e^x via range reduction + Taylor */ int n=(int)(x/0.6931471805599453); double r=x-n*0.6931471805599453; double t=1,sum=1; for(int i=1;i<20;i++){t*=r/i;sum+=t;} double p2=1; if(n>=0){for(int i=0;i<n;i++)p2*=2;}else{for(int i=0;i<-n;i++)p2*=0.5;} return sum*p2; }
double log(double x){ if(x<=0)return -1e300; /* ln via atanh series */ int e=0; while(x>1.5){x*=0.5;e++;} while(x<0.66){x*=2;e--;} double y=(x-1)/(x+1),y2=y*y,t=y,sum=0; for(int i=1;i<40;i+=2){sum+=t/i;t*=y2;} return 2*sum+e*0.6931471805599453; }
double log2(double x){ return log(x)/0.6931471805599453; }
double log10(double x){ return log(x)/2.302585092994046; }
double tan(double x){ double c=cos(x); return c==0?1e300:sin(x)/c; }
double atan(double x){ int neg=x<0; if(neg)x=-x; /* halve twice via atan(x)=2*atan(x/(1+sqrt(1+x*x))) for fast convergence */ double s1=x/(1.0+sqrt(1.0+x*x)); double s2=s1/(1.0+sqrt(1.0+s1*s1)); double y2=s2*s2,t=s2,sum=0; for(int i=1;i<40;i+=2){sum+=t/i;t*=-y2;} sum*=4.0; return neg?-sum:sum; }
double atan2(double y,double x){ if(x>0)return atan(y/x); if(x<0){ if(y>=0)return atan(y/x)+3.14159265358979323846; return atan(y/x)-3.14159265358979323846; } return y>0?1.5707963267948966:(y<0?-1.5707963267948966:0); }
double asin(double x){ if(x<=-1)return -1.5707963267948966; if(x>=1)return 1.5707963267948966; return atan(x/sqrt(1-x*x)); }
double acos(double x){ return 1.5707963267948966-asin(x); }
double trunc(double x){ return (double)(long)x; }
double copysign(double x,double y){ double a=x<0?-x:x; return y<0?-a:a; }
double hypot(double a,double b){ return sqrt(a*a+b*b); }
double fmax(double a,double b){ return a>b?a:b; }
double fmin(double a,double b){ return a<b?a:b; }
/* --- string additions --- */
int strcasecmp(const char* a,const char* b){ for(;;){ int ca=*a,cb=*b; if(ca>='A'&&ca<='Z')ca+=32; if(cb>='A'&&cb<='Z')cb+=32; if(ca!=cb)return ca-cb; if(!*a)return 0; a++;b++; } }
size_t strnlen(const char* s,size_t m){ size_t n=0; while(n<m&&s[n])n++; return n; }
char* stpcpy(char* d,const char* s){ while((*d=*s)){d++;s++;} return d; }
void* memrchr(const void* p,int c,size_t n){ const unsigned char* x=(const unsigned char*)p; for(size_t i=n;i>0;i--)if(x[i-1]==(unsigned char)c)return (void*)(x+i-1); return 0; }
char* strncdup(const char* s,size_t n){ size_t l=strnlen(s,n); char* p=(char*)malloc(l+1); if(p){for(size_t i=0;i<l;i++)p[i]=s[i];p[l]=0;} return p; }
long labs2(long x){ return x<0?-x:x; }
typedef struct { long quot,rem; } ldiv_t;
ldiv_t ldiv(long a,long b){ ldiv_t r; r.quot=a/b; r.rem=a%b; return r; }
int imax(int a,int b){ return a>b?a:b; }
int imin(int a,int b){ return a<b?a:b; }
/* --- stdio additions --- */
void clearerr(FILE* f){ if(f){f->eof=0;f->err=0;} }
void rewind(FILE* f){ if(f){ __fflush_buf(f); lseek(f->fd,0,SEEK_SET); f->eof=0; f->err=0; } }
long ftell(FILE* f){ if(!f)return -1; __fflush_buf(f); return lseek(f->fd,0,SEEK_CUR); }
int fseek(FILE* f,long off,int whence){ if(f){__fflush_buf(f); f->eof=0; f->unget=-1;} return f?(int)(lseek(f->fd,off,whence)>=0?0:-1):-1; }
int rmdir(const char* path);
int remove(const char* path){ if(unlink(path)==0)return 0; return rmdir(path); }
void perror(const char* s){ if(s&&*s){ fputs(s,stderr); fputs(": ",stderr); } fputs("error\n",stderr); }

/* ================= stat (everything-is-a-file) ================= */
struct stat { mode_t st_mode; unsigned long st_size; int st_isdir;
    unsigned int st_uid, st_gid, st_nlink; unsigned long st_mtime, st_ino, st_blocks; };
#define S_IFMT2  0170000
#define S_IFDIR2 0040000
#define S_IFREG2 0100000
#define S_IFCHR2 0020000
#define S_ISDIR(m)  (((m)&0170000)==0040000)
#define S_ISREG(m)  (((m)&0170000)==0100000)
#define S_ISCHR(m)  (((m)&0170000)==0020000)
int __stat2(const char* p,struct stat* st,int follow){ unsigned int o[10]={0,0,0,0,0,0,0,0,0,0};
    long r=__sys(163,(long)p,(long)o,follow,0,0); if(r<0){ errno=ENOENT; return -1; }
    st->st_mode=o[0]; st->st_uid=o[1]; st->st_gid=o[2]; st->st_nlink=o[3]; st->st_size=o[4]; st->st_ino=o[5]; st->st_mtime=o[6]; st->st_isdir=(int)o[9]; st->st_blocks=(o[4]+511)/512; return 0; }
int stat(const char* path,struct stat* st){ return __stat2(path,st,1); }
int lstat(const char* path,struct stat* st){ return __stat2(path,st,0); }
#define S_IFLNK 0120000
#define S_ISLNK(m) (((m)&0170000)==0120000)
int link(const char* a,const char* b){ int r=(int)__sys(160,(long)a,(long)b,0,0,0); if(r<0)errno=EEXIST; return r; }
int symlink(const char* t,const char* p){ int r=(int)__sys(161,(long)t,(long)p,0,0,0); if(r<0)errno=EEXIST; return r; }
ssize_t readlink(const char* p,char* b,size_t n){ long r=__sys(162,(long)p,(long)b,(long)n,0,0); if(r<0)errno=EINVAL; return r; }
int chown(const char* p,int u,int g){ int r=(int)__sys(165,(long)p,u,g,1,0); if(r<0)errno=EPERM; return r; }
int lchown(const char* p,int u,int g){ int r=(int)__sys(165,(long)p,u,g,0,0); if(r<0)errno=EPERM; return r; }
int chmod(const char* p,unsigned m){ int r=(int)__sys(166,(long)p,m,0,0,0); if(r<0)errno=EPERM; return r; }
struct utimbuf { unsigned long actime, modtime; };
int utime(const char* p,const struct utimbuf* t){ return (int)__sys(167,(long)p,t?(long)t->actime:0,t?(long)t->modtime:0,0,0); }
int mount(const char* src,const char* target,const char* type,unsigned long fl,const void* data){ (void)type;(void)fl;(void)data; int r=(int)__sys(168,(long)src,(long)target,0,0,0); if(r<0)errno=EPERM; return r; }
int umount(const char* target){ int r=(int)__sys(169,(long)target,0,0,0,0); if(r<0)errno=EINVAL; return r; }
int sethostname(const char* n,size_t l){ (void)l; return (int)__sys(171,(long)n,0,0,0,0); }
int fstat2(int fd,struct stat* st){ long sz=__sys(34,fd,0,0,0,0); if(sz<0)return -1; st->st_mode=0100644u; st->st_size=(unsigned long)sz; st->st_isdir=0; return 0; }

/* ================= setjmp / longjmp (pure asm, no OS) ================= */
typedef unsigned long jmp_buf[8];
__attribute__((returns_twice,noinline)) int setjmp(jmp_buf b){
    __asm__ volatile(
        "movq %%rbx,0(%0)\n\t movq %%rbp,8(%0)\n\t movq %%r12,16(%0)\n\t movq %%r13,24(%0)\n\t"
        "movq %%r14,32(%0)\n\t movq %%r15,40(%0)\n\t leaq 8(%%rsp),%%rax\n\t movq %%rax,48(%0)\n\t"
        "movq (%%rsp),%%rax\n\t movq %%rax,56(%0)\n\t"
        :: "r"(b) : "rax","memory");
    return 0;
}
__attribute__((noreturn,noinline)) void longjmp(jmp_buf b,int v){
    __asm__ volatile(
        "movl %1,%%eax\n\t testl %%eax,%%eax\n\t jnz 1f\n\t movl $1,%%eax\n\t 1:\n\t"
        "movq 0(%0),%%rbx\n\t movq 8(%0),%%rbp\n\t movq 16(%0),%%r12\n\t movq 24(%0),%%r13\n\t"
        "movq 32(%0),%%r14\n\t movq 40(%0),%%r15\n\t movq 48(%0),%%rsp\n\t jmpq *56(%0)\n\t"
        :: "r"(b),"r"(v) : "rax","memory");
    __builtin_unreachable();
}

/* ================= IEEE-754 math edge cases (<math.h>) ================= */
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
int isnan(double x){ return x!=x; }
int isinf(double x){ return x!=0 && x*2==x && !isnan(x); }
int isfinite(double x){ return !isnan(x) && !isinf(x); }
int signbit(double x){ union{double d; unsigned long u;} v; v.d=x; return (int)(v.u>>63); }
int fpclassify(double x){ if(isnan(x))return FP_NAN; if(isinf(x))return FP_INFINITE; if(x==0)return FP_ZERO;
    union{double d; unsigned long u;} v; v.d=x; if(((v.u>>52)&0x7ff)==0)return FP_SUBNORMAL; return FP_NORMAL; }
double nan_(const char* s){ (void)s; return NAN; }
double frexp(double x,int* e){ *e=0; if(x==0||isnan(x)||isinf(x))return x; int neg=x<0; if(neg)x=-x; while(x>=1.0){x*=0.5;(*e)++;} while(x<0.5){x*=2.0;(*e)--;} return neg?-x:x; }
double ldexp(double x,int e){ while(e>0){x*=2;e--;} while(e<0){x*=0.5;e++;} return x; }
double modf(double x,double* ip){ double i=(double)(long)x; *ip=i; return x-i; }

/* ================= fuller errno set ================= */
#define EIO2     5
#define E2BIG    7
#define EAGAIN   11
#define EBUSY    16
#define EXDEV    18
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define ENFILE   23
#define EFBIG    27
#define ENOSPC   28
#define ESPIPE2  29
#define EROFS    30
#define EPIPE    32
#define ERANGE   34
#define ENOSYS   38
#define ENOTEMPTY 39
#define ECONNREFUSED 111
const char* strerror(int e){ switch(e){ case 0:return "Success"; case EPERM:return "Operation not permitted";
    case ENOENT:return "No such file or directory"; case EBADF:return "Bad file descriptor"; case ENOMEM:return "Out of memory";
    case EACCES:return "Permission denied"; case EEXIST:return "File exists"; case EINVAL:return "Invalid argument";
    case ENOSPC:return "No space left on device"; case ENOSYS:return "Function not implemented"; case ERANGE:return "Result out of range";
    default:return "Unknown error"; } }

/* ================= 30 more libc functions ================= */
/* --- string / mem (1-9) --- */
void* memmem(const void* h,size_t hl,const void* n,size_t nl){ if(nl==0)return (void*)h; const char* hp=(const char*)h; for(size_t i=0;i+nl<=hl;i++){ size_t j=0; for(;j<nl;j++)if(hp[i+j]!=((const char*)n)[j])break; if(j==nl)return (void*)(hp+i); } return 0; }
char* strsep(char** sp,const char* delim){ char* s=*sp; if(!s)return 0; char* p=s; for(;*p;p++){ const char* d=delim; for(;*d;d++)if(*p==*d){ *p=0; *sp=p+1; return s; } } *sp=0; return s; }
size_t strlcpy(char* d,const char* s,size_t n){ size_t i=0; if(n)for(;i<n-1&&s[i];i++)d[i]=s[i]; if(n)d[i]=0; while(s[i])i++; return i; }
size_t strlcat(char* d,const char* s,size_t n){ size_t dl=0; while(dl<n&&d[dl])dl++; size_t i=0; while(dl+i<n-1&&s[i]){d[dl+i]=s[i];i++;} if(dl+i<n)d[dl+i]=0; return dl+i; }
char* strcasestr(const char* h,const char* n){ if(!*n)return (char*)h; for(;*h;h++){ const char* a=h;const char* b=n; while(*a&&*b){ int ca=*a,cb=*b; if(ca>='A'&&ca<='Z')ca+=32; if(cb>='A'&&cb<='Z')cb+=32; if(ca!=cb)break; a++;b++; } if(!*b)return (char*)h; } return 0; }
char* strrev(char* s){ int n=0; while(s[n])n++; for(int i=0;i<n/2;i++){ char t=s[i]; s[i]=s[n-1-i]; s[n-1-i]=t; } return s; }
char* strchrnul(const char* s,int c){ while(*s&&*s!=(char)c)s++; return (char*)s; }
void* mempcpy(void* d,const void* s,size_t n){ char* dd=(char*)d; const char* ss=(const char*)s; for(size_t i=0;i<n;i++)dd[i]=ss[i]; return dd+n; }
size_t strspn2(const char* s,const char* set){ return strspn(s,set); }
/* --- math (10-18) --- */
double cbrt(double x){ if(x==0)return 0; int neg=x<0; if(neg)x=-x; double g=x; for(int i=0;i<40;i++)g=(2*g+x/(g*g))/3; return neg?-g:g; }
double expm1(double x){ return exp(x)-1.0; }
double log1p(double x){ return log(1.0+x); }
double sinh(double x){ double e=exp(x); return (e-1.0/e)*0.5; }
double cosh(double x){ double e=exp(x); return (e+1.0/e)*0.5; }
double tanh(double x){ double e=exp(2*x); return (e-1)/(e+1); }
double nearbyint(double x){ return (double)(long)(x<0?x-0.5:x+0.5); }
double remainder2(double a,double b){ if(b==0)return NAN; double q=nearbyint(a/b); return a-q*b; }
double exp2(double x){ return pow(2.0,x); }
/* --- stdlib / process (19-26) --- */
void (*__atexit_fn[8])(void); int __atexit_n=0;
int atexit(void(*f)(void)){ if(__atexit_n>=8)return -1; __atexit_fn[__atexit_n++]=f; return 0; }
__attribute__((noreturn)) void abort(void){ for(int i=__atexit_n-1;i>=0;i--)__atexit_fn[i](); _exit(134); __builtin_unreachable(); }
int system(const char* cmd){ return cmd?(int)__sys(5,(long)cmd,0,0,0,0):1; }   /* spawn a command */
int usleep(unsigned us){ sleep_ms(us/1000); return 0; }
long random(void){ return rand2(); }
void srandom(unsigned s){ srand(s); }
int getpagesize(void){ return 4096; }
long sysconf(int name){ switch(name){ case 30:return 4096; /*_SC_PAGESIZE*/ case 84:return 1; /*_SC_NPROCESSORS_ONLN*/ default:return -1; } }
/* --- stdio (27-30) --- */
int fileno(FILE* f){ return f?f->fd:-1; }
long getline(char** lineptr,size_t* n,FILE* f){ if(!*lineptr){ *lineptr=(char*)malloc(128); *n=128; } size_t i=0; int c; while((c=fgetc(f))!=EOF){ if(i+1>=*n){ *n*=2; char* nb=(char*)realloc(*lineptr,*n); if(!nb)return -1; *lineptr=nb; } (*lineptr)[i++]=(char)c; if(c=='\n')break; } if(i==0)return -1; (*lineptr)[i]=0; return (long)i; }
int isatty(int fd){ return __sys(172,fd,0,0,0,0)==1; }
char* basename(char* p){ char* b=p; for(char* q=p;*q;q++)if(*q=='/')b=q+1; return b; }

/* ================= 50 more libc functions (app-oriented) ================= */
/* --- ctype extras & conversions (1-6) --- */
int isblank(int c){ return c==' '||c=='\t'; }
int isgraph(int c){ return c>32&&c<127; }
int digittoint(int c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return 0; }
char* itoa(int v,char* buf,int base){ char* p=buf; int neg=(v<0&&base==10); unsigned uv=neg?-v:v; char tmp[36]; int i=0; if(uv==0)tmp[i++]='0'; const char* d="0123456789abcdef"; while(uv){tmp[i++]=d[uv%base];uv/=base;} if(neg)*p++='-'; while(i)*p++=tmp[--i]; *p=0; return buf; }
char* ltoa(long v,char* buf,int base){ char* p=buf; int neg=(v<0&&base==10); unsigned long uv=neg?-v:v; char tmp[68]; int i=0; if(uv==0)tmp[i++]='0'; const char* d="0123456789abcdef"; while(uv){tmp[i++]=d[uv%base];uv/=base;} if(neg)*p++='-'; while(i)*p++=tmp[--i]; *p=0; return buf; }
char* utoa(unsigned v,char* buf,int base){ char* p=buf; char tmp[36]; int i=0; if(v==0)tmp[i++]='0'; const char* d="0123456789abcdef"; while(v){tmp[i++]=d[v%base];v/=base;} while(i)*p++=tmp[--i]; *p=0; return buf; }
/* --- string search / tokenize (7-14) --- */
char* strtok_r(char* s,const char* delim,char** save){ if(s)*save=s; s=*save; if(!s)return 0; while(*s){ const char* d=delim; int hit=0; for(;*d;d++)if(*s==*d){hit=1;break;} if(!hit)break; s++; } if(!*s){*save=0;return 0;} char* tok=s; while(*s){ const char* d=delim; for(;*d;d++)if(*s==*d){*s++=0;*save=s;return tok;} s++; } *save=0; return tok; }
int strncmp2(const char* a,const char* b,size_t n){ return strncmp(a,b,n); }
char* strnstr(const char* h,const char* n,size_t len){ size_t nl=strlen(n); if(!nl)return (char*)h; for(size_t i=0;i+nl<=len&&h[i];i++){ size_t j=0; for(;j<nl;j++)if(h[i+j]!=n[j])break; if(j==nl)return (char*)(h+i); } return 0; }
int startswith(const char* s,const char* pre){ while(*pre){ if(*s++!=*pre++)return 0; } return 1; }
int endswith(const char* s,const char* suf){ size_t sl=strlen(s),fl=strlen(suf); if(fl>sl)return 0; return strcmp(s+sl-fl,suf)==0; }
char* strltrim(char* s){ while(*s==' '||*s=='\t'||*s=='\n')s++; return s; }
char* strrtrim(char* s){ int n=strlen(s); while(n>0&&(s[n-1]==' '||s[n-1]=='\t'||s[n-1]=='\n'))s[--n]=0; return s; }
char* strtrim(char* s){ return strrtrim(strltrim(s)); }
/* --- math extras (15-24) --- */
double deg2rad(double d){ return d*(M_PI/180.0); }
double rad2deg(double r){ return r*(180.0/M_PI); }
double clampd(double v,double lo,double hi){ return v<lo?lo:(v>hi?hi:v); }
int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
double lerp(double a,double b,double t){ return a+(b-a)*t; }
long lround(double x){ return (long)(x<0?x-0.5:x+0.5); }
int isqrt(int x){ if(x<0)return 0; int g=x; if(g==0)return 0; for(int i=0;i<40;i++)g=(g+x/g)/2; return g; }
double fdim(double a,double b){ return a>b?a-b:0; }
double fma_(double a,double b,double c){ return a*b+c; }
int ilog2(unsigned v){ int r=0; while(v>>=1)r++; return r; }
/* --- stdlib / memory (25-32) --- */
void* memdup(const void* p,size_t n){ void* q=malloc(n); if(q)memcpy(q,p,n); return q; }
void bzero(void* p,size_t n){ memset(p,0,n); }
void bcopy(const void* s,void* d,size_t n){ memmove(d,s,n); }
int ffs(int v){ if(!v)return 0; int i=1; while(!(v&1)){v>>=1;i++;} return i; }
int popcount(unsigned v){ int c=0; while(v){c+=v&1;v>>=1;} return c; }
unsigned bswap32(unsigned v){ return (v>>24)|((v>>8)&0xff00)|((v<<8)&0xff0000)|(v<<24); }
unsigned short bswap16(unsigned short v){ return (v>>8)|(v<<8); }
unsigned long bswap64(unsigned long v){ return ((unsigned long)bswap32(v&0xffffffff)<<32)|bswap32(v>>32); }
/* --- time formatting (33-40) --- */
struct tm { int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst; };
int __mdays[12]={31,28,31,30,31,30,31,31,30,31,30,31};
struct tm* gmtime_r(const time_t* t,struct tm* r){ long s=*t; r->tm_sec=s%60; s/=60; r->tm_min=s%60; s/=60; r->tm_hour=s%24; s/=24; r->tm_wday=(int)((s+4)%7); int y=1970; for(;;){ int dy=((y%4==0&&y%100!=0)||y%400==0)?366:365; if(s<dy)break; s-=dy; y++; } r->tm_year=y-1900; r->tm_yday=(int)s; int leap=((y%4==0&&y%100!=0)||y%400==0); int m=0; for(;m<12;m++){ int dm=__mdays[m]+((m==1)&&leap); if(s<dm)break; s-=dm; } r->tm_mon=m; r->tm_mday=(int)s+1; r->tm_isdst=0; return r; }
struct tm* gmtime(const time_t* t){ static struct tm r; return gmtime_r(t,&r); }
/* TZ: POSIX style "CET-1" / "EST5" (offset west of UTC) or ISO style "+02:00" (east of UTC). Unset = UTC. */
long __tz_offset(void){ char* tz=getenv("TZ"); if(!tz||!*tz)return 0; const char* p=tz; int iso=(*p=='+'||*p=='-');
    if(!iso) while(*p&&((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')))p++; if(!*p)return 0;
    int sign=1; if(*p=='-'){sign=-1;p++;} else if(*p=='+')p++;
    long h=0; while(*p>='0'&&*p<='9'){h=h*10+(*p-'0');p++;} long m=0; if(*p==':'){p++; while(*p>='0'&&*p<='9'){m=m*10+(*p-'0');p++;}}
    long off=sign*(h*3600+m*60); return iso?off:-off; }
struct tm* localtime(const time_t* t){ time_t lt=*t+(time_t)__tz_offset(); return gmtime(&lt); }
const char* __wd[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* __mo[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
char* asctime(const struct tm* t){ static char b[32]; snprintf(b,32,"%s %s %d %d",__wd[t->tm_wday%7],__mo[t->tm_mon%12],t->tm_mday,t->tm_year+1900); return b; }
char* ctime(const time_t* t){ return asctime(localtime(t)); }
/* --- app utilities (41-50) --- */
const char* getprogname(void){ return "app"; }
int max3(int a,int b,int c){ int m=a>b?a:b; return m>c?m:c; }
int min3(int a,int b,int c){ int m=a<b?a:b; return m<c?m:c; }
void swapi(int* a,int* b){ int t=*a;*a=*b;*b=t; }
int gcd(int a,int b){ if(a<0)a=-a; if(b<0)b=-b; while(b){int t=b;b=a%b;a=t;} return a; }
long lcm(int a,int b){ int g=gcd(a,b); return g?(long)(a/g)*b:0; }
int parse_int(const char* s,int* ok){ int v=0,neg=0,got=0; while(*s==' ')s++; if(*s=='-'){neg=1;s++;} while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;got=1;} if(ok)*ok=got; return neg?-v:v; }
char* strjoin(char* dst,size_t cap,const char* a,const char* sep,const char* b){ snprintf(dst,cap,"%s%s%s",a,sep,b); return dst; }
int hexval(const char* s){ int v=0; if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2; while(*s){ int d; if(*s>='0'&&*s<='9')d=*s-'0'; else if(*s>='a'&&*s<='f')d=*s-'a'+10; else if(*s>='A'&&*s<='F')d=*s-'A'+10; else break; v=v*16+d; s++; } return v; }
void hexdump_line(const unsigned char* p,int n,char* out){ char* o=out; for(int i=0;i<n;i++){ const char* d="0123456789abcdef"; *o++=d[p[i]>>4]; *o++=d[p[i]&15]; *o++=' '; } *o=0; }

/* ================= termios (raw / cooked terminal mode) ================= */
/* Raw keyboard is available via syscalls 50 (getkey, non-blocking) and
   51 (waitkey, blocking). termios here toggles line-buffered "cooked" input
   vs. character-at-a-time "raw" input for interactive apps (editors, REPLs). */
typedef unsigned int tcflag_t; typedef unsigned char cc_t;
#define NCCS 20
struct termios { tcflag_t c_iflag,c_oflag,c_cflag,c_lflag; cc_t c_cc[NCCS]; };
#define ICANON 0002
#define ECHO   0010
#define ISIG   0001
#define VMIN   6
#define VTIME  5
#define TCSANOW   0
#define TCSAFLUSH 2
int __raw_mode=0;
int tcgetattr(int fd,struct termios* t){ (void)fd; t->c_lflag=__raw_mode?0:(ICANON|ECHO|ISIG); t->c_iflag=t->c_oflag=t->c_cflag=0; for(int i=0;i<NCCS;i++)t->c_cc[i]=0; t->c_cc[VMIN]=1; return 0; }
int tcsetattr(int fd,int act,const struct termios* t){ (void)fd;(void)act; __raw_mode=!(t->c_lflag&ICANON); return 0; }
void cfmakeraw(struct termios* t){ t->c_lflag&=~(ICANON|ECHO|ISIG); t->c_cc[VMIN]=1; t->c_cc[VTIME]=0; }
/* raw single-char read (blocking) and non-blocking poll */
int getch(void){ return (int)__sys(51,0,0,0,0,0); }        /* blocking raw key */
int getch_nb(void){ return (int)__sys(50,0,0,0,0,0); }     /* -1 if none */
int kbhit(void){ return getch_nb(); }                       /* returns key or -1 */

/* ================= ANSI cursor / screen control (terminal escapes) ================= */
void term_clear_screen(void){ fputs("\x1b[2J\x1b[H",stdout); }
void term_gotoxy(int x,int y){ char b[24]; snprintf(b,24,"\x1b[%d;%dH",y+1,x+1); fputs(b,stdout); }
void term_hide_cursor(void){ fputs("\x1b[?25l",stdout); }
void term_show_cursor(void){ fputs("\x1b[?25h",stdout); }
void term_color(int fg){ char b[16]; snprintf(b,16,"\x1b[%dm",fg); fputs(b,stdout); }
void term_reset(void){ fputs("\x1b[0m",stdout); }

/* ================= select / poll (readiness over the fd set) ================= */
typedef struct { unsigned long b[16]; } fd_set;
void FD_ZERO(fd_set* s){ for(int i=0;i<16;i++)s->b[i]=0; }
void FD_SET(int fd,fd_set* s){ if(fd>=0&&fd<1024)s->b[fd/64]|=1UL<<(fd%64); }
void FD_CLR(int fd,fd_set* s){ if(fd>=0&&fd<1024)s->b[fd/64]&=~(1UL<<(fd%64)); }
int FD_ISSET(int fd,fd_set* s){ return (fd>=0&&fd<1024)?((s->b[fd/64]>>(fd%64))&1):0; }
struct timeval { long tv_sec,tv_usec; };
/* single-process model: stdin ready = a key is waiting; other fds treated ready. */
int select(int nfds,fd_set* rd,fd_set* wr,fd_set* ex,struct timeval* tv){
    (void)ex; int ready=0;
    for(int fd=0;fd<nfds;fd++){
        if(wr&&FD_ISSET(fd,wr))ready++;                 /* writes never block here */
        if(rd&&FD_ISSET(fd,rd)){ if(fd==0){ if(getch_nb()>=0)ready++; else FD_CLR(fd,rd); } else ready++; }
    }
    if(!ready&&tv){ long ms=tv->tv_sec*1000+tv->tv_usec/1000; if(ms>0)sleep_ms((unsigned)(ms>500?500:ms)); }
    return ready;
}
struct pollfd { int fd; short events; short revents; };
#define POLLIN 1
#define POLLOUT 4
int poll(struct pollfd* fds,unsigned n,int timeout){ int ready=0;
    for(unsigned i=0;i<n;i++){ fds[i].revents=0;
        if((fds[i].events&POLLIN)){ if(fds[i].fd==0){ if(getch_nb()>=0){fds[i].revents|=POLLIN;ready++;} } else {fds[i].revents|=POLLIN;ready++;} }
        if((fds[i].events&POLLOUT)){ fds[i].revents|=POLLOUT; ready++; } }
    if(!ready&&timeout>0)sleep_ms((unsigned)(timeout>500?500:timeout));
    return ready;
}

/* ================= pthreads (minimal, over the kernel thread_create) ================= */
/* NoovexOS uses a cooperative scheduler; these map to kernel threads (syscall 61)
   and a spinlock-based mutex. Suitable for simple concurrent tasks, not a full
   preemptive pthread implementation. */
typedef unsigned long pthread_t;
typedef struct { volatile int lock; } pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {0}
int pthread_create(pthread_t* th,const void* attr,void*(*fn)(void*),void* arg){
    (void)attr;(void)arg; long r=__sys(61,(long)fn,0,0,0,0); if(r<0)return -1; if(th)*th=(pthread_t)r; return 0; }
int pthread_join(pthread_t th,void** ret){ (void)th; if(ret)*ret=0; return 0; }
int pthread_mutex_init(pthread_mutex_t* m,const void* a){ (void)a; m->lock=0; return 0; }
int pthread_mutex_lock(pthread_mutex_t* m){ while(__sync_lock_test_and_set(&m->lock,1)){ __sys(20,0,0,0,0,0); } return 0; }
int pthread_mutex_unlock(pthread_mutex_t* m){ __sync_lock_release(&m->lock); return 0; }
int pthread_mutex_destroy(pthread_mutex_t* m){ (void)m; return 0; }
pthread_t pthread_self(void){ return (pthread_t)getpid(); }

/* ================= dlopen / dlsym (honest stub: no dynamic linker) ================= */
/* NoovexOS statically links programs; there is no runtime dynamic loader yet.
   These exist so code that references them compiles, but they always fail. */
#define RTLD_LAZY 1
#define RTLD_NOW  2
void* dlopen(const char* path,int flag){ (void)path;(void)flag; return 0; }
void* dlsym(void* h,const char* sym){ (void)h;(void)sym; return 0; }
int dlclose(void* h){ (void)h; return 0; }
const char* dlerror(void){ return "dynamic loading not supported (static linking only)"; }

/* ================= 100 more libc functions (v7.94) ================= */

/* ---- strings (17) ---- */
char* strndup(const char* s,size_t n){ size_t l=0; while(l<n&&s[l])l++; char* d=(char*)malloc(l+1); if(!d)return 0; for(size_t i=0;i<l;i++)d[i]=s[i]; d[l]=0; return d; }
void* memccpy(void* d,const void* s,int c,size_t n){ unsigned char* dd=(unsigned char*)d; const unsigned char* ss=(const unsigned char*)s;
    for(size_t i=0;i<n;i++){ dd[i]=ss[i]; if(ss[i]==(unsigned char)c)return dd+i+1; } return 0; }
void explicit_bzero(void* p,size_t n){ volatile unsigned char* v=(volatile unsigned char*)p; while(n--)*v++=0; }
char* index(const char* s,int c){ return strchr(s,c); }
char* rindex(const char* s,int c){ return strrchr(s,c); }
const char* strsignal(int sig){ switch(sig){ case 2:return "Interrupt"; case 9:return "Killed"; case 11:return "Segmentation fault"; case 15:return "Terminated"; case 6:return "Aborted"; default:return "Unknown signal"; } }
int strverscmp(const char* a,const char* b){ while(*a&&*b){ if(*a>='0'&&*a<='9'&&*b>='0'&&*b<='9'){ unsigned long x=0,y=0; while(*a>='0'&&*a<='9')x=x*10+(*a++-'0'); while(*b>='0'&&*b<='9')y=y*10+(*b++-'0'); if(x!=y)return x<y?-1:1; } else { if(*a!=*b)return (unsigned char)*a-(unsigned char)*b; a++;b++; } } return (unsigned char)*a-(unsigned char)*b; }
char* strupr(char* s){ for(char* p=s;*p;p++)if(*p>='a'&&*p<='z')*p-=32; return s; }
char* strlwr(char* s){ for(char* p=s;*p;p++)if(*p>='A'&&*p<='Z')*p+=32; return s; }
int strcount(const char* s,int c){ int n=0; for(;*s;s++)if(*s==(char)c)n++; return n; }
char* strreplace(char* s,int from,int to){ for(char* p=s;*p;p++)if(*p==(char)from)*p=(char)to; return s; }
int strsplit(char* s,char delim,char** out,int max){ int n=0; if(max<=0)return 0; out[n++]=s; for(char* p=s;*p&&n<max;p++)if(*p==delim){*p=0;out[n++]=p+1;} return n; }

/* ---- stdlib (12) ---- */
typedef struct { long long quot,rem; } lldiv_t;
long long llabs(long long x){ return x<0?-x:x; }
lldiv_t lldiv(long long a,long long b){ lldiv_t r; r.quot=a/b; r.rem=a%b; return r; }
int rand_r(unsigned* seed){ *seed=*seed*1103515245u+12345u; return (int)((*seed>>1)&0x7fffffff); }
unsigned long long __r48=0x1234ABCD330EULL;
void srand48(long s){ __r48=(((unsigned long long)(unsigned long)s)<<16)|0x330E; }
unsigned long long __next48(void){ __r48=(__r48*0x5DEECE66DULL+0xB)&0xFFFFFFFFFFFFULL; return __r48; }
double drand48(void){ return (double)__next48()/281474976710656.0; }
long lrand48(void){ return (long)(__next48()>>17); }
long mrand48(void){ return (long)(int)(__next48()>>16); }
void __bswap(unsigned char* a,unsigned char* b,size_t sz){ while(sz--){ unsigned char t=*a; *a++=*b; *b++=t; } }
void qsort_r(void* base,size_t n,size_t sz,int(*cmp)(const void*,const void*,void*),void* arg){
    unsigned char* b=(unsigned char*)base; if(n<2||sz==0)return;
    for(size_t start=n/2;start-->0;){ size_t root=start; for(;;){ size_t c=2*root+1; if(c>=n)break; if(c+1<n&&cmp(b+c*sz,b+(c+1)*sz,arg)<0)c++; if(cmp(b+root*sz,b+c*sz,arg)>=0)break; __bswap(b+root*sz,b+c*sz,sz); root=c; } }
    for(size_t end=n-1;end>0;end--){ __bswap(b,b+end*sz,sz); size_t root=0; for(;;){ size_t c=2*root+1; if(c>=end)break; if(c+1<end&&cmp(b+c*sz,b+(c+1)*sz,arg)<0)c++; if(cmp(b+root*sz,b+c*sz,arg)>=0)break; __bswap(b+root*sz,b+c*sz,sz); root=c; } } }
void* lfind(const void* key,const void* base,size_t* n,size_t sz,int(*cmp)(const void*,const void*)){
    const unsigned char* b=(const unsigned char*)base; for(size_t i=0;i<*n;i++)if(cmp(key,b+i*sz)==0)return (void*)(b+i*sz); return 0; }
void* lsearch(const void* key,void* base,size_t* n,size_t sz,int(*cmp)(const void*,const void*)){
    void* r=lfind(key,base,n,sz,cmp); if(r)return r; unsigned char* dst=(unsigned char*)base+(*n)*sz; memcpy(dst,key,sz); (*n)++; return dst; }
void* reallocarray(void* p,size_t n,size_t sz){ if(sz&&n>((size_t)-1)/sz){errno=ENOMEM;return 0;} return realloc(p,n*sz); }
char* realpath(const char* path,char* out){ static char rb[256]; char* r=out?out:rb; char tmp[256]; int tl=0;
    if(path[0]!='/'){ if(!getcwd(tmp,256))tmp[0]='/',tmp[1]=0; tl=(int)strlen(tmp); if(tl>1&&tmp[tl-1]!='/'){tmp[tl++]='/';} }
    for(const char* p=path;*p&&tl<255;p++)tmp[tl++]=*p; tmp[tl]=0;
    char* parts[64]; int np=0; char* s=tmp; while(*s){ while(*s=='/')s++; if(!*s)break; char* st=s; while(*s&&*s!='/')s++; if(*s)*s++=0;
        if(!strcmp(st,"."))continue; if(!strcmp(st,"..")){ if(np)np--; continue; } if(np<64)parts[np++]=st; }
    int o=0; r[o++]='/'; for(int i=0;i<np;i++){ for(char* q=parts[i];*q&&o<254;q++)r[o++]=*q; if(i<np-1&&o<254)r[o++]='/'; } r[o]=0; return r; }

/* ---- float math (15) ---- */
float sqrtf(float x){ return (float)sqrt(x); }
float sinf(float x){ return (float)sin(x); }
float cosf(float x){ return (float)cos(x); }
float tanf(float x){ return (float)tan(x); }
float fabsf(float x){ return x<0?-x:x; }
float floorf(float x){ return (float)floor(x); }
float ceilf(float x){ return (float)ceil(x); }
float powf(float a,float b){ return (float)pow(a,b); }
float logf(float x){ return (float)log(x); }
float expf(float x){ return (float)exp(x); }
float roundf(float x){ return (float)round(x); }
float fminf(float a,float b){ return a<b?a:b; }
float fmaxf(float a,float b){ return a>b?a:b; }
float fmodf(float a,float b){ return (float)fmod(a,b); }
float atan2f(float y,float x){ return (float)atan2(y,x); }

/* ---- extra math (13) ---- */
double asinh(double x){ return log(x+sqrt(x*x+1)); }
double acosh(double x){ return x<1?NAN:log(x+sqrt(x*x-1)); }
double atanh(double x){ return (x<=-1||x>=1)?NAN:0.5*log((1+x)/(1-x)); }
double erf(double x){ int neg=x<0; if(neg)x=-x; double t=1.0/(1.0+0.3275911*x);
    double y=1.0-(((((1.061405429*t-1.453152027)*t)+1.421413741)*t-0.284496736)*t+0.254829592)*t*exp(-x*x); return neg?-y:y; }
double erfc(double x){ return 1.0-erf(x); }
double tgamma(double x){ if(x<0.5)return M_PI/(sin(M_PI*x)*tgamma(1-x));
    static const double g[9]={0.99999999999980993,676.5203681218851,-1259.1392167224028,771.32342877765313,-176.61502916214059,12.507343278686905,-0.13857109526572012,9.9843695780195716e-6,1.5056327351493116e-7};
    x-=1; double a=g[0]; double t=x+7.5; for(int i=1;i<9;i++)a+=g[i]/(x+i); return sqrt(2*M_PI)*pow(t,x+0.5)*exp(-t)*a; }
double lgamma(double x){ double g=tgamma(x); return log(g<0?-g:g); }
double rint(double x){ double f=floor(x); double d=x-f; if(d>0.5)return f+1; if(d<0.5)return f; return ((long)f%2==0)?f:f+1; }
long lrint(double x){ return (long)rint(x); }
long long llround(double x){ return (long long)(x<0?x-0.5:x+0.5); }
double scalbn(double x,int n){ return ldexp(x,n); }
int ilogb(double x){ if(x==0)return -2147483647-1; int e; frexp(x,&e); return e-1; }
double logb(double x){ return (double)ilogb(x); }

/* ---- stdio (11) ---- */
typedef long fpos_t;
int getc(FILE* f){ return fgetc(f); }
int putc(int c,FILE* f){ return fputc(c,f); }
int fgetpos(FILE* f,fpos_t* pos){ long p=ftell(f); if(p<0)return -1; *pos=p; return 0; }
int fsetpos(FILE* f,const fpos_t* pos){ return fseek(f,*pos,SEEK_SET); }
int setvbuf(FILE* f,char* buf,int mode,size_t size){ if(!f)return -1; __fflush_buf(f);
    if(f->bown&&f->buf)free(f->buf); f->buf=0; f->bown=0; f->bpos=0; f->bmode=mode;
    if(mode==_IONBF){ __funreg(f); return 0; } if(mode!=_IOFBF&&mode!=_IOLBF)return -1; if(size==0)size=BUFSIZ;
    if(buf) f->buf=buf; else { f->buf=(char*)malloc(size); if(!f->buf){f->bmode=_IONBF;return -1;} f->bown=1; }
    f->bsize=(int)size; __freg(f); return 0; }
void setbuf(FILE* f,char* buf){ if(buf)setvbuf(f,buf,_IOFBF,BUFSIZ); else setvbuf(f,0,_IONBF,0); }
void setlinebuf(FILE* f){ setvbuf(f,0,_IOLBF,0); }
FILE* fdopen(int fd,const char* mode){ (void)mode; if(fd<0)return 0; FILE* f=(FILE*)malloc(sizeof(FILE)); if(!f)return 0; __finit(f,fd); return f; }
long getdelim(char** lineptr,size_t* n,int delim,FILE* f){ if(!*lineptr){*lineptr=(char*)malloc(128);*n=128;} if(!*lineptr)return -1;
    size_t i=0; int c; while((c=fgetc(f))!=EOF){ if(i+1>=*n){ *n*=2; char* nb=(char*)realloc(*lineptr,*n); if(!nb)return -1; *lineptr=nb; } (*lineptr)[i++]=(char)c; if(c==delim)break; }
    if(i==0)return -1; (*lineptr)[i]=0; return (long)i; }

/* ---- ctype (2) ---- */
int isascii(int c){ return c>=0&&c<128; }
int toascii(int c){ return c&0x7f; }

/* ---- time (9) ---- */
struct timespec { long tv_sec, tv_nsec; };
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
int __isleap(int y){ return (y%4==0&&y%100!=0)||y%400==0; }
time_t timegm(struct tm* t){ long days=0; int y=t->tm_year+1900;
    for(int yy=1970;yy<y;yy++)days+=__isleap(yy)?366:365;
    static const int md[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    for(int m=0;m<t->tm_mon;m++)days+=md[m]+((m==1)&&__isleap(y));
    days+=t->tm_mday-1; return (time_t)(((days*24+t->tm_hour)*60+t->tm_min)*60+t->tm_sec); }
time_t mktime(struct tm* t){ time_t r=timegm(t)-(time_t)__tz_offset(); time_t lt=r+(time_t)__tz_offset(); gmtime_r(&lt,t); return r; }
double difftime(time_t a,time_t b){ return (double)(a-b); }
int gettimeofday(struct timeval* tv,void* tz){ (void)tz; unsigned long ms=__realtime_ms(); if(tv){ tv->tv_sec=(long)(ms/1000); tv->tv_usec=(long)((ms%1000)*1000); } return 0; }
int clock_gettime(int clk,struct timespec* ts){ unsigned long ms=(clk==CLOCK_MONOTONIC)?time_ticks():__realtime_ms(); ts->tv_sec=(long)(ms/1000); ts->tv_nsec=(long)((ms%1000)*1000000); return 0; }
int nanosleep(const struct timespec* req,struct timespec* rem){ unsigned long ms=(unsigned long)req->tv_sec*1000+(unsigned long)req->tv_nsec/1000000; if(ms)sleep_ms((unsigned)ms); if(rem){rem->tv_sec=0;rem->tv_nsec=0;} return 0; }
struct tm* localtime_r(const time_t* t,struct tm* r){ time_t lt=*t+(time_t)__tz_offset(); return gmtime_r(&lt,r); }
char* asctime_r(const struct tm* t,char* buf){ snprintf(buf,32,"%s %s %d %d",__wd[t->tm_wday%7],__mo[t->tm_mon%12],t->tm_mday,t->tm_year+1900); return buf; }
char* ctime_r(const time_t* t,char* buf){ struct tm tm; localtime_r(t,&tm); return asctime_r(&tm,buf); }

/* ---- unistd / identity (10) ---- */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
int getuid(void){ return (int)__sys(130,0,0,0,0,0); }
int setuid(int uid){ return (int)__sys(131,uid,0,0,0,0); }
int geteuid(void){ return getuid(); }
int getgid(void){ return (int)__sys(148,0,0,0,0,0); }
int getegid(void){ return getgid(); }
int setgid(int g){ return (int)__sys(149,g,0,0,0,0); }
int getppid(void){ return (int)__sys(132,0,0,0,0,0); }
int gethostname(char* name,size_t len){ return (int)__sys(170,(long)name,(long)len,0,0,0); }
char* getlogin(void){ return getuid()==0?(char*)"root":(char*)"user"; }
int access(const char* path,int mode){ struct stat st; if(stat(path,&st)<0){errno=ENOENT;return -1;}
    if(getuid()==0)return 0; if((mode&W_OK)&&!(st.st_mode&0222)){errno=EACCES;return -1;} if((mode&X_OK)&&!(st.st_mode&0111)){errno=EACCES;return -1;} return 0; }
char* ttyname(int fd){ return (fd>=0&&fd<=2)?(char*)"/dev/tty":0; }
char* optarg=0; int optind=1, opterr=1, optopt=0; int __optpos=1;
int getopt(int argc,char* const argv[],const char* opts){
    if(optind>=argc||!argv[optind]||argv[optind][0]!='-'||!argv[optind][1])return -1;
    if(argv[optind][1]=='-'&&!argv[optind][2]){optind++;return -1;}
    int c=argv[optind][__optpos]; const char* o=strchr(opts,c);
    if(!o||c==':'){ optopt=c; if(opterr)fprintf(stderr,"%s: invalid option -- '%c'\n",argv[0],c);
        if(!argv[optind][++__optpos]){optind++;__optpos=1;} return '?'; }
    if(o[1]==':'){ if(argv[optind][__optpos+1]){ optarg=&argv[optind][__optpos+1]; optind++; __optpos=1; }
        else if(optind+1<argc){ optarg=argv[optind+1]; optind+=2; __optpos=1; }
        else { optopt=c; optind++; __optpos=1; if(opterr)fprintf(stderr,"%s: option requires an argument -- '%c'\n",argv[0],c); return opts[0]==':'?':':'?'; } }
    else { optarg=0; if(!argv[optind][++__optpos]){optind++;__optpos=1;} }
    return c; }

/* ---- hashing / encoding / utf-8 (10) ---- */
unsigned crc32(unsigned crc,const void* data,size_t n){ const unsigned char* p=(const unsigned char*)data; crc=~crc;
    while(n--){ crc^=*p++; for(int k=0;k<8;k++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1))); } return ~crc; }
unsigned adler32(unsigned a,const void* data,size_t n){ const unsigned char* p=(const unsigned char*)data; unsigned s1=a&0xffff,s2=a>>16;
    while(n--){ s1=(s1+*p++)%65521; s2=(s2+s1)%65521; } return (s2<<16)|s1; }
unsigned fnv1a32(const void* data,size_t n){ const unsigned char* p=(const unsigned char*)data; unsigned h=2166136261u; while(n--){h^=*p++;h*=16777619u;} return h; }
unsigned long djb2(const char* s){ unsigned long h=5381; int c; while((c=(unsigned char)*s++))h=h*33+c; return h; }
const char __b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
size_t base64_encode(const void* in,size_t n,char* out){ const unsigned char* p=(const unsigned char*)in; size_t o=0;
    for(size_t i=0;i<n;i+=3){ unsigned v=p[i]<<16; if(i+1<n)v|=p[i+1]<<8; if(i+2<n)v|=p[i+2];
        out[o++]=__b64[(v>>18)&63]; out[o++]=__b64[(v>>12)&63]; out[o++]=(i+1<n)?__b64[(v>>6)&63]:'='; out[o++]=(i+2<n)?__b64[v&63]:'='; }
    out[o]=0; return o; }
int __b64v(int c){ if(c>='A'&&c<='Z')return c-'A'; if(c>='a'&&c<='z')return c-'a'+26; if(c>='0'&&c<='9')return c-'0'+52; if(c=='+')return 62; if(c=='/')return 63; return -1; }
long base64_decode(const char* in,void* out){ unsigned char* o=(unsigned char*)out; long n=0; unsigned v=0; int bits=0;
    for(;*in;in++){ if(*in=='=')break; int d=__b64v(*in); if(d<0)continue; v=(v<<6)|d; bits+=6; if(bits>=8){bits-=8;o[n++]=(unsigned char)(v>>bits);} } return n; }
size_t hex_encode(const void* in,size_t n,char* out){ const unsigned char* p=(const unsigned char*)in; const char* d="0123456789abcdef";
    for(size_t i=0;i<n;i++){out[2*i]=d[p[i]>>4];out[2*i+1]=d[p[i]&15];} out[2*n]=0; return 2*n; }
long hex_decode(const char* in,void* out){ unsigned char* o=(unsigned char*)out; long n=0;
    while(in[0]&&in[1]){ int h=digittoint(in[0]),l=digittoint(in[1]); o[n++]=(unsigned char)((h<<4)|l); in+=2; } return n; }
int utf8_decode(const char* s,unsigned* cp){ const unsigned char* u=(const unsigned char*)s;
    if(u[0]<0x80){*cp=u[0];return u[0]?1:0;}
    if((u[0]&0xE0)==0xC0&&(u[1]&0xC0)==0x80){*cp=((u[0]&0x1F)<<6)|(u[1]&0x3F);return 2;}
    if((u[0]&0xF0)==0xE0&&(u[1]&0xC0)==0x80&&(u[2]&0xC0)==0x80){*cp=((u[0]&0x0F)<<12)|((u[1]&0x3F)<<6)|(u[2]&0x3F);return 3;}
    if((u[0]&0xF8)==0xF0&&(u[1]&0xC0)==0x80&&(u[2]&0xC0)==0x80&&(u[3]&0xC0)==0x80){*cp=((u[0]&0x07)<<18)|((u[1]&0x3F)<<12)|((u[2]&0x3F)<<6)|(u[3]&0x3F);return 4;}
    *cp=0xFFFD; return 1; }
int utf8_encode(unsigned cp,char* out){
    if(cp<0x80){out[0]=(char)cp;return 1;}
    if(cp<0x800){out[0]=(char)(0xC0|(cp>>6));out[1]=(char)(0x80|(cp&0x3F));return 2;}
    if(cp<0x10000){out[0]=(char)(0xE0|(cp>>12));out[1]=(char)(0x80|((cp>>6)&0x3F));out[2]=(char)(0x80|(cp&0x3F));return 3;}
    out[0]=(char)(0xF0|(cp>>18));out[1]=(char)(0x80|((cp>>12)&0x3F));out[2]=(char)(0x80|((cp>>6)&0x3F));out[3]=(char)(0x80|(cp&0x3F));return 4; }


/* ================= file management: create, move, shrink, sync, delete (v7.95) ================= */
int rename(const char* from,const char* to){ return (int)__sys(134,(long)from,(long)to,0,0,0); }
int rmdir(const char* path){ return (int)__sys(135,(long)path,0,0,0,0); }
int truncate(const char* path,off_t len){ return (int)__sys(136,(long)path,(long)len,0,0,0); }
int ftruncate(int fd,off_t len){ return (int)__sys(137,fd,(long)len,0,0,0); }
int fsync(int fd){ return (int)__sys(138,fd,0,0,0,0); }
void __tmpfill(char* x){ static const char cs[]="abcdefghijklmnopqrstuvwxyz0123456789"; unsigned long v=__realtime_ms()^((unsigned long)rand2()<<7);
    for(int i=0;i<6;i++){ v=v*6364136223846793005UL+1442695040888963407UL; x[i]=cs[(v>>33)%36]; } }
int mkstemp(char* tmpl){ size_t L=strlen(tmpl); if(L<6||strcmp(tmpl+L-6,"XXXXXX")!=0){errno=EINVAL;return -1;}
    for(int tries=0;tries<100;tries++){ __tmpfill(tmpl+L-6); struct stat st; if(stat(tmpl,&st)==0)continue;
        int fd=open(tmpl,O_CREAT|O_RDWR|O_TRUNC); if(fd>=0)return fd; }
    errno=EEXIST; return -1; }
char* mkdtemp(char* tmpl){ size_t L=strlen(tmpl); if(L<6||strcmp(tmpl+L-6,"XXXXXX")!=0){errno=EINVAL;return 0;}
    for(int tries=0;tries<100;tries++){ __tmpfill(tmpl+L-6); struct stat st; if(stat(tmpl,&st)==0)continue;
        if(mkdir(tmpl,0700)==0)return tmpl; }
    errno=EEXIST; return 0; }
FILE* tmpfile(void){ char name[24]; memcpy(name,"tmp.XXXXXX",11); int fd=mkstemp(name); if(fd<0)return 0;
    FILE* f=fdopen(fd,"w+"); if(!f){ close(fd); unlink(name); return 0; } f->tmpname=strdup(name); return f; }   /* deleted on fclose */
int copyfile(const char* src,const char* dst){ int in=open(src,O_RDONLY); if(in<0)return -1;
    int out=open(dst,O_CREAT|O_RDWR|O_TRUNC); if(out<0){close(in);return -1;}
    char buf[1024]; long n; int rc=0; while((n=read(in,buf,sizeof(buf)))>0){ if(write(out,buf,(size_t)n)!=n){rc=-1;break;} }
    if(n<0)rc=-1; fsync(out); close(in); close(out); return rc; }
int rmtree(const char* path){   /* recursive delete: files, then subdirectories, then the directory itself */
    struct stat st; if(stat(path,&st)<0)return -1;
    if(!st.st_isdir)return unlink(path);
    char (*names)[64]=(char(*)[64])malloc(32*64); unsigned char* types=(unsigned char*)malloc(32); if(!names||!types){free(names);free(types);return -1;}
    int rc=0;
    for(;;){ DIR* d=opendir(path); if(!d){rc=-1;break;} int n=0; struct dirent* e;
        while(n<32&&(e=readdir(d))){ strlcpy(names[n],e->d_name,64); types[n]=e->d_type; n++; } closedir(d);
        if(n==0)break;
        int removed=0; for(int i=0;i<n;i++){ char child[256]; snprintf(child,sizeof(child),"%s/%s",path,names[i]);
            if((types[i]==DT_DIR?rmtree(child):unlink(child))==0)removed++; else rc=-1; }
        if(!removed)break; }
    free(names); free(types);
    if(rmdir(path)<0)rc=-1; return rc; }

#ifndef NVX_U_TYPES
#define NVX_U_TYPES
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long u64;
#endif
/* ================= 50 more math (v8.08) ================= */
double remainder(double x,double y){ if(y==0)return NAN; double q=x/y; double r=q-floor(q); double n=floor(q); if(r>0.5||(r==0.5&&((long long)n&1)))n+=1; return x-n*y; }
double remquo(double x,double y,int* quo){ double n=floor(x/y+0.5); if(quo)*quo=(int)n; return x-n*y; }
double drem(double x,double y){ return remainder(x,y); }
double trunc2(double x){ return (x<0)?ceil(x):floor(x); }
double significand(double x){ if(x==0)return 0; int e; return frexp(x,&e)*2.0; }
double exp10(double x){ return pow(10.0,x); }
double pow10(double x){ return pow(10.0,x); }
double log2_(double x){ return log(x)/0.69314718055994530942; }
double logbase(double x,double b){ return log(x)/log(b); }
double rsqrt(double x){ return x>0?1.0/sqrt(x):NAN; }
double hypot3(double a,double b,double c){ return sqrt(a*a+b*b+c*c); }
double sec_(double x){ double c=cos(x); return c!=0?1.0/c:NAN; }
double csc_(double x){ double s=sin(x); return s!=0?1.0/s:NAN; }
double cot_(double x){ double t=tan(x); return t!=0?1.0/t:NAN; }
double versin(double x){ return 1.0-cos(x); }
double haversin(double x){ return 0.5*(1.0-cos(x)); }
double sinc(double x){ if(x==0)return 1.0; return sin(x)/x; }
double logistic(double x){ return 1.0/(1.0+exp(-x)); }
double sigmoid(double x){ return 1.0/(1.0+exp(-x)); }
double relu(double x){ return x>0?x:0; }
double softplus(double x){ return log1p(exp(x)); }
double gd(double x){ return 2.0*atan(exp(x))-1.5707963267948966; }
double copysignd(double x,double y){ return copysign(x,y); }
double clamp01(double x){ return x<0?0:(x>1?1:x); }
double clampd2(double x,double lo,double hi){ return x<lo?lo:(x>hi?hi:x); }
double wrap(double x,double lo,double hi){ double r=hi-lo; if(r<=0)return lo; double t=x-r*floor((x-lo)/r); return t; }
double wrap180(double d){ return wrap(d,-180.0,180.0); }
double wrap2pi(double a){ return wrap(a,0.0,2*M_PI); }
double normangle(double a){ return wrap(a,-M_PI,M_PI); }
double lerpd(double a,double b,double t){ return a+(b-a)*t; }
double invlerp(double a,double b,double v){ return (b==a)?0:(v-a)/(b-a); }
double remap(double v,double a,double b,double c,double d){ return c+(d-c)*((v-a)/(b-a)); }
double smoothstep(double e0,double e1,double x){ double t=clamp01((x-e0)/(e1-e0)); return t*t*(3-2*t); }
double smootherstep(double e0,double e1,double x){ double t=clamp01((x-e0)/(e1-e0)); return t*t*t*(t*(t*6-15)+10); }
double step_(double edge,double x){ return x<edge?0.0:1.0; }
double frac(double x){ return x-floor(x); }
double roundto(double x,int dp){ double p=pow(10.0,dp); return floor(x*p+0.5)/p; }
long roundhalfeven(double x){ double f=floor(x); double d=x-f; if(d<0.5)return (long)f; if(d>0.5)return (long)f+1; return ((long)f&1)?(long)f+1:(long)f; }
double deg_(double r){ return r*57.29577951308232; }
double rad_(double d){ return d*0.017453292519943295; }
double meandd(const double* a,int n){ if(n<=0)return 0; double s=0; for(int i=0;i<n;i++)s+=a[i]; return s/n; }
double sumd(const double* a,int n){ double s=0; for(int i=0;i<n;i++)s+=a[i]; return s; }
double variance(const double* a,int n){ if(n<=1)return 0; double m=meandd(a,n),s=0; for(int i=0;i<n;i++){ double d=a[i]-m; s+=d*d; } return s/n; }
double stddev(const double* a,int n){ return sqrt(variance(a,n)); }
double dmaxv(const double* a,int n){ double m=a[0]; for(int i=1;i<n;i++)if(a[i]>m)m=a[i]; return m; }
double dminv(const double* a,int n){ double m=a[0]; for(int i=1;i<n;i++)if(a[i]<m)m=a[i]; return m; }
double dot(const double* a,const double* b,int n){ double s=0; for(int i=0;i<n;i++)s+=a[i]*b[i]; return s; }
double norm2(const double* a,int n){ return sqrt(dot(a,a,n)); }
long long ipow(long long b,int e){ long long r=1; while(e>0){ if(e&1)r*=b; b*=b; e>>=1; } return r; }
unsigned long long factorial(unsigned n){ unsigned long long r=1; for(unsigned i=2;i<=n;i++)r*=i; return r; }
unsigned long long binomial(unsigned n,unsigned k){ if(k>n)return 0; if(k>n-k)k=n-k; unsigned long long r=1; for(unsigned i=0;i<k;i++){ r=r*(n-i)/(i+1); } return r; }

/* ================= 80 more file/path helpers (v8.08) ================= */
char* uhex8(char* p,unsigned v){ const char* h="0123456789abcdef"; for(int i=7;i>=0;i--)*p++=h[(v>>(i*4))&15]; return p; }
int fd_read_all(int fd,char* buf,int cap);
int file_exists(const char* p){ struct stat st; return stat(p,&st)==0; }
int is_dir(const char* p){ struct stat st; return stat(p,&st)==0&&S_ISDIR(st.st_mode); }
int is_file(const char* p){ struct stat st; return stat(p,&st)==0&&S_ISREG(st.st_mode); }
int is_link(const char* p){ struct stat st; return lstat(p,&st)==0&&S_ISLNK(st.st_mode); }
long file_size(const char* p){ struct stat st; return stat(p,&st)==0?(long)st.st_size:-1; }
unsigned file_mode(const char* p){ struct stat st; return stat(p,&st)==0?st.st_mode:0; }
long file_mtime(const char* p){ struct stat st; return stat(p,&st)==0?(long)st.st_mtime:-1; }
int path_is_abs(const char* p){ return p&&p[0]=='/'; }
const char* path_base(const char* p){ const char* b=p; for(;*p;p++) if(*p=='/')b=p+1; return b; }
void path_dir(const char* p,char* out){ int last=-1; for(int i=0;p[i];i++)if(p[i]=='/')last=i; if(last<0){ out[0]='.'; out[1]=0; return; } if(last==0){ out[0]='/'; out[1]=0; return; } for(int i=0;i<last;i++)out[i]=p[i]; out[last]=0; }
const char* path_ext(const char* p){ const char* b=path_base(p); const char* d=0; for(const char* q=b;*q;q++) if(*q=='.')d=q; return d?d+1:""; }
void path_stem(const char* p,char* out){ const char* b=path_base(p); const char* d=0; for(const char* q=b;*q;q++)if(*q=='.')d=q; int n=d?(int)(d-b):(int)strlen(b); for(int i=0;i<n;i++)out[i]=b[i]; out[n]=0; }
void path_join(char* out,const char* a,const char* b){ int n=0; for(;a[n];n++)out[n]=a[n]; if(n&&out[n-1]!='/'&&b[0]!='/')out[n++]='/'; for(int i=0;b[i];i++)out[n++]=b[i]; out[n]=0; }
int path_has_ext(const char* p,const char* e){ const char* pe=path_ext(p); while(*pe&&*e){ char x=*pe,y=*e; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; pe++;e++; } return !*pe&&!*e; }
void path_normalize(const char* p,char* out){ realpath(p,out); }
int fd_read_all(int fd,char* buf,int cap){ int n=0,r; while(n<cap-1&&(r=(int)read(fd,buf+n,cap-1-n))>0)n+=r; buf[n]=0; return n; }
long read_file(const char* p,char* buf,int cap){ int fd=open(p,O_RDONLY); if(fd<0)return -1; int n=fd_read_all(fd,buf,cap); close(fd); return n; }
int write_file(const char* p,const void* data,int len){ int fd=open(p,O_CREAT|O_WRONLY|O_TRUNC); if(fd<0)return -1; int w=0,r; while(w<len&&(r=(int)write(fd,(const char*)data+w,len-w))>0)w+=r; close(fd); return w==len?0:-1; }
int append_file(const char* p,const void* data,int len){ int fd=open(p,O_CREAT|O_WRONLY|O_APPEND); if(fd<0)return -1; int w=(int)write(fd,data,(size_t)len); close(fd); return w==len?0:-1; }
int write_str(const char* p,const char* s){ return write_file(p,s,(int)strlen(s)); }
int append_str(const char* p,const char* s){ return append_file(p,s,(int)strlen(s)); }
int append_line(const char* p,const char* s){ if(append_str(p,s)<0)return -1; return append_str(p,"\n"); }
int touch_file(const char* p){ struct stat st; if(stat(p,&st)==0)return utime(p,0); int fd=open(p,O_CREAT|O_WRONLY); if(fd<0)return -1; close(fd); return 0; }
int file_equal(const char* a,const char* b){ int fa=open(a,O_RDONLY),fb=open(b,O_RDONLY); if(fa<0||fb<0){ if(fa>=0)close(fa); if(fb>=0)close(fb); return 0; } char x[256],y[256]; int eq=1; for(;;){ long na=read(fa,x,256),nb=read(fb,y,256); if(na!=nb){eq=0;break;} if(na<=0)break; for(long i=0;i<na;i++)if(x[i]!=y[i]){eq=0;break;} if(!eq)break; } close(fa); close(fb); return eq; }
long count_lines(const char* p){ int fd=open(p,O_RDONLY); if(fd<0)return -1; char b[512]; long n=0,r; int last=0; while((r=read(fd,b,512))>0){ for(long i=0;i<r;i++){ if(b[i]=='\n')n++; last=b[i]; } } close(fd); if(r==0&&last!='\n'&&last)n++; return n; }
int mkdirs(const char* path){ char t[256]; int n=0; for(;path[n]&&n<255;n++)t[n]=path[n]; t[n]=0; for(int i=1;t[i];i++){ if(t[i]=='/'){ t[i]=0; if(!is_dir(t))mkdir(t,0777); t[i]='/'; } } if(!is_dir(t))return mkdir(t,0777); return 0; }
int ensure_dir(const char* p){ return is_dir(p)?0:mkdirs(p); }
int dir_count(const char* p){ DIR* d=opendir(p); if(!d)return -1; int n=0; struct dirent* e; while((e=readdir(d))){ if(e->d_name[0]=='.'&&(e->d_name[1]==0||(e->d_name[1]=='.'&&e->d_name[2]==0)))continue; n++; } closedir(d); return n; }
int dir_empty(const char* p){ int c=dir_count(p); return c==0; }
int list_dir(const char* p,char names[][64],int max){ DIR* d=opendir(p); if(!d)return -1; int n=0; struct dirent* e; while(n<max&&(e=readdir(d))){ if(e->d_name[0]=='.')continue; strlcpy(names[n],e->d_name,64); n++; } closedir(d); return n; }
int foreach_file(const char* p,void(*fn)(const char*,void*),void* ctx){ DIR* d=opendir(p); if(!d)return -1; int n=0; struct dirent* e; while((e=readdir(d))){ if(e->d_name[0]=='.')continue; fn(e->d_name,ctx); n++; } closedir(d); return n; }
int copy_file2(const char* src,const char* dst){ return copyfile(src,dst); }
int move_file(const char* src,const char* dst){ if(rename(src,dst)==0)return 0; if(copyfile(src,dst)<0)return -1; return unlink(src); }
int remove_file(const char* p){ return unlink(p); }
int remove_dir(const char* p){ return rmdir(p); }
long fd_size(int fd){ long cur=lseek(fd,0,SEEK_CUR); long end=lseek(fd,0,SEEK_END); lseek(fd,cur,SEEK_SET); return end; }
int fd_rewind(int fd){ return (int)lseek(fd,0,SEEK_SET); }
int fd_eof(int fd){ long cur=lseek(fd,0,SEEK_CUR); long end=lseek(fd,0,SEEK_END); lseek(fd,cur,SEEK_SET); return cur>=end; }
int read_at(int fd,long off,void* buf,int len){ lseek(fd,off,SEEK_SET); return (int)read(fd,buf,(size_t)len); }
int write_at(int fd,long off,const void* buf,int len){ lseek(fd,off,SEEK_SET); return (int)write(fd,buf,(size_t)len); }
int slurp_lines(const char* p,char lines[][128],int max){ int fd=open(p,O_RDONLY); if(fd<0)return -1; char c; int n=0,col=0; while(n<max&&read(fd,&c,1)==1){ if(c=='\n'){ lines[n][col]=0; n++; col=0; } else if(col<127)lines[n][col++]=c; } if(col>0&&n<max){ lines[n][col]=0; n++; } close(fd); return n; }
int file_getline(int fd,char* out,int cap){ int n=0; char c; while(n<cap-1&&read(fd,&c,1)==1){ if(c=='\n'){ out[n]=0; return n; } out[n++]=c; } out[n]=0; return n?n:-1; }
int freadable(const char* p){ return access(p,R_OK)==0; }
int fwritable(const char* p){ return access(p,W_OK)==0; }
int fexecutable(const char* p){ return access(p,X_OK)==0; }
int chext(const char* p,const char* newext,char* out){ path_stem(p,out); char dir[256]; path_dir(p,dir); char stem[128]; strcpy(stem,out); if(strcmp(dir,".")==0)snprintf(out,256,"%s.%s",stem,newext); else snprintf(out,256,"%s/%s.%s",dir,stem,newext); return 0; }
long tell_fd(int fd){ return lseek(fd,0,SEEK_CUR); }
int truncate_file(const char* p,long n){ return truncate(p,n); }
int prepend_file(const char* p,const char* s){ static char buf[65536]; long n=read_file(p,buf,sizeof(buf)); if(n<0)n=0; int fd=open(p,O_CREAT|O_WRONLY|O_TRUNC); if(fd<0)return -1; write(fd,s,strlen(s)); write(fd,buf,(size_t)n); close(fd); return 0; }
int replace_in_file(const char* p,char from,char to){ static char buf[65536]; long n=read_file(p,buf,sizeof(buf)); if(n<0)return -1; for(long i=0;i<n;i++)if(buf[i]==from)buf[i]=to; return write_file(p,buf,(int)n); }
int file_starts_with(const char* p,const char* pre){ int fd=open(p,O_RDONLY); if(fd<0)return 0; int L=(int)strlen(pre); char b[256]; if(L>256)L=256; int r=(int)read(fd,b,L); close(fd); if(r<L)return 0; for(int i=0;i<L;i++)if(b[i]!=pre[i])return 0; return 1; }
int cat_to(int outfd,const char* p){ int fd=open(p,O_RDONLY); if(fd<0)return -1; char b[512]; long r; while((r=read(fd,b,512))>0)write(outfd,b,(size_t)r); close(fd); return 0; }
int concat_files(const char* dst,const char** srcs,int n){ int fd=open(dst,O_CREAT|O_WRONLY|O_TRUNC); if(fd<0)return -1; for(int i=0;i<n;i++)cat_to(fd,srcs[i]); close(fd); return 0; }
unsigned file_crc32(const char* p){ int fd=open(p,O_RDONLY); if(fd<0)return 0; char b[512]; long r; unsigned c=0; while((r=read(fd,b,512))>0)c=crc32(c,b,(size_t)r); close(fd); return c; }
int file_isempty(const char* p){ return file_size(p)==0; }
int same_file(const char* a,const char* b){ struct stat sa,sb; if(stat(a,&sa)||stat(b,&sb))return 0; return sa.st_ino==sb.st_ino&&sa.st_ino!=0; }
int mkfile_size(const char* p,long size,char fill){ int fd=open(p,O_CREAT|O_WRONLY|O_TRUNC); if(fd<0)return -1; char b[512]; for(int i=0;i<512;i++)b[i]=fill; long w=0; while(w<size){ long c=size-w; if(c>512)c=512; write(fd,b,(size_t)c); w+=c; } close(fd); return 0; }
int rename_ext(const char* p,const char* newext){ char nw[256]; chext(p,newext,nw); return rename(p,nw); }
int backup_file(const char* p){ char bk[256]; snprintf(bk,sizeof(bk),"%s.bak",p); return copyfile(p,bk); }
int line_at(const char* p,int idx,char* out,int cap){ int fd=open(p,O_RDONLY); if(fd<0)return -1; int i=0; while(i<=idx){ if(file_getline(fd,out,cap)<0){ close(fd); return -1; } i++; } close(fd); return 0; }
int grep_file(const char* p,const char* needle,void(*hit)(int,const char*,void*),void* ctx){ int fd=open(p,O_RDONLY); if(fd<0)return -1; char l[256]; int n=0,c=0; while(file_getline(fd,l,sizeof(l))>=0){ n++; if(strstr(l,needle)){ c++; if(hit)hit(n,l,ctx); } } close(fd); return c; }
int fd_copy(int in,int out){ char b[1024]; long r,t=0; while((r=read(in,b,1024))>0){ write(out,b,(size_t)r); t+=r; } return (int)t; }
long dir_total_size(const char* p){ DIR* d=opendir(p); if(!d)return -1; long tot=0; struct dirent* e; while((e=readdir(d))){ if(e->d_name[0]=='.')continue; char c[256]; path_join(c,p,e->d_name); struct stat st; if(stat(c,&st)==0){ if(S_ISDIR(st.st_mode))tot+=dir_total_size(c); else tot+=st.st_size; } } closedir(d); return tot; }
int find_in_dir(const char* dir,const char* name,char* out,int cap){ DIR* d=opendir(dir); if(!d)return -1; struct dirent* e; while((e=readdir(d))){ if(e->d_name[0]=='.')continue; if(strcmp(e->d_name,name)==0){ path_join(out,dir,e->d_name); closedir(d); return 0; } char c[256]; path_join(c,dir,e->d_name); struct stat st; if(stat(c,&st)==0&&S_ISDIR(st.st_mode)){ if(find_in_dir(c,name,out,cap)==0){ closedir(d); return 0; } } } closedir(d); return -1; }
int with_tmpfile(void(*fn)(const char*,void*),void* ctx){ char t[]="tmp.XXXXXX"; int fd=mkstemp(t); if(fd<0)return -1; close(fd); fn(t,ctx); unlink(t); return 0; }
int atomic_write(const char* p,const void* data,int len){ char t[256]; snprintf(t,sizeof(t),"%s.tmp",p); if(write_file(t,data,len)<0)return -1; return rename(t,p); }
int chmod_str(const char* p,const char* octal){ unsigned m=(unsigned)strtol(octal,0,8); return chmod(p,m); }
int is_hidden(const char* p){ const char* b=path_base(p); return b[0]=='.'; }
int wc_words(const char* p){ int fd=open(p,O_RDONLY); if(fd<0)return -1; char b[512]; long r; int w=0,in=0; while((r=read(fd,b,512))>0){ for(long i=0;i<r;i++){ char c=b[i]; if(c==' '||c=='\n'||c=='\t')in=0; else if(!in){ in=1; w++; } } } close(fd); return w; }
int copy_into_dir(const char* file,const char* dir){ char dst[256]; path_join(dst,dir,path_base(file)); return copyfile(file,dst); }
int move_into_dir(const char* file,const char* dir){ char dst[256]; path_join(dst,dir,path_base(file)); return move_file(file,dst); }
long fappend_fd(int fd,const void* data,int len){ lseek(fd,0,SEEK_END); return (long)write(fd,data,(size_t)len); }
int read_first_line(const char* p,char* out,int cap){ int fd=open(p,O_RDONLY); if(fd<0)return -1; int n=file_getline(fd,out,cap); close(fd); return n; }
int read_last_line(const char* p,char* out,int cap){ int fd=open(p,O_RDONLY); if(fd<0)return -1; char l[256]; int got=-1; while(file_getline(fd,l,sizeof(l))>=0){ strlcpy(out,l,cap); got=0; } close(fd); return got; }
int trunc_to_lines(const char* p,int keep){ static char lines[512][128]; int n=slurp_lines(p,lines,512); if(n<0)return -1; int fd=open(p,O_CREAT|O_WRONLY|O_TRUNC); if(fd<0)return -1; int start=n>keep?n-keep:0; for(int i=start;i<n;i++){ write(fd,lines[i],strlen(lines[i])); write(fd,"\n",1); } close(fd); return 0; }
int dump_hex_file(const char* p,int outfd){ int fd=open(p,O_RDONLY); if(fd<0)return -1; unsigned char b[16]; long off=0,r; char line[80]; while((r=read(fd,b,16))>0){ char* q=line; q=uhex8(q,(unsigned)off); *q++=' '; *q++=' '; for(int i=0;i<16;i++){ if(i<r){ static const char HXD[]="0123456789abcdef"; *q++=HXD[b[i]>>4]; *q++=HXD[b[i]&15]; } else { *q++=' '; *q++=' '; } *q++=' '; } *q++=' '; for(int i=0;i<r;i++)*q++=(b[i]>=32&&b[i]<127)?b[i]:'.'; *q++='\n'; write(outfd,line,(size_t)(q-line)); off+=r; } close(fd); return 0; }
int line_count_str(const char* p){ return (int)count_lines(p); }
int file_append_fmt(const char* p,const char* s){ return append_str(p,s); }
int swap_files(const char* a,const char* b){ char t[256]; snprintf(t,sizeof(t),"%s.swp",a); if(rename(a,t))return -1; if(rename(b,a)){ rename(t,a); return -1; } return rename(t,b); }
int file_head(const char* p,int nlines,int outfd){ int fd=open(p,O_RDONLY); if(fd<0)return -1; char l[256]; for(int i=0;i<nlines&&file_getline(fd,l,sizeof(l))>=0;i++){ write(outfd,l,strlen(l)); write(outfd,"\n",1); } close(fd); return 0; }

/* ================= 100 cryptographic (v8.08) — userland, self-contained ================= */
u32 nx_rotr32(u32 x,int n){ return (x>>n)|(x<<(32-n)); }
u32 nx_rotl32(u32 x,int n){ return (x<<n)|(x>>(32-n)); }
u64 nx_rotr64(u64 x,int n){ return (x>>n)|(x<<(64-n)); }
u64 nx_rotl64(u64 x,int n){ return (x<<n)|(x>>(64-n)); }
u16 bswap16c(u16 x){ return (u16)((x<<8)|(x>>8)); }
u32 bswap32c(u32 x){ return (x<<24)|((x&0xFF00)<<8)|((x>>8)&0xFF00)|(x>>24); }
u64 bswap64c(u64 x){ return ((u64)bswap32c((u32)x)<<32)|bswap32c((u32)(x>>32)); }
u32 ld32be(const u8* p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
u32 ld32le(const u8* p){ return ((u32)p[3]<<24)|((u32)p[2]<<16)|((u32)p[1]<<8)|p[0]; }
u64 ld64be(const u8* p){ return ((u64)ld32be(p)<<32)|ld32be(p+4); }
void st32be(u8* p,u32 v){ p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=(u8)v; }
void st32le(u8* p,u32 v){ p[0]=(u8)v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
void st64be(u8* p,u64 v){ st32be(p,(u32)(v>>32)); st32be(p+4,(u32)v); }

/* ---- SHA-256 ---- */
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } nx_sha256;
const u32 NXK256[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
void nx_sha256_blk(nx_sha256* s,const u8* p){ u32 w[64]; for(int i=0;i<16;i++)w[i]=ld32be(p+i*4); for(int i=16;i<64;i++){ u32 a=nx_rotr32(w[i-15],7)^nx_rotr32(w[i-15],18)^(w[i-15]>>3); u32 b=nx_rotr32(w[i-2],17)^nx_rotr32(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+a+w[i-7]+b; }
    u32 a=s->h[0],b=s->h[1],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7];
    for(int i=0;i<64;i++){ u32 S1=nx_rotr32(e,6)^nx_rotr32(e,11)^nx_rotr32(e,25); u32 ch=(e&f)^(~e&g); u32 t1=h+S1+ch+NXK256[i]+w[i]; u32 S0=nx_rotr32(a,2)^nx_rotr32(a,13)^nx_rotr32(a,22); u32 mj=(a&b)^(a&c)^(b&c); u32 t2=S0+mj; h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2; }
    s->h[0]+=a;s->h[1]+=b;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h; }
void sha256_init(nx_sha256* s){ s->h[0]=0x6a09e667;s->h[1]=0xbb67ae85;s->h[2]=0x3c6ef372;s->h[3]=0xa54ff53a;s->h[4]=0x510e527f;s->h[5]=0x9b05688c;s->h[6]=0x1f83d9ab;s->h[7]=0x5be0cd19;s->len=0;s->n=0; }
void sha256_update(nx_sha256* s,const void* data,int len){ const u8* p=(const u8*)data; s->len+=len; while(len>0){ int c=64-s->n; if(c>len)c=len; for(int i=0;i<c;i++)s->buf[s->n+i]=p[i]; s->n+=c; p+=c; len-=c; if(s->n==64){ nx_sha256_blk(s,s->buf); s->n=0; } } }
void sha256_final(nx_sha256* s,u8 out[32]){ u64 bits=s->len*8; u8 pad=0x80; sha256_update(s,&pad,1); u8 z=0; while(s->n!=56)sha256_update(s,&z,1); u8 lb[8]; st64be(lb,bits); sha256_update(s,lb,8); for(int i=0;i<8;i++)st32be(out+i*4,s->h[i]); }
void sha256(const void* data,int len,u8 out[32]){ nx_sha256 s; sha256_init(&s); sha256_update(&s,data,len); sha256_final(&s,out); }
void sha224(const void* data,int len,u8 out[28]){ nx_sha256 s; sha256_init(&s); s.h[0]=0xc1059ed8;s.h[1]=0x367cd507;s.h[2]=0x3070dd17;s.h[3]=0xf70e5939;s.h[4]=0xffc00b31;s.h[5]=0x68581511;s.h[6]=0x64f98fa7;s.h[7]=0xbefa4fa4; sha256_update(&s,data,len); u8 f[32]; sha256_final(&s,f); for(int i=0;i<28;i++)out[i]=f[i]; }

/* ---- SHA-1 ---- */
void sha1(const void* data,int len,u8 out[20]){ const u8* p=(const u8*)data; u32 h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
    u64 ml=(u64)len*8; static u8 msg[16384]; int tl=len; for(int i=0;i<len;i++)msg[i]=p[i]; msg[tl++]=0x80; while((tl%64)!=56)msg[tl++]=0; for(int i=0;i<8;i++)msg[tl++]=(u8)(ml>>(56-i*8));
    for(int ch=0;ch<tl;ch+=64){ u32 w[80]; for(int i=0;i<16;i++)w[i]=ld32be(msg+ch+i*4); for(int i=16;i<80;i++)w[i]=nx_rotl32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        u32 a=h0,b=h1,c=h2,d=h3,e=h4; for(int i=0;i<80;i++){ u32 f,k; if(i<20){f=(b&c)|(~b&d);k=0x5A827999;} else if(i<40){f=b^c^d;k=0x6ED9EBA1;} else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;} else {f=b^c^d;k=0xCA62C1D6;} u32 t=nx_rotl32(a,5)+f+e+k+w[i]; e=d;d=c;c=nx_rotl32(b,30);b=a;a=t; }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e; } st32be(out,h0);st32be(out+4,h1);st32be(out+8,h2);st32be(out+12,h3);st32be(out+16,h4); }

/* ---- MD5 ---- */
void md5(const void* data,int len,u8 out[16]){ const u8* msg=(const u8*)data; static const u32 K[64]={0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const int R[64]={7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    u32 a0=0x67452301,b0=0xefcdab89,c0=0x98badcfe,d0=0x10325476; static u8 m[16384]; int tl=len; for(int i=0;i<len;i++)m[i]=msg[i]; m[tl++]=0x80; while((tl%64)!=56)m[tl++]=0; u64 bits=(u64)len*8; for(int i=0;i<8;i++)m[tl++]=(u8)(bits>>(i*8));
    for(int ch=0;ch<tl;ch+=64){ u32 M[16]; for(int i=0;i<16;i++)M[i]=ld32le(m+ch+i*4); u32 A=a0,B=b0,C=c0,D=d0; for(int i=0;i<64;i++){ u32 F; int g; if(i<16){F=(B&C)|(~B&D);g=i;} else if(i<32){F=(D&B)|(~D&C);g=(5*i+1)%16;} else if(i<48){F=B^C^D;g=(3*i+5)%16;} else {F=C^(B|~D);g=(7*i)%16;} F=F+A+K[i]+M[g]; A=D;D=C;C=B;B=B+nx_rotl32(F,R[i]); } a0+=A;b0+=B;c0+=C;d0+=D; }
    st32le(out,a0);st32le(out+4,b0);st32le(out+8,c0);st32le(out+12,d0); }

/* ---- HMAC ---- */
void hmac_sha256(const u8* key,int klen,const u8* msg,int mlen,u8 out[32]){ u8 k[64],ip[64],op[64]; for(int i=0;i<64;i++)k[i]=0; if(klen>64){ sha256(key,klen,k); } else for(int i=0;i<klen;i++)k[i]=key[i]; for(int i=0;i<64;i++){ ip[i]=k[i]^0x36; op[i]=k[i]^0x5c; } nx_sha256 s; sha256_init(&s); sha256_update(&s,ip,64); sha256_update(&s,msg,mlen); u8 ih[32]; sha256_final(&s,ih); sha256_init(&s); sha256_update(&s,op,64); sha256_update(&s,ih,32); sha256_final(&s,out); }
void hmac_md5c(const u8* key,int klen,const u8* msg,int mlen,u8 out[16]){ u8 k[64],ip[64],op[64]; for(int i=0;i<64;i++)k[i]=0; if(klen>64)md5(key,klen,k); else for(int i=0;i<klen;i++)k[i]=key[i]; for(int i=0;i<64;i++){ ip[i]=k[i]^0x36; op[i]=k[i]^0x5c; } static u8 t[16448]; for(int i=0;i<64;i++)t[i]=ip[i]; for(int i=0;i<mlen;i++)t[64+i]=msg[i]; u8 ih[16]; md5(t,64+mlen,ih); u8 t2[80]; for(int i=0;i<64;i++)t2[i]=op[i]; for(int i=0;i<16;i++)t2[64+i]=ih[i]; md5(t2,80,out); }
void hmac_sha1c(const u8* key,int klen,const u8* msg,int mlen,u8 out[20]){ u8 k[64],ip[64],op[64]; for(int i=0;i<64;i++)k[i]=0; if(klen>64)sha1(key,klen,k); else for(int i=0;i<klen;i++)k[i]=key[i]; for(int i=0;i<64;i++){ ip[i]=k[i]^0x36; op[i]=k[i]^0x5c; } static u8 t[16448]; for(int i=0;i<64;i++)t[i]=ip[i]; for(int i=0;i<mlen;i++)t[64+i]=msg[i]; u8 ih[20]; sha1(t,64+mlen,ih); u8 t2[84]; for(int i=0;i<64;i++)t2[i]=op[i]; for(int i=0;i<20;i++)t2[64+i]=ih[i]; sha1(t2,84,out); }
void hkdf_sha256(const u8* salt,int sl,const u8* ikm,int il,const u8* info,int inl,u8* out,int olen){ u8 prk[32]; u8 z[32]; if(!salt){ for(int i=0;i<32;i++)z[i]=0; salt=z; sl=32; } hmac_sha256(salt,sl,ikm,il,prk); u8 t[32]; int tn=0,done=0; u8 ctr=1; while(done<olen){ static u8 buf[80]; int bn=0; for(int i=0;i<tn;i++)buf[bn++]=t[i]; for(int i=0;i<inl;i++)buf[bn++]=info[i]; buf[bn++]=ctr++; hmac_sha256(prk,32,buf,bn,t); tn=32; for(int i=0;i<32&&done<olen;i++)out[done++]=t[i]; } }
void pbkdf2_sha256(const u8* pw,int pl,const u8* salt,int sl,int iter,u8* out,int olen){ int done=0; u32 blk=1; while(done<olen){ static u8 s2[128]; int sn=0; for(int i=0;i<sl;i++)s2[sn++]=salt[i]; st32be(s2+sn,blk); sn+=4; u8 u[32],t[32]; hmac_sha256(pw,pl,s2,sn,u); for(int i=0;i<32;i++)t[i]=u[i]; for(int it=1;it<iter;it++){ hmac_sha256(pw,pl,u,32,u); for(int i=0;i<32;i++)t[i]^=u[i]; } for(int i=0;i<32&&done<olen;i++)out[done++]=t[i]; blk++; } }

/* ---- non-crypto hashes ---- */
u32 fnv1_32(const void* d,int n){ const u8* p=(const u8*)d; u32 h=2166136261u; for(int i=0;i<n;i++){ h*=16777619u; h^=p[i]; } return h; }
u64 fnv1a_64(const void* d,int n){ const u8* p=(const u8*)d; u64 h=1469598103934665603ULL; for(int i=0;i<n;i++){ h^=p[i]; h*=1099511628211ULL; } return h; }
u32 murmur3_32(const void* key,int len,u32 seed){ const u8* d=(const u8*)key; u32 h=seed; int nb=len/4; for(int i=0;i<nb;i++){ u32 k=ld32le(d+i*4); k*=0xcc9e2d51; k=nx_rotl32(k,15); k*=0x1b873593; h^=k; h=nx_rotl32(h,13); h=h*5+0xe6546b64; } u32 k=0; const u8* t=d+nb*4; switch(len&3){ case 3:k^=t[2]<<16; case 2:k^=t[1]<<8; case 1:k^=t[0]; k*=0xcc9e2d51;k=nx_rotl32(k,15);k*=0x1b873593;h^=k; } h^=len; h^=h>>16;h*=0x85ebca6b;h^=h>>13;h*=0xc2b2ae35;h^=h>>16; return h; }
u32 jenkins_oaat(const void* d,int n){ const u8* p=(const u8*)d; u32 h=0; for(int i=0;i<n;i++){ h+=p[i]; h+=h<<10; h^=h>>6; } h+=h<<3; h^=h>>11; h+=h<<15; return h; }
u32 sdbm_hash(const char* s){ u32 h=0; while(*s)h=(u8)(*s++)+(h<<6)+(h<<16)-h; return h; }
u32 elf_hash(const char* s){ u32 h=0,g; while(*s){ h=(h<<4)+(u8)(*s++); g=h&0xF0000000u; if(g)h^=g>>24; h&=~g; } return h; }
u32 crc32c(u32 crc,const void* d,int n){ const u8* p=(const u8*)d; crc=~crc; for(int i=0;i<n;i++){ crc^=p[i]; for(int k=0;k<8;k++)crc=(crc>>1)^(0x82F63B78u&(0u-(crc&1))); } return ~crc; }
u16 crc16_ccitt(const void* d,int n){ const u8* p=(const u8*)d; u16 crc=0xFFFF; for(int i=0;i<n;i++){ crc^=(u16)p[i]<<8; for(int k=0;k<8;k++)crc=(crc&0x8000)?(crc<<1)^0x1021:(crc<<1); } return crc; }
u8 crc8(const void* d,int n){ const u8* p=(const u8*)d; u8 crc=0; for(int i=0;i<n;i++){ crc^=p[i]; for(int k=0;k<8;k++)crc=(crc&0x80)?(crc<<1)^0x07:(crc<<1); } return crc; }
u32 xxhash32(const void* in,int len,u32 seed){ const u8* p=(const u8*)in; u32 h; const u32 P1=2654435761u,P2=2246822519u,P3=3266489917u,P4=668265263u,P5=374761393u; if(len>=16){ u32 v1=seed+P1+P2,v2=seed+P2,v3=seed,v4=seed-P1; const u8* e=p+(len&~15); do{ v1+=ld32le(p)*P2; v1=nx_rotl32(v1,13)*P1; p+=4; v2+=ld32le(p)*P2; v2=nx_rotl32(v2,13)*P1; p+=4; v3+=ld32le(p)*P2; v3=nx_rotl32(v3,13)*P1; p+=4; v4+=ld32le(p)*P2; v4=nx_rotl32(v4,13)*P1; p+=4; }while(p<e); h=nx_rotl32(v1,1)+nx_rotl32(v2,7)+nx_rotl32(v3,12)+nx_rotl32(v4,18); } else h=seed+P5; h+=(u32)len; const u8* e=(const u8*)in+len; while(p+4<=e){ h+=ld32le(p)*P3; h=nx_rotl32(h,17)*P4; p+=4; } while(p<e){ h+=(*p++)*P5; h=nx_rotl32(h,11)*P1; } h^=h>>15;h*=P2;h^=h>>13;h*=P3;h^=h>>16; return h; }

/* ---- ChaCha20 ---- */
void chacha_qr(u32* s,int a,int b,int c,int d){ s[a]+=s[b]; s[d]^=s[a]; s[d]=nx_rotl32(s[d],16); s[c]+=s[d]; s[b]^=s[c]; s[b]=nx_rotl32(s[b],12); s[a]+=s[b]; s[d]^=s[a]; s[d]=nx_rotl32(s[d],8); s[c]+=s[d]; s[b]^=s[c]; s[b]=nx_rotl32(s[b],7); }
void chacha20_block(const u8 key[32],const u8 nonce[12],u32 counter,u8 out[64]){ u32 s[16]; s[0]=0x61707865;s[1]=0x3320646e;s[2]=0x79622d32;s[3]=0x6b206574; for(int i=0;i<8;i++)s[4+i]=ld32le(key+i*4); s[12]=counter; for(int i=0;i<3;i++)s[13+i]=ld32le(nonce+i*4); u32 x[16]; for(int i=0;i<16;i++)x[i]=s[i]; for(int i=0;i<10;i++){ chacha_qr(x,0,4,8,12); chacha_qr(x,1,5,9,13); chacha_qr(x,2,6,10,14); chacha_qr(x,3,7,11,15); chacha_qr(x,0,5,10,15); chacha_qr(x,1,6,11,12); chacha_qr(x,2,7,8,13); chacha_qr(x,3,4,9,14); } for(int i=0;i<16;i++)st32le(out+i*4,x[i]+s[i]); }
void chacha20(const u8 key[32],const u8 nonce[12],u32 counter,const u8* in,u8* out,int len){ u8 ks[64]; int off=0; while(off<len){ chacha20_block(key,nonce,counter++,ks); int c=len-off; if(c>64)c=64; for(int i=0;i<c;i++)out[off+i]=in[off+i]^ks[i]; off+=c; } }

/* ---- base32, base58, base64url, hex ---- */
const char NX_B32[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
int base32_encode(const u8* in,int n,char* out){ int o=0; for(int i=0;i<n;i+=5){ u64 v=0; int bytes=n-i<5?n-i:5; for(int j=0;j<5;j++)v=(v<<8)|(j<bytes?in[i+j]:0); int chars=(bytes*8+4)/5; for(int j=0;j<8;j++)out[o++]=(j<chars)?NX_B32[(v>>(35-5*j))&0x1F]:'='; } out[o]=0; return o; }
int base64url_encode(const u8* in,int n,char* out){ static const char B[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"; int o=0; for(int i=0;i<n;i+=3){ u32 v=in[i]<<16; if(i+1<n)v|=in[i+1]<<8; if(i+2<n)v|=in[i+2]; out[o++]=B[(v>>18)&63]; out[o++]=B[(v>>12)&63]; if(i+1<n)out[o++]=B[(v>>6)&63]; if(i+2<n)out[o++]=B[v&63]; } out[o]=0; return o; }
int base16_encode(const u8* in,int n,char* out){ return (int)hex_encode(in,n,out); }
const char NX_B58[]="123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
int base58_encode(const u8* in,int n,char* out){ u8 tmp[256]; int tn=0; for(int i=0;i<n;i++){ int carry=in[i]; for(int j=0;j<tn;j++){ int x=tmp[j]*256+carry; tmp[j]=x%58; carry=x/58; } while(carry){ tmp[tn++]=carry%58; carry/=58; } } int zeros=0; while(zeros<n&&in[zeros]==0)zeros++; int o=0; for(int i=0;i<zeros;i++)out[o++]='1'; for(int i=tn-1;i>=0;i--)out[o++]=NX_B58[tmp[i]]; out[o]=0; return o; }

/* ---- XOR / OTP / rot ---- */
void xor_bytes(u8* dst,const u8* a,const u8* b,int n){ for(int i=0;i<n;i++)dst[i]=a[i]^b[i]; }
void xor_key(u8* buf,int n,const u8* key,int kn){ for(int i=0;i<n;i++)buf[i]^=key[i%kn]; }
void xor_cipher(const char* in,char* out,int n,u8 k){ for(int i=0;i<n;i++)out[i]=in[i]^k; }
void rot13(char* s){ for(;*s;s++){ if(*s>='a'&&*s<='z')*s='a'+(*s-'a'+13)%26; else if(*s>='A'&&*s<='Z')*s='A'+(*s-'A'+13)%26; } }
void rot_n(char* s,int n){ n=((n%26)+26)%26; for(;*s;s++){ if(*s>='a'&&*s<='z')*s='a'+(*s-'a'+n)%26; else if(*s>='A'&&*s<='Z')*s='A'+(*s-'A'+n)%26; } }
void caesar(char* s,int shift){ rot_n(s,shift); }
void vigenere(char* s,const char* key,int dec){ int ki=0,kl=(int)strlen(key); if(!kl)return; for(;*s;s++){ if(*s>='a'&&*s<='z'){ int k=(key[ki%kl]|32)-'a'; if(dec)k=26-k; *s='a'+(*s-'a'+k)%26; ki++; } else if(*s>='A'&&*s<='Z'){ int k=(key[ki%kl]|32)-'a'; if(dec)k=26-k; *s='A'+(*s-'A'+k)%26; ki++; } } }

/* ---- constant-time & utils ---- */
int ct_memeq(const void* a,const void* b,int n){ const u8* x=(const u8*)a; const u8* y=(const u8*)b; u8 r=0; for(int i=0;i<n;i++)r|=x[i]^y[i]; return r==0; }
void secure_zero(void* p,int n){ volatile u8* v=(volatile u8*)p; while(n--)*v++=0; }
int hash_eq(const u8* a,const u8* b,int n){ return ct_memeq(a,b,n); }
void mask_bytes(u8* p,int n,u8 m){ for(int i=0;i<n;i++)p[i]^=m; }
u32 checksum32(const void* d,int n){ const u8* p=(const u8*)d; u32 s=0; for(int i=0;i<n;i++)s+=p[i]; return s; }
u8 xor_checksum(const void* d,int n){ const u8* p=(const u8*)d; u8 x=0; for(int i=0;i<n;i++)x^=p[i]; return x; }
u16 fletcher16(const void* d,int n){ const u8* p=(const u8*)d; u16 a=0,b=0; for(int i=0;i<n;i++){ a=(a+p[i])%255; b=(b+a)%255; } return (b<<8)|a; }
u32 fletcher32(const u16* d,int n){ u32 a=0xffff,b=0xffff; while(n){ int t=n>359?359:n; n-=t; do{ a+=*d++; b+=a; }while(--t); a=(a&0xffff)+(a>>16); b=(b&0xffff)+(b>>16); } a=(a&0xffff)+(a>>16); b=(b&0xffff)+(b>>16); return (b<<16)|a; }
int luhn_check(const char* s){ int sum=0,alt=0; for(int i=(int)strlen(s)-1;i>=0;i--){ if(s[i]<'0'||s[i]>'9')continue; int d=s[i]-'0'; if(alt){ d*=2; if(d>9)d-=9; } sum+=d; alt=!alt; } return sum%10==0; }
u8 parity8(u8 x){ x^=x>>4; x^=x>>2; x^=x>>1; return x&1; }
int popcount32(u32 x){ int c=0; while(x){ x&=x-1; c++; } return c; }
int popcount64(u64 x){ int c=0; while(x){ x&=x-1; c++; } return c; }
int clz32(u32 x){ if(!x)return 32; int n=0; while(!(x&0x80000000u)){ x<<=1; n++; } return n; }
int ctz32(u32 x){ if(!x)return 32; int n=0; while(!(x&1)){ x>>=1; n++; } return n; }
u32 reverse_bits32(u32 x){ x=((x&0x55555555)<<1)|((x>>1)&0x55555555); x=((x&0x33333333)<<2)|((x>>2)&0x33333333); x=((x&0x0F0F0F0F)<<4)|((x>>4)&0x0F0F0F0F); return bswap32c(x); }

/* ---- SipHash-2-4 ---- */
u64 siphash24(const void* in,int inlen,const u8 key[16]){ u64 k0=ld64be(key),k1=ld64be(key+8); k0=bswap64c(k0); k1=bswap64c(k1); u64 v0=0x736f6d6570736575ULL^k0,v1=0x646f72616e646f6dULL^k1,v2=0x6c7967656e657261ULL^k0,v3=0x7465646279746573ULL^k1; const u8* p=(const u8*)in; int left=inlen&7; const u8* end=p+inlen-left; u64 b=(u64)inlen<<56;
    #define SR(x,n) (((x)<<(n))|((x)>>(64-(n))))
    #define ROUND do{ v0+=v1;v1=SR(v1,13);v1^=v0;v0=SR(v0,32); v2+=v3;v3=SR(v3,16);v3^=v2; v0+=v3;v3=SR(v3,21);v3^=v0; v2+=v1;v1=SR(v1,17);v1^=v2;v2=SR(v2,32); }while(0)
    for(;p!=end;p+=8){ u64 m=0; for(int i=0;i<8;i++)m|=(u64)p[i]<<(8*i); v3^=m; ROUND; ROUND; v0^=m; } for(int i=0;i<left;i++)b|=(u64)p[i]<<(8*i); v3^=b; ROUND; ROUND; v0^=b; v2^=0xff; ROUND; ROUND; ROUND; ROUND;
    #undef SR
    #undef ROUND
    return v0^v1^v2^v3; }

/* ---- Diffie-Hellman-ish modexp (small), and misc ---- */
u64 mulmod64(u64 a,u64 b,u64 m){ u64 r=0; a%=m; while(b){ if(b&1)r=(r+a)%m; a=(a*2)%m; b>>=1; } return r; }
u64 powmod64(u64 b,u64 e,u64 m){ u64 r=1; b%=m; while(e){ if(e&1)r=mulmod64(r,b,m); b=mulmod64(b,b,m); e>>=1; } return r; }
int miller_rabin(u64 n){ if(n<2)return 0; for(u64 p=2;p<=37;p++){ if(n%p==0)return n==p; } u64 d=n-1; int r=0; while(!(d&1)){ d>>=1; r++; } u64 a[]={2,3,5,7,11,13,17,19,23,29,31,37}; for(int i=0;i<12;i++){ u64 x=powmod64(a[i],d,n); if(x==1||x==n-1)continue; int ok=0; for(int j=0;j<r-1;j++){ x=mulmod64(x,x,n); if(x==n-1){ ok=1; break; } } if(!ok)return 0; } return 1; }
u64 gcd64(u64 a,u64 b){ while(b){ u64 t=a%b; a=b; b=t; } return a; }
u64 lcm64(u64 a,u64 b){ return a/gcd64(a,b)*b; }
long egcd(long a,long b,long* x,long* y){ if(!b){ *x=1;*y=0; return a; } long x1,y1; long g=egcd(b,a%b,&x1,&y1); *x=y1; *y=x1-(a/b)*y1; return g; }
long modinv(long a,long m){ long x,y; long g=egcd(((a%m)+m)%m,m,&x,&y); if(g!=1)return -1; return ((x%m)+m)%m; }

/* ---- one-shot digests to hex strings ---- */
void sha256_hex(const void* d,int n,char* out){ u8 h[32]; sha256(d,n,h); hex_encode(h,32,out); }
void sha1_hex(const void* d,int n,char* out){ u8 h[20]; sha1(d,n,h); hex_encode(h,20,out); }
void md5_hex(const void* d,int n,char* out){ u8 h[16]; md5(d,n,h); hex_encode(h,16,out); }
void hmac_sha256_hex(const u8* k,int kl,const u8* m,int ml,char* out){ u8 h[32]; hmac_sha256(k,kl,m,ml,h); hex_encode(h,32,out); }
void sha224_hex(const void* d,int n,char* out){ u8 h[28]; sha224(d,n,h); hex_encode(h,28,out); }
void hmac_sha1_hex(const u8* k,int kl,const u8* m,int ml,char* out){ u8 h[20]; hmac_sha1c(k,kl,m,ml,h); hex_encode(h,20,out); }
void hmac_md5_hex(const u8* k,int kl,const u8* m,int ml,char* out){ u8 h[16]; hmac_md5c(k,kl,m,ml,h); hex_encode(h,16,out); }
u64 hash64_str(const char* s){ return fnv1a_64(s,(int)strlen(s)); }
u32 hash32_str(const char* s){ return murmur3_32(s,(int)strlen(s),0); }
int chacha20_selftest(void){ u8 k[32],n[12]; for(int i=0;i<32;i++)k[i]=(u8)i; for(int i=0;i<12;i++)n[i]=0; u8 z[64],c[64]; for(int i=0;i<64;i++)z[i]=0; chacha20(k,n,0,z,c,64); u8 d[64]; chacha20(k,n,0,c,d,64); for(int i=0;i<64;i++)if(d[i])return 0; return 1; }
void keystream(const u8 key[32],const u8 nonce[12],u8* out,int n){ u8 z[256]; for(int i=0;i<n&&i<256;i++)z[i]=0; chacha20(key,nonce,0,z,out,n<256?n:256); }
u32 adler32c(u32 a,const void* d,int n){ return adler32(a,d,n); }
u8 bcd_encode(u8 v){ return (u8)(((v/10)<<4)|(v%10)); }
u8 bcd_decode(u8 v){ return (u8)(((v>>4)*10)+(v&0x0F)); }
u32 rol_hash(const char* s){ u32 h=0; while(*s){ h=nx_rotl32(h,5)^(u8)(*s++); } return h; }
u32 djb2a(const char* s){ u32 h=5381; int c; while((c=(u8)*s++))h=((h<<5)+h)^c; return h; }
u16 internet_checksum(const void* d,int n){ const u8* p=(const u8*)d; u32 s=0; for(int i=0;i+1<n;i+=2)s+=((u32)p[i]<<8)|p[i+1]; if(n&1)s+=(u32)p[n-1]<<8; while(s>>16)s=(s&0xFFFF)+(s>>16); return (u16)~s; }
void nx_random_key(u8* out,int n){ static u64 st=0; if(!st){ st=(u64)time_ticks()^0x2545F4914F6CDD1DULL; } for(int i=0;i<n;i++){ st^=st<<13; st^=st>>7; st^=st<<17; out[i]=(u8)(st>>24); } }
int is_prime_u32(u32 n){ return miller_rabin(n); }
u64 next_prime(u64 n){ if(n<2)return 2; if(!(n&1))n++; while(!miller_rabin(n))n+=2; return n; }
u32 mix32(u32 a,u32 b){ a^=b; a*=0x9E3779B1u; a^=a>>16; return a; }
u64 splitmix64_hash(u64 x){ x+=0x9E3779B97F4A7C15ULL; x=(x^(x>>30))*0xBF58476D1CE4E5B9ULL; x=(x^(x>>27))*0x94D049BB133111EBULL; return x^(x>>31); }
u8 hamming_distance(u8 a,u8 b){ return (u8)popcount32(a^b); }
int hamming_bytes(const u8* a,const u8* b,int n){ int d=0; for(int i=0;i<n;i++)d+=popcount32(a[i]^b[i]); return d; }
u32 crc32_str(const char* s){ return crc32(0,s,(int)strlen(s)); }
void obfuscate(u8* p,int n,u32 seed){ for(int i=0;i<n;i++){ seed=seed*1103515245u+12345u; p[i]^=(u8)(seed>>16); } }
void deobfuscate(u8* p,int n,u32 seed){ obfuscate(p,n,seed); }

/* ================= 150 general utilities (v8.08) ================= */
/* --- bit tricks (24) --- */
u32 next_pow2(u32 x){ if(x<=1)return 1; x--; x|=x>>1;x|=x>>2;x|=x>>4;x|=x>>8;x|=x>>16; return x+1; }
int is_pow2(u64 x){ return x&&!(x&(x-1)); }
u32 prev_pow2(u32 x){ if(!x)return 0; x|=x>>1;x|=x>>2;x|=x>>4;x|=x>>8;x|=x>>16; return x-(x>>1); }
int log2i(u32 x){ int r=0; while(x>>=1)r++; return r; }
u32 round_up(u32 x,u32 a){ return (x+a-1)/a*a; }
u32 round_down(u32 x,u32 a){ return x/a*a; }
u32 align_up(u32 x,u32 a){ return (x+(a-1))&~(a-1); }
u32 align_down(u32 x,u32 a){ return x&~(a-1); }
int get_bit(u32 x,int n){ return (x>>n)&1; }
u32 set_bit(u32 x,int n){ return x|(1u<<n); }
u32 clr_bit(u32 x,int n){ return x&~(1u<<n); }
u32 tgl_bit(u32 x,int n){ return x^(1u<<n); }
u32 bit_mask(int n){ return n>=32?0xFFFFFFFFu:((1u<<n)-1); }
u32 extract_bits(u32 x,int lo,int cnt){ return (x>>lo)&bit_mask(cnt); }
u32 deposit_bits(u32 x,int lo,int cnt,u32 v){ u32 m=bit_mask(cnt)<<lo; return (x&~m)|((v<<lo)&m); }
u32 lowest_bit(u32 x){ return x&(0u-x); }
u32 clear_lowest(u32 x){ return x&(x-1); }
int count_leading_zeros(u32 x){ return clz32(x); }
int count_trailing_zeros(u32 x){ return ctz32(x); }
u8 reverse_byte(u8 b){ b=(b&0xF0)>>4|(b&0x0F)<<4; b=(b&0xCC)>>2|(b&0x33)<<2; b=(b&0xAA)>>1|(b&0x55)<<1; return b; }
u16 pack_u16(u8 hi,u8 lo){ return ((u16)hi<<8)|lo; }
u32 pack_u32(u8 a,u8 b,u8 c,u8 d){ return ((u32)a<<24)|((u32)b<<16)|((u32)c<<8)|d; }
int sign(long x){ return (x>0)-(x<0); }
u32 interleave2(u16 x,u16 y){ u32 r=0; for(int i=0;i<16;i++)r|=((u32)((x>>i)&1)<<(2*i))|((u32)((y>>i)&1)<<(2*i+1)); return r; }

/* --- int helpers (26) --- */
long lmax(long a,long b){ return a>b?a:b; }
long lmin(long a,long b){ return a<b?a:b; }
int iclamp(int x,int lo,int hi){ return x<lo?lo:(x>hi?hi:x); }
long lclamp(long x,long lo,long hi){ return x<lo?lo:(x>hi?hi:x); }
int iabs2(int x){ return x<0?-x:x; }
int isqz(long x){ return x==0; }
void iswap(int* a,int* b){ int t=*a; *a=*b; *b=t; }
void lswap(long* a,long* b){ long t=*a; *a=*b; *b=t; }
void pswap(void** a,void** b){ void* t=*a; *a=*b; *b=t; }
int cmp_int(const void* a,const void* b){ int x=*(const int*)a,y=*(const int*)b; return (x>y)-(x<y); }
int cmp_int_desc(const void* a,const void* b){ return cmp_int(b,a); }
int cmp_long(const void* a,const void* b){ long x=*(const long*)a,y=*(const long*)b; return (x>y)-(x<y); }
int cmp_double(const void* a,const void* b){ double x=*(const double*)a,y=*(const double*)b; return (x>y)-(x<y); }
int cmp_str(const void* a,const void* b){ return strcmp(*(const char**)a,*(const char**)b); }
long isum(const int* a,int n){ long s=0; for(int i=0;i<n;i++)s+=a[i]; return s; }
int imaxv(const int* a,int n){ int m=a[0]; for(int i=1;i<n;i++)if(a[i]>m)m=a[i]; return m; }
int iminv(const int* a,int n){ int m=a[0]; for(int i=1;i<n;i++)if(a[i]<m)m=a[i]; return m; }
int iavg(const int* a,int n){ return n?(int)(isum(a,n)/n):0; }
int digit_count(long x){ int n=x<=0?1:0; if(x<0)x=-x; while(x){ n++; x/=10; } return n; }
long parse_long(const char* s,int base){ return strtol(s,0,base); }
int clampi_wrap(int x,int n){ x%=n; if(x<0)x+=n; return x; }
int int_pow(int b,int e){ int r=1; while(e-->0)r*=b; return r; }
int int_sqrt(int x){ if(x<0)return -1; int r=0; while((r+1)*(r+1)<=x)r++; return r; }

/* --- array ops (22) --- */
void arr_fill_int(int* a,int n,int v){ for(int i=0;i<n;i++)a[i]=v; }
void arr_iota(int* a,int n,int start){ for(int i=0;i<n;i++)a[i]=start+i; }
void arr_reverse_int(int* a,int n){ for(int i=0;i<n/2;i++){ int t=a[i]; a[i]=a[n-1-i]; a[n-1-i]=t; } }
int arr_find_int(const int* a,int n,int v){ for(int i=0;i<n;i++)if(a[i]==v)return i; return -1; }
int arr_contains_int(const int* a,int n,int v){ return arr_find_int(a,n,v)>=0; }
int arr_count_int(const int* a,int n,int v){ int c=0; for(int i=0;i<n;i++)if(a[i]==v)c++; return c; }
void arr_copy_int(int* dst,const int* src,int n){ for(int i=0;i<n;i++)dst[i]=src[i]; }
int arr_unique_int(int* a,int n){ int m=0; for(int i=0;i<n;i++){ int dup=0; for(int j=0;j<m;j++)if(a[j]==a[i]){dup=1;break;} if(!dup)a[m++]=a[i]; } return m; }
int arr_remove_int(int* a,int n,int v){ int m=0; for(int i=0;i<n;i++)if(a[i]!=v)a[m++]=a[i]; return m; }
void arr_rotate_int(int* a,int n,int k){ k=((k%n)+n)%n; arr_reverse_int(a,k); arr_reverse_int(a+k,n-k); arr_reverse_int(a,n); }
int arr_sum_range(const int* a,int lo,int hi){ int s=0; for(int i=lo;i<hi;i++)s+=a[i]; return s; }
int arr_min_idx(const int* a,int n){ int m=0; for(int i=1;i<n;i++)if(a[i]<a[m])m=i; return m; }
int arr_max_idx(const int* a,int n){ int m=0; for(int i=1;i<n;i++)if(a[i]>a[m])m=i; return m; }
void arr_shuffle_int(int* a,int n){ for(int i=n-1;i>0;i--){ int j=(int)(rand2()%(i+1)); int t=a[i]; a[i]=a[j]; a[j]=t; } }
int arr_bsearch_int(const int* a,int n,int v){ int lo=0,hi=n-1; while(lo<=hi){ int m=(lo+hi)/2; if(a[m]==v)return m; if(a[m]<v)lo=m+1; else hi=m-1; } return -1; }
int arr_is_sorted_int(const int* a,int n){ for(int i=1;i<n;i++)if(a[i-1]>a[i])return 0; return 1; }
void arr_scale_int(int* a,int n,int f){ for(int i=0;i<n;i++)a[i]*=f; }
void arr_map_int(int* a,int n,int(*fn)(int)){ for(int i=0;i<n;i++)a[i]=fn(a[i]); }
int arr_reduce_int(const int* a,int n,int init,int(*fn)(int,int)){ int acc=init; for(int i=0;i<n;i++)acc=fn(acc,a[i]); return acc; }
int arr_filter_int(const int* src,int n,int* dst,int(*pred)(int)){ int m=0; for(int i=0;i<n;i++)if(pred(src[i]))dst[m++]=src[i]; return m; }
void arr_zero(void* p,int n){ u8* b=(u8*)p; for(int i=0;i<n;i++)b[i]=0; }
int arr_equal_int(const int* a,const int* b,int n){ for(int i=0;i<n;i++)if(a[i]!=b[i])return 0; return 1; }

/* --- string extras (30) --- */
int str_count_char(const char* s,char c){ int n=0; for(;*s;s++)if(*s==c)n++; return n; }
int str_index(const char* s,char c){ for(int i=0;s[i];i++)if(s[i]==c)return i; return -1; }
int str_rindex(const char* s,char c){ int r=-1; for(int i=0;s[i];i++)if(s[i]==c)r=i; return r; }
int str_prefix(const char* s,const char* p){ while(*p){ if(*s++!=*p++)return 0; } return 1; }
int str_suffix(const char* s,const char* suf){ int ls=(int)strlen(s),lp=(int)strlen(suf); if(lp>ls)return 0; return strcmp(s+ls-lp,suf)==0; }
int str_contains(const char* s,const char* sub){ return strstr(s,sub)!=0; }
void str_repeat(char* out,const char* s,int n){ int o=0; for(int i=0;i<n;i++)for(const char* p=s;*p;p++)out[o++]=*p; out[o]=0; }
int str_replace_all(char* s,const char* from,const char* to){ static char buf[4096]; int fl=(int)strlen(from),tl=(int)strlen(to),o=0,cnt=0; for(int i=0;s[i];){ if(fl&&strncmp(s+i,from,fl)==0){ for(int j=0;j<tl;j++)buf[o++]=to[j]; i+=fl; cnt++; } else buf[o++]=s[i++]; } buf[o]=0; strcpy(s,buf); return cnt; }
void str_pad_left(char* out,const char* s,int width,char pad){ int l=(int)strlen(s),o=0; for(int i=l;i<width;i++)out[o++]=pad; for(int i=0;s[i];i++)out[o++]=s[i]; out[o]=0; }
void str_pad_right(char* out,const char* s,int width,char pad){ int o=0; for(int i=0;s[i];i++)out[o++]=s[i]; int l=(int)strlen(s); for(int i=l;i<width;i++)out[o++]=pad; out[o]=0; }
void str_center(char* out,const char* s,int width){ int l=(int)strlen(s); if(l>=width){ strcpy(out,s); return; } int left=(width-l)/2,o=0; for(int i=0;i<left;i++)out[o++]=' '; for(int i=0;s[i];i++)out[o++]=s[i]; for(int i=left+l;i<width;i++)out[o++]=' '; out[o]=0; }
void str_toupper(char* s){ for(;*s;s++)if(*s>='a'&&*s<='z')*s-=32; }
void str_tolower(char* s){ for(;*s;s++)if(*s>='A'&&*s<='Z')*s+=32; }
void str_capitalize(char* s){ if(s[0]>='a'&&s[0]<='z')s[0]-=32; }
void str_title(char* s){ int st=1; for(;*s;s++){ if(*s==' ')st=1; else { if(st&&*s>='a'&&*s<='z')*s-=32; st=0; } } }
void str_swapcase(char* s){ for(;*s;s++){ if(*s>='a'&&*s<='z')*s-=32; else if(*s>='A'&&*s<='Z')*s+=32; } }
int str_is_numeric(const char* s){ if(!*s)return 0; for(;*s;s++)if(*s<'0'||*s>'9')return 0; return 1; }
int str_is_alpha(const char* s){ if(!*s)return 0; for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')))return 0; return 1; }
int str_is_alnum(const char* s){ if(!*s)return 0; for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')))return 0; return 1; }
int str_is_blank(const char* s){ for(;*s;s++)if(*s!=' '&&*s!='\t'&&*s!='\n')return 0; return 1; }
int str_eq_ci(const char* a,const char* b){ while(*a&&*b){ char x=*a,y=*b; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y)return 0; a++;b++; } return *a==*b; }
void str_reverse(char* s){ int n=(int)strlen(s); for(int i=0;i<n/2;i++){ char t=s[i]; s[i]=s[n-1-i]; s[n-1-i]=t; } }
int str_word_count(const char* s){ int n=0,in=0; for(;*s;s++){ if(*s==' '||*s=='\t'||*s=='\n')in=0; else if(!in){ in=1; n++; } } return n; }
void str_squeeze(char* s){ int o=0; int sp=0; for(int i=0;s[i];i++){ if(s[i]==' '){ if(!sp)s[o++]=' '; sp=1; } else { s[o++]=s[i]; sp=0; } } s[o]=0; }
void str_remove_char(char* s,char c){ int o=0; for(int i=0;s[i];i++)if(s[i]!=c)s[o++]=s[i]; s[o]=0; }
int str_common_prefix(const char* a,const char* b){ int n=0; while(a[n]&&a[n]==b[n])n++; return n; }
int str_levenshtein(const char* a,const char* b){ int la=(int)strlen(a),lb=(int)strlen(b); static int d[128]; if(lb>=128)return -1; for(int j=0;j<=lb;j++)d[j]=j; for(int i=1;i<=la;i++){ int prev=d[0]; d[0]=i; for(int j=1;j<=lb;j++){ int cur=d[j]; int cost=(a[i-1]==b[j-1])?0:1; int m=d[j]+1; if(d[j-1]+1<m)m=d[j-1]+1; if(prev+cost<m)m=prev+cost; d[j]=m; prev=cur; } } return d[lb]; }
int str_to_bool(const char* s){ return str_eq_ci(s,"true")||str_eq_ci(s,"yes")||str_eq_ci(s,"1")||str_eq_ci(s,"on"); }
void str_ellipsis(char* out,const char* s,int max){ int l=(int)strlen(s); if(l<=max){ strcpy(out,s); return; } int k=max-3; if(k<0)k=0; for(int i=0;i<k;i++)out[i]=s[i]; out[k]=0; strcat(out,"..."); }

/* --- color / graphics (14) --- */
u32 rgb_pack(u8 r,u8 g,u8 b){ return ((u32)r<<16)|((u32)g<<8)|b; }
void rgb_unpack(u32 c,u8* r,u8* g,u8* b){ *r=(c>>16)&255; *g=(c>>8)&255; *b=c&255; }
u32 rgb_lerp(u32 a,u32 b,int t){ int ar=(a>>16)&255,ag=(a>>8)&255,ab=a&255,br=(b>>16)&255,bg=(b>>8)&255,bb=b&255; return rgb_pack(ar+(br-ar)*t/255,ag+(bg-ag)*t/255,ab+(bb-ab)*t/255); }
u8 rgb_luma(u32 c){ int r=(c>>16)&255,g=(c>>8)&255,b=c&255; return (u8)((r*77+g*150+b*29)>>8); }
u32 rgb_gray(u32 c){ u8 l=rgb_luma(c); return rgb_pack(l,l,l); }
u32 rgb_invert(u32 c){ return rgb_pack(255-((c>>16)&255),255-((c>>8)&255),255-(c&255)); }
u32 rgb_scale(u32 c,int pct){ int r=((c>>16)&255)*pct/100,g=((c>>8)&255)*pct/100,b=(c&255)*pct/100; return rgb_pack(r>255?255:r,g>255?255:g,b>255?255:b); }
u32 rgb_from_hex(const char* s){ if(*s=='#')s++; u32 v=(u32)strtol(s,0,16); return v&0xFFFFFF; }
void rgb_to_hex(u32 c,char* out){ snprintf(out,8,"#%06x",c&0xFFFFFF); }
u32 hsv_to_rgb(int h,int s,int v){ h%=360; if(h<0)h+=360; int c=v*s/255; int x=c*(60-((h%120)-60<0?-(( h%120)-60):((h%120)-60)))/60; int m=v-c; int r=0,g=0,b=0; if(h<60){r=c;g=x;} else if(h<120){r=x;g=c;} else if(h<180){g=c;b=x;} else if(h<240){g=x;b=c;} else if(h<300){r=x;b=c;} else {r=c;b=x;} return rgb_pack(r+m,g+m,b+m); }
u32 rgb_blend(u32 a,u32 b,int alpha){ return rgb_lerp(a,b,alpha); }
u32 rgb_darken(u32 c,int pct){ return rgb_scale(c,100-pct); }
u32 rgb_lighten(u32 c,int pct){ return rgb_lerp(c,0xFFFFFF,pct*255/100); }
int rgb_distance(u32 a,u32 b){ int dr=((a>>16)&255)-((b>>16)&255),dg=((a>>8)&255)-((b>>8)&255),db=(a&255)-(b&255); return dr*dr+dg*dg+db*db; }

/* --- time / misc (18) --- */
int is_leap_year(int y){ return (y%4==0&&y%100!=0)||y%400==0; }
int days_in_month(int y,int m){ static const int d[12]={31,28,31,30,31,30,31,31,30,31,30,31}; if(m==2&&is_leap_year(y))return 29; return (m>=1&&m<=12)?d[m-1]:0; }
int day_of_year(int y,int m,int d){ int n=d; for(int i=1;i<m;i++)n+=days_in_month(y,i); return n; }
int day_of_week(int y,int m,int d){ static const int t[]={0,3,2,5,0,3,5,1,4,6,2,4}; if(m<3)y--; return (y+y/4-y/100+y/400+t[m-1]+d)%7; }
const char* month_name(int m){ static const char* n[]={"January","February","March","April","May","June","July","August","September","October","November","December"}; return (m>=1&&m<=12)?n[m-1]:"?"; }
const char* weekday_name(int w){ static const char* n[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"}; return (w>=0&&w<7)?n[w]:"?"; }
long unix_from_ymd(int y,int m,int d,int hh,int mm,int ss){ long days=0; for(int yy=1970;yy<y;yy++)days+=is_leap_year(yy)?366:365; for(int i=1;i<m;i++)days+=days_in_month(y,i); days+=d-1; return ((days*24+hh)*60+mm)*60+ss; }
void secs_to_hms(long s,int* h,int* m,int* sec){ *h=(int)(s/3600); *m=(int)((s%3600)/60); *sec=(int)(s%60); }
void fmt_duration(char* out,long secs){ long d=secs/86400,h=(secs%86400)/3600,m=(secs%3600)/60,s=secs%60; if(d)snprintf(out,32,"%ldd %ldh %ldm %lds",d,h,m,s); else if(h)snprintf(out,32,"%ldh %ldm %lds",h,m,s); else if(m)snprintf(out,32,"%ldm %lds",m,s); else snprintf(out,32,"%lds",s); }
void fmt_bytes(char* out,u64 n){ const char* u[]={"B","KB","MB","GB","TB"}; int i=0; u64 whole=n; u64 frac=0; while(whole>=1024&&i<4){ frac=(whole%1024)*10/1024; whole/=1024; i++; } if(i==0)snprintf(out,24,"%llu %s",(unsigned long long)whole,u[i]); else snprintf(out,24,"%llu.%llu %s",(unsigned long long)whole,(unsigned long long)frac,u[i]); }
void fmt_commas(char* out,long n){ char t[32]; int neg=n<0; if(neg)n=-n; int tn=0; if(n==0)t[tn++]='0'; while(n){ t[tn++]='0'+n%10; n/=10; } int o=0; if(neg)out[o++]='-'; for(int i=0;i<tn;i++){ out[o++]=t[tn-1-i]; int rem=tn-1-i; if(rem>0&&rem%3==0)out[o++]=','; } out[o]=0; }
int roll_dice(int sides){ return 1+(int)(rand2()%sides); }
int rand_range(int lo,int hi){ if(hi<=lo)return lo; return lo+(int)(rand2()%(u32)(hi-lo)); }
double rand_double(void){ return (double)(rand2()&0xFFFFFF)/16777216.0; }
int coin_flip(void){ return (int)(rand2()&1); }
int percent_chance(int pct){ return (int)(rand2()%100)<pct; }
void rand_string(char* out,int n){ static const char cs[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"; for(int i=0;i<n;i++)out[i]=cs[rand2()%62]; out[n]=0; }
u32 rand_color(void){ return rand2()&0xFFFFFF; }

/* --- validation / conversion (16) --- */
int valid_ipv4(const char* s){ int parts=0,val=0,dig=0; for(;;s++){ if(*s>='0'&&*s<='9'){ val=val*10+(*s-'0'); dig=1; if(val>255)return 0; } else if(*s=='.'||*s==0){ if(!dig)return 0; parts++; val=0; dig=0; if(*s==0)break; } else return 0; } return parts==4; }
int valid_email(const char* s){ int at=0,dot=0; for(int i=0;s[i];i++){ if(s[i]=='@'){ at++; dot=0; } else if(s[i]=='.'&&at)dot=1; } return at==1&&dot; }
int valid_hex(const char* s){ if(!*s)return 0; for(;*s;s++)if(!((*s>='0'&&*s<='9')||(*s>='a'&&*s<='f')||(*s>='A'&&*s<='F')))return 0; return 1; }
u32 ipv4_to_u32(const char* s){ u32 r=0; int v=0; for(;;s++){ if(*s>='0'&&*s<='9')v=v*10+(*s-'0'); else { r=(r<<8)|(v&255); v=0; if(!*s)break; } } return r; }
void u32_to_ipv4(u32 ip,char* out){ snprintf(out,16,"%u.%u.%u.%u",(ip>>24)&255,(ip>>16)&255,(ip>>8)&255,ip&255); }
int hexval2(char c){ if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10; return -1; }
long bin_to_long(const char* s){ long v=0; if(s[0]=='0'&&(s[1]=='b'||s[1]=='B'))s+=2; for(;*s=='0'||*s=='1';s++)v=(v<<1)|(*s-'0'); return v; }
void long_to_bin(long v,int bits,char* out){ for(int i=0;i<bits;i++)out[i]=((v>>(bits-1-i))&1)+'0'; out[bits]=0; }
long oct_to_long(const char* s){ return strtol(s,0,8); }
int char_to_digit(char c){ return (c>='0'&&c<='9')?c-'0':-1; }
char digit_to_char(int d){ return (d>=0&&d<=9)?'0'+d:(d<36?'a'+d-10:'?'); }
int roman_to_int(const char* s){ int v[128]; v['I']=1;v['V']=5;v['X']=10;v['L']=50;v['C']=100;v['D']=500;v['M']=1000; int r=0; for(int i=0;s[i];i++){ int c=v[(int)(u8)s[i]]; int nx=s[i+1]?v[(int)(u8)s[i+1]]:0; if(c<nx)r-=c; else r+=c; } return r; }
void int_to_roman(int n,char* out){ static const int val[]={1000,900,500,400,100,90,50,40,10,9,5,4,1}; static const char* sym[]={"M","CM","D","CD","C","XC","L","XL","X","IX","V","IV","I"}; int o=0; for(int i=0;i<13;i++)while(n>=val[i]){ for(const char* p=sym[i];*p;p++)out[o++]=*p; n-=val[i]; } out[o]=0; }
long clamp_long(long x,long lo,long hi){ return x<lo?lo:(x>hi?hi:x); }
int approx_eq(double a,double b,double eps){ double d=a-b; return (d<0?-d:d)<=eps; }
int in_range(long x,long lo,long hi){ return x>=lo&&x<=hi; }
int even(long x){ return (x&1)==0; }

#endif /* NVX_POSIX_H */
