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

extern /* ---- errno ---- */
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
int open(const char* path,int flags,...);
ssize_t read(int fd,void* buf,size_t n);
ssize_t write(int fd,const void* buf,size_t n);
int close(int fd);
off_t lseek(int fd,off_t off,int whence);
int dup(int fd);
int dup2(int o,int n);
int pipe(int fds[2]);
#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define O_CLOEXEC 0x80000
#define O_NONBLOCK 0x800
int fcntl(int fd,int cmd,...);
mode_t umask(mode_t m);
pid_t setpgid(pid_t pid,pid_t pg);
pid_t getpgid(pid_t pid);
pid_t getpgrp(void);
pid_t setsid(void);
pid_t getsid(pid_t pid);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd,pid_t pg);
pid_t getpid(void);
void _exit(int code);
void __flush_all(void);
void exit(int code);

/* ================= process control ================= */
pid_t fork(void);
/* execv-like: NoovexOS exec takes a path; args are advisory (single-arg model) */
int execv(const char* path,char* const argv[]);
int execve(const char* path,char* const argv[],char* const envp[]);
int execvp(const char* file,char* const argv[]);
int execl(const char* path,const char* arg0,...);
int execlp(const char* file,const char* arg0,...);
/* spawn = fork+exec in one call (NoovexOS extension), returns pid */
pid_t posix_spawnp(const char* path);
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
pid_t waitpid(pid_t pid,int* status,int flags);
pid_t wait(int* status);

/* ================= memory ================= */
void* sbrk(long inc);
/* a simple bump+free-list malloc */
typedef struct blk { size_t sz; struct blk* next; } blk_t;
extern blk_t* g_free;
void* malloc(size_t n);
void free(void* ptr);
void* calloc(size_t a,size_t b);
void* realloc(void* ptr,size_t n);

/* ================= string / mem ================= */
size_t strlen(const char* s);
int strcmp(const char* a,const char* b);
int strncmp(const char* a,const char* b,size_t n);
char* strcpy(char* d,const char* s);
char* strncpy(char* d,const char* s,size_t n);
char* strcat(char* d,const char* s);
char* strchr(const char* s,int c);
void* memcpy(void* d,const void* s,size_t n);
void* memset(void* d,int c,size_t n);
int memcmp(const void* a,const void* b,size_t n);
int atoi(const char* s);

/* ================= stdio (printf family, on write) ================= */
void __putc(int fd,char c);
void __puts_fd(int fd,const char* s);
void __putnum(int fd,long v,int base,int isu);
int vdprintf_impl(int fd,const char* fmt,__builtin_va_list ap);
int dprintf(int fd,const char* fmt,...);
int __stdout_vprintf(const char* fmt,__builtin_va_list ap); long __stdout_write(const char* d,size_t n);
int printf(const char* fmt,...);
int puts(const char* s);
int putchar(int c);

/* ================= more string / stdlib / ctype ================= */
char* strstr(const char* h,const char* n);
char* strdup(const char* s);
long strtol(const char* s,char** end,int base);
int isdigit(int c);
int isspace(int c);
int isalpha(int c);
int toupper(int c);
int tolower(int c);
int abs(int x);
char* strtok(char* s,const char* delim);
void qsort(void* base,size_t n,size_t sz,int(*cmp)(const void*,const void*));
extern char __env_buf[128];
char* getenv(const char* name);
int setenv(const char* name,const char* val,int overwrite);
int putenv(char* nv);
int unsetenv(const char* name);
extern int getcwd_helper;

/* ================= misc ================= */
void sleep_ms(unsigned ms);
unsigned long time_ticks(void);   /* monotonic ms since boot */

/* ================= stdio: FILE* streams ================= */
typedef struct { int fd; int eof; int err; int unget; char* buf; int bsize; int bpos; int bmode; int bown; char* tmpname; } FILE;
extern FILE __stdin, __stdout, __stderr;
#define stdin  (&__stdin)
#define stdout (&__stdout)
#define stderr (&__stderr)
#define EOF (-1)
#define BUFSIZ 1024
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
int unlink(const char* path); int vsnprintf(char* out,size_t cap,const char* fmt,__builtin_va_list ap);
extern FILE* __ffiles[16];
void __freg(FILE* f);
void __funreg(FILE* f);
int __fflush_buf(FILE* f);
long __fwrite_raw(FILE* f,const char* d,size_t n);
void __flush_all(void);
void __finit(FILE* f,int fd);
long __stdout_write(const char* d,size_t n);

FILE* fopen(const char* path,const char* mode);
int fclose(FILE* f);
size_t fread(void* buf,size_t sz,size_t n,FILE* f);
size_t fwrite(const void* buf,size_t sz,size_t n,FILE* f);
int fgetc(FILE* f);
int getchar(void);
int ungetc(int c,FILE* f);
char* fgets(char* buf,int n,FILE* f);
int fputc(int c,FILE* f);
int feof(FILE* f);
int ferror(FILE* f);
int fflush(FILE* f);
/* vfprintf/fprintf onto a FILE* */
int vfprintf_f(FILE* f,const char* fmt,__builtin_va_list ap);
int __stdout_vprintf(const char* fmt,__builtin_va_list ap);
int fprintf(FILE* f,const char* fmt,...);
int fputs(const char* s,FILE* f);

/* ================= snprintf / sprintf (format into a buffer) ================= */
typedef struct { char* buf; int cap; int len; } __sbuf;
void __sb_putc(__sbuf* sb,char c);
void __sb_puts(__sbuf* sb,const char* s);
void __sb_num(__sbuf* sb,long v,int base,int isu);
int vsnprintf(char* out,size_t cap,const char* fmt,__builtin_va_list ap);
int snprintf(char* out,size_t cap,const char* fmt,...);
int sprintf(char* out,const char* fmt,...);

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
extern void __nvx_sigtramp(void);
int sigemptyset(sigset_t* s);
int sigfillset(sigset_t* s);
int sigaddset(sigset_t* s,int n);
int sigdelset(sigset_t* s,int n);
int sigismember(const sigset_t* s,int n);
int sigaction(int sig,const struct sigaction* act,struct sigaction* old);
sighandler_t signal(int sig,sighandler_t h);
int sigprocmask(int how,const sigset_t* set,sigset_t* old);
int kill(pid_t pid,int sig);
int raise(int sig);
int pause(void);
int killpg(pid_t pg,int sig);

/* ================= unistd extras ================= */
char* getcwd(char* buf,size_t n);   /* syscall 18 */
int chdir(const char* path);                                     /* syscall 19 */
int unlink(const char* path);                                    /* syscall 38 */
int mkdir(const char* path,mode_t m);                    /* syscall 21 */
unsigned sleep(unsigned s);

/* ================= BSD-style sockets (TCP) ================= */
#define AF_INET     2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
typedef unsigned int in_addr_t;
typedef unsigned short in_port_t;
struct in_addr { in_addr_t s_addr; };
struct sockaddr_in { unsigned short sin_family; in_port_t sin_port; struct in_addr sin_addr; char pad[8]; };
struct sockaddr { unsigned short sa_family; char sa_data[14]; };

unsigned short htons(unsigned short x);
unsigned short ntohs(unsigned short x);
unsigned int htonl(unsigned int x);
unsigned int ntohl(unsigned int x);

int socket(int dom,int type,int proto);
/* resolve a hostname (or dotted IP) to a big-endian IP; returns 0 on failure */
unsigned int resolve(const char* host);
int connect(int sid,const struct sockaddr* addr,unsigned len);
/* convenience: connect straight to host:port */
int connect_host(int sid,const char* host,unsigned short port);
ssize_t send(int sid,const void* buf,size_t n,int flags);
ssize_t recv(int sid,void* buf,size_t n,int flags);
int closesocket(int sid);
/* inet_addr: dotted string -> big-endian (via resolve, which parses dotted too) */
in_addr_t inet_addr(const char* s);

/* ================= (1) scanf / sscanf ================= */
const char* __scan_num(const char* p,long* out,int base);
int vsscanf_impl(const char* str,const char* fmt,__builtin_va_list ap);
int sscanf(const char* str,const char* fmt,...);
int scanf(const char* fmt,...);
int fscanf(FILE* f,const char* fmt,...);

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
double fabs(double x);
double floor(double x);
double ceil(double x);
double sqrt(double x);
double pow(double b,double e);
double sin(double x);
double cos(double x);
double fmod(double a,double b);
double round(double x);

/* ================= (4) time.h ================= */
typedef long time_t; typedef long clock_t;
#define CLOCKS_PER_SEC 1000
unsigned long __realtime_ms(void);   /* RTC-backed wall clock */
time_t time(time_t* t);
clock_t clock(void);

/* ================= (5) dirent: opendir/readdir/closedir ================= */
struct dirent { char d_name[64]; unsigned char d_type; unsigned long d_ino; };
#define DT_REG 8
#define DT_DIR 4
typedef struct { int idx; struct dirent ent; char path[128]; } DIR;
extern DIR __dirs[4]; extern int __dir_used[4];
DIR* opendir(const char* path);
struct dirent* readdir(DIR* d);
int closedir(DIR* d);

/* ================= 15 more standard libc functions ================= */
/* 1. strrchr - last occurrence */
char* strrchr(const char* s,int c);
/* 2. strncat */
char* strncat(char* d,const char* s,size_t n);
/* 3. memmove - handles overlap */
void* memmove(void* d,const void* s,size_t n);
/* 4. memchr */
void* memchr(const void* p,int c,size_t n);
/* 5. strspn / 6. strcspn / 7. strpbrk */
size_t strspn(const char* s,const char* set);
size_t strcspn(const char* s,const char* set);
char* strpbrk(const char* s,const char* set);
/* 8. strncasecmp */
int strncasecmp(const char* a,const char* b,size_t n);
/* 9. atol / 10. atof */
long atol(const char* s);
double atof(const char* s);
extern /* 11. rand / srand (LCG - fine for non-crypto userspace) */
unsigned long __rng;
void srand(unsigned seed);
#define RAND_MAX 0x7fffffff
int rand2(void);
/* 12. labs / 13. div */
long labs(long x);
typedef struct { int quot,rem; } div_t;
div_t div(int a,int b);
/* 14. bsearch */
void* bsearch(const void* key,const void* base,size_t n,size_t sz,int(*cmp)(const void*,const void*));
/* 15. the rest of ctype */
int isupper(int c);
int islower(int c);
int isalnum(int c);
int isxdigit(int c);
int isprint(int c);
int ispunct(int c);
int iscntrl(int c);

/* ================= 25 more standard libc functions ================= */
/* --- math.h additions --- */
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double tan(double x);
double atan(double x);
double atan2(double y,double x);
double asin(double x);
double acos(double x);
double trunc(double x);
double copysign(double x,double y);
double hypot(double a,double b);
double fmax(double a,double b);
double fmin(double a,double b);
/* --- string additions --- */
int strcasecmp(const char* a,const char* b);
size_t strnlen(const char* s,size_t m);
char* stpcpy(char* d,const char* s);
void* memrchr(const void* p,int c,size_t n);
char* strncdup(const char* s,size_t n);
/* --- stdlib additions --- */
unsigned long strtoul(const char* s,char** end,int base);
long labs2(long x);
typedef struct { long quot,rem; } ldiv_t;
ldiv_t ldiv(long a,long b);
int imax(int a,int b);
int imin(int a,int b);
/* --- stdio additions --- */
void clearerr(FILE* f);
void rewind(FILE* f);
long ftell(FILE* f);
int fseek(FILE* f,long off,int whence);
int rmdir(const char* path);
int remove(const char* path);
void perror(const char* s);

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
int __stat2(const char* p,struct stat* st,int follow);
int stat(const char* path,struct stat* st);
int lstat(const char* path,struct stat* st);
#define S_IFLNK 0120000
#define S_ISLNK(m) (((m)&0170000)==0120000)
int link(const char* a,const char* b);
int symlink(const char* t,const char* p);
ssize_t readlink(const char* p,char* b,size_t n);
int chown(const char* p,int u,int g);
int lchown(const char* p,int u,int g);
int chmod(const char* p,unsigned m);
struct utimbuf { unsigned long actime, modtime; };
int utime(const char* p,const struct utimbuf* t);
int mount(const char* src,const char* target,const char* type,unsigned long fl,const void* data);
int umount(const char* target);
int sethostname(const char* n,size_t l);
int fstat2(int fd,struct stat* st);

/* ================= setjmp / longjmp (pure asm, no OS) ================= */
typedef unsigned long jmp_buf[8];
__attribute__((returns_twice,noinline)) int setjmp(jmp_buf b);
__attribute__((noreturn,noinline)) void longjmp(jmp_buf b,int v);

/* ================= IEEE-754 math edge cases (<math.h>) ================= */
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
int isnan(double x);
int isinf(double x);
int isfinite(double x);
int signbit(double x);
int fpclassify(double x);
double nan_(const char* s);
double frexp(double x,int* e);
double ldexp(double x,int e);
double modf(double x,double* ip);

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
const char* strerror(int e);

/* ================= 30 more libc functions ================= */
/* --- string / mem (1-9) --- */
void* memmem(const void* h,size_t hl,const void* n,size_t nl);
char* strsep(char** sp,const char* delim);
size_t strlcpy(char* d,const char* s,size_t n);
size_t strlcat(char* d,const char* s,size_t n);
char* strcasestr(const char* h,const char* n);
char* strrev(char* s);
char* strchrnul(const char* s,int c);
void* mempcpy(void* d,const void* s,size_t n);
size_t strspn2(const char* s,const char* set);
/* --- math (10-18) --- */
double cbrt(double x);
double expm1(double x);
double log1p(double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double nearbyint(double x);
double remainder2(double a,double b);
double exp2(double x);
extern /* --- stdlib / process (19-26) --- */
void (*__atexit_fn[8])(void); extern int __atexit_n;
int atexit(void(*f)(void));
__attribute__((noreturn)) void abort(void);
int system(const char* cmd);   /* spawn a command */
int usleep(unsigned us);
long random(void);
void srandom(unsigned s);
int getpagesize(void);
long sysconf(int name);
/* --- stdio (27-30) --- */
int fileno(FILE* f);
long getline(char** lineptr,size_t* n,FILE* f);
int isatty(int fd);
char* basename(char* p);

/* ================= 50 more libc functions (app-oriented) ================= */
/* --- ctype extras & conversions (1-6) --- */
int isblank(int c);
int isgraph(int c);
int digittoint(int c);
char* itoa(int v,char* buf,int base);
char* ltoa(long v,char* buf,int base);
char* utoa(unsigned v,char* buf,int base);
/* --- string search / tokenize (7-14) --- */
char* strtok_r(char* s,const char* delim,char** save);
int strncmp2(const char* a,const char* b,size_t n);
char* strnstr(const char* h,const char* n,size_t len);
int startswith(const char* s,const char* pre);
int endswith(const char* s,const char* suf);
char* strltrim(char* s);
char* strrtrim(char* s);
char* strtrim(char* s);
/* --- math extras (15-24) --- */
double deg2rad(double d);
double rad2deg(double r);
double clampd(double v,double lo,double hi);
int clampi(int v,int lo,int hi);
double lerp(double a,double b,double t);
long lround(double x);
int isqrt(int x);
double fdim(double a,double b);
double fma_(double a,double b,double c);
int ilog2(unsigned v);
/* --- stdlib / memory (25-32) --- */
void* memdup(const void* p,size_t n);
void bzero(void* p,size_t n);
void bcopy(const void* s,void* d,size_t n);
int ffs(int v);
int popcount(unsigned v);
unsigned bswap32(unsigned v);
unsigned short bswap16(unsigned short v);
unsigned long bswap64(unsigned long v);
/* --- time formatting (33-40) --- */
struct tm { int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst; };
extern int __mdays[12];
struct tm* gmtime_r(const time_t* t,struct tm* r);
struct tm* gmtime(const time_t* t);
/* TZ: POSIX style "CET-1" / "EST5" (offset west of UTC) or ISO style "+02:00" (east of UTC). Unset = UTC. */
long __tz_offset(void);
struct tm* localtime(const time_t* t);
extern const char* __wd[7];
extern const char* __mo[12];
size_t strftime(char* buf,size_t cap,const char* fmt,const struct tm* t);
char* asctime(const struct tm* t);
char* ctime(const time_t* t);
/* --- app utilities (41-50) --- */
const char* getprogname(void);
int max3(int a,int b,int c);
int min3(int a,int b,int c);
void swapi(int* a,int* b);
int gcd(int a,int b);
long lcm(int a,int b);
int parse_int(const char* s,int* ok);
char* strjoin(char* dst,size_t cap,const char* a,const char* sep,const char* b);
int hexval(const char* s);
void hexdump_line(const unsigned char* p,int n,char* out);

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
extern int __raw_mode;
int tcgetattr(int fd,struct termios* t);
int tcsetattr(int fd,int act,const struct termios* t);
void cfmakeraw(struct termios* t);
/* raw single-char read (blocking) and non-blocking poll */
int getch(void);        /* blocking raw key */
int getch_nb(void);     /* -1 if none */
int kbhit(void);                       /* returns key or -1 */

/* ================= ANSI cursor / screen control (terminal escapes) ================= */
void term_clear_screen(void);
void term_gotoxy(int x,int y);
void term_hide_cursor(void);
void term_show_cursor(void);
void term_color(int fg);
void term_reset(void);

/* ================= select / poll (readiness over the fd set) ================= */
typedef struct { unsigned long b[16]; } fd_set;
void FD_ZERO(fd_set* s);
void FD_SET(int fd,fd_set* s);
void FD_CLR(int fd,fd_set* s);
int FD_ISSET(int fd,fd_set* s);
struct timeval { long tv_sec,tv_usec; };
/* single-process model: stdin ready = a key is waiting; other fds treated ready. */
int select(int nfds,fd_set* rd,fd_set* wr,fd_set* ex,struct timeval* tv);
struct pollfd { int fd; short events; short revents; };
#define POLLIN 1
#define POLLOUT 4
int poll(struct pollfd* fds,unsigned n,int timeout);

/* ================= pthreads (minimal, over the kernel thread_create) ================= */
/* NoovexOS uses a cooperative scheduler; these map to kernel threads (syscall 61)
   and a spinlock-based mutex. Suitable for simple concurrent tasks, not a full
   preemptive pthread implementation. */
typedef unsigned long pthread_t;
typedef struct { volatile int lock; } pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {0}
int pthread_create(pthread_t* th,const void* attr,void*(*fn)(void*),void* arg);
int pthread_join(pthread_t th,void** ret);
int pthread_mutex_init(pthread_mutex_t* m,const void* a);
int pthread_mutex_lock(pthread_mutex_t* m);
int pthread_mutex_unlock(pthread_mutex_t* m);
int pthread_mutex_destroy(pthread_mutex_t* m);
pthread_t pthread_self(void);

/* ================= dlopen / dlsym (honest stub: no dynamic linker) ================= */
/* NoovexOS statically links programs; there is no runtime dynamic loader yet.
   These exist so code that references them compiles, but they always fail. */
#define RTLD_LAZY 1
#define RTLD_NOW  2
void* dlopen(const char* path,int flag);
void* dlsym(void* h,const char* sym);
int dlclose(void* h);
const char* dlerror(void);

/* ================= 100 more libc functions (v7.94) ================= */

/* ---- strings (17) ---- */
char* strndup(const char* s,size_t n);
long long strtoll(const char* s,char** end,int base);
unsigned long long strtoull(const char* s,char** end,int base);
double strtod(const char* s,char** end);
float strtof(const char* s,char** end);
void* memccpy(void* d,const void* s,int c,size_t n);
void explicit_bzero(void* p,size_t n);
char* index(const char* s,int c);
char* rindex(const char* s,int c);
const char* strsignal(int sig);
int strverscmp(const char* a,const char* b);
char* strupr(char* s);
char* strlwr(char* s);
int strcount(const char* s,int c);
char* strreplace(char* s,int from,int to);
int strsplit(char* s,char delim,char** out,int max);
long long atoll(const char* s);

/* ---- stdlib (12) ---- */
typedef struct { long long quot,rem; } lldiv_t;
long long llabs(long long x);
lldiv_t lldiv(long long a,long long b);
int rand_r(unsigned* seed);
extern unsigned long long __r48;
void srand48(long s);
unsigned long long __next48(void);
double drand48(void);
long lrand48(void);
long mrand48(void);
void __bswap(unsigned char* a,unsigned char* b,size_t sz);
void qsort_r(void* base,size_t n,size_t sz,int(*cmp)(const void*,const void*,void*),void* arg);
void* lfind(const void* key,const void* base,size_t* n,size_t sz,int(*cmp)(const void*,const void*));
void* lsearch(const void* key,void* base,size_t* n,size_t sz,int(*cmp)(const void*,const void*));
void* reallocarray(void* p,size_t n,size_t sz);
char* realpath(const char* path,char* out);

/* ---- float math (15) ---- */
float sqrtf(float x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
float fabsf(float x);
float floorf(float x);
float ceilf(float x);
float powf(float a,float b);
float logf(float x);
float expf(float x);
float roundf(float x);
float fminf(float a,float b);
float fmaxf(float a,float b);
float fmodf(float a,float b);
float atan2f(float y,float x);

/* ---- extra math (13) ---- */
double asinh(double x);
double acosh(double x);
double atanh(double x);
double erf(double x);
double erfc(double x);
double tgamma(double x);
double lgamma(double x);
double rint(double x);
long lrint(double x);
long long llround(double x);
double scalbn(double x,int n);
int ilogb(double x);
double logb(double x);

/* ---- stdio (11) ---- */
typedef long fpos_t;
int getc(FILE* f);
int putc(int c,FILE* f);
int fgetpos(FILE* f,fpos_t* pos);
int fsetpos(FILE* f,const fpos_t* pos);
int setvbuf(FILE* f,char* buf,int mode,size_t size);
void setbuf(FILE* f,char* buf);
void setlinebuf(FILE* f);
FILE* fdopen(int fd,const char* mode);
int vprintf(const char* fmt,__builtin_va_list ap);
int vfprintf(FILE* f,const char* fmt,__builtin_va_list ap);
int vasprintf(char** out,const char* fmt,__builtin_va_list ap);
int asprintf(char** out,const char* fmt,...);
long getdelim(char** lineptr,size_t* n,int delim,FILE* f);

/* ---- ctype (2) ---- */
int isascii(int c);
int toascii(int c);

/* ---- time (9) ---- */
struct timespec { long tv_sec, tv_nsec; };
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
int __isleap(int y);
time_t timegm(struct tm* t);
time_t mktime(struct tm* t);
double difftime(time_t a,time_t b);
int gettimeofday(struct timeval* tv,void* tz);
int clock_gettime(int clk,struct timespec* ts);
int nanosleep(const struct timespec* req,struct timespec* rem);
struct tm* localtime_r(const time_t* t,struct tm* r);
char* asctime_r(const struct tm* t,char* buf);
char* ctime_r(const time_t* t,char* buf);

/* ---- unistd / identity (10) ---- */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
int getuid(void);
int setuid(int uid);
int geteuid(void);
int getgid(void);
int getegid(void);
int setgid(int g);
int getppid(void);
int gethostname(char* name,size_t len);
char* getlogin(void);
int access(const char* path,int mode);
char* ttyname(int fd);
extern char* optarg; extern int optind, opterr, optopt; extern int __optpos;
int getopt(int argc,char* const argv[],const char* opts);

/* ---- hashing / encoding / utf-8 (10) ---- */
unsigned crc32(unsigned crc,const void* data,size_t n);
unsigned adler32(unsigned a,const void* data,size_t n);
unsigned fnv1a32(const void* data,size_t n);
unsigned long djb2(const char* s);
extern const char __b64[];
size_t base64_encode(const void* in,size_t n,char* out);
int __b64v(int c);
long base64_decode(const char* in,void* out);
size_t hex_encode(const void* in,size_t n,char* out);
long hex_decode(const char* in,void* out);
int utf8_decode(const char* s,unsigned* cp);
int utf8_encode(unsigned cp,char* out);


/* ================= file management: create, move, shrink, sync, delete (v7.95) ================= */
int rename(const char* from,const char* to);
int rmdir(const char* path);
int truncate(const char* path,off_t len);
int ftruncate(int fd,off_t len);
int fsync(int fd);
void __tmpfill(char* x);
int mkstemp(char* tmpl);
char* mkdtemp(char* tmpl);
FILE* tmpfile(void);   /* deleted on fclose */
int copyfile(const char* src,const char* dst);
int rmtree(const char* path);

#ifndef NVX_U_TYPES
#define NVX_U_TYPES
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long u64;
#endif
/* ================= 50 more math (v8.08) ================= */
double remainder(double x,double y);
double remquo(double x,double y,int* quo);
double drem(double x,double y);
double trunc2(double x);
double significand(double x);
double exp10(double x);
double pow10(double x);
double log2_(double x);
double logbase(double x,double b);
double rsqrt(double x);
double hypot3(double a,double b,double c);
double sec_(double x);
double csc_(double x);
double cot_(double x);
double versin(double x);
double haversin(double x);
double sinc(double x);
double logistic(double x);
double sigmoid(double x);
double relu(double x);
double softplus(double x);
double gd(double x);
double copysignd(double x,double y);
double clamp01(double x);
double clampd2(double x,double lo,double hi);
double wrap(double x,double lo,double hi);
double wrap180(double d);
double wrap2pi(double a);
double normangle(double a);
double lerpd(double a,double b,double t);
double invlerp(double a,double b,double v);
double remap(double v,double a,double b,double c,double d);
double smoothstep(double e0,double e1,double x);
double smootherstep(double e0,double e1,double x);
double step_(double edge,double x);
double frac(double x);
double roundto(double x,int dp);
long roundhalfeven(double x);
double deg_(double r);
double rad_(double d);
double meandd(const double* a,int n);
double sumd(const double* a,int n);
double variance(const double* a,int n);
double stddev(const double* a,int n);
double dmaxv(const double* a,int n);
double dminv(const double* a,int n);
double dot(const double* a,const double* b,int n);
double norm2(const double* a,int n);
long long ipow(long long b,int e);
unsigned long long factorial(unsigned n);
unsigned long long binomial(unsigned n,unsigned k);

/* ================= 80 more file/path helpers (v8.08) ================= */
char* uhex8(char* p,unsigned v);
int fd_read_all(int fd,char* buf,int cap);
int file_exists(const char* p);
int is_dir(const char* p);
int is_file(const char* p);
int is_link(const char* p);
long file_size(const char* p);
unsigned file_mode(const char* p);
long file_mtime(const char* p);
int path_is_abs(const char* p);
const char* path_base(const char* p);
void path_dir(const char* p,char* out);
const char* path_ext(const char* p);
void path_stem(const char* p,char* out);
void path_join(char* out,const char* a,const char* b);
int path_has_ext(const char* p,const char* e);
void path_normalize(const char* p,char* out);
int fd_read_all(int fd,char* buf,int cap);
long read_file(const char* p,char* buf,int cap);
int write_file(const char* p,const void* data,int len);
int append_file(const char* p,const void* data,int len);
int write_str(const char* p,const char* s);
int append_str(const char* p,const char* s);
int append_line(const char* p,const char* s);
int touch_file(const char* p);
int file_equal(const char* a,const char* b);
long count_lines(const char* p);
int mkdirs(const char* path);
int ensure_dir(const char* p);
int dir_count(const char* p);
int dir_empty(const char* p);
int list_dir(const char* p,char names[][64],int max);
int foreach_file(const char* p,void(*fn)(const char*,void*),void* ctx);
int copy_file2(const char* src,const char* dst);
int move_file(const char* src,const char* dst);
int remove_file(const char* p);
int remove_dir(const char* p);
long fd_size(int fd);
int fd_rewind(int fd);
int fd_eof(int fd);
int read_at(int fd,long off,void* buf,int len);
int write_at(int fd,long off,const void* buf,int len);
int slurp_lines(const char* p,char lines[][128],int max);
int file_getline(int fd,char* out,int cap);
int freadable(const char* p);
int fwritable(const char* p);
int fexecutable(const char* p);
int chext(const char* p,const char* newext,char* out);
long tell_fd(int fd);
int truncate_file(const char* p,long n);
int prepend_file(const char* p,const char* s);
int replace_in_file(const char* p,char from,char to);
int file_starts_with(const char* p,const char* pre);
int cat_to(int outfd,const char* p);
int concat_files(const char* dst,const char** srcs,int n);
unsigned file_crc32(const char* p);
int file_isempty(const char* p);
int same_file(const char* a,const char* b);
int mkfile_size(const char* p,long size,char fill);
int rename_ext(const char* p,const char* newext);
int backup_file(const char* p);
int line_at(const char* p,int idx,char* out,int cap);
int grep_file(const char* p,const char* needle,void(*hit)(int,const char*,void*),void* ctx);
int fd_copy(int in,int out);
long dir_total_size(const char* p);
int find_in_dir(const char* dir,const char* name,char* out,int cap);
int with_tmpfile(void(*fn)(const char*,void*),void* ctx);
int atomic_write(const char* p,const void* data,int len);
int chmod_str(const char* p,const char* octal);
int is_hidden(const char* p);
int wc_words(const char* p);
int copy_into_dir(const char* file,const char* dir);
int move_into_dir(const char* file,const char* dir);
long fappend_fd(int fd,const void* data,int len);
int read_first_line(const char* p,char* out,int cap);
int read_last_line(const char* p,char* out,int cap);
int trunc_to_lines(const char* p,int keep);
int dump_hex_file(const char* p,int outfd);
int line_count_str(const char* p);
int file_append_fmt(const char* p,const char* s);
int swap_files(const char* a,const char* b);
int file_head(const char* p,int nlines,int outfd);

/* ================= 100 cryptographic (v8.08) — userland, self-contained ================= */
u32 nx_rotr32(u32 x,int n);
u32 nx_rotl32(u32 x,int n);
u64 nx_rotr64(u64 x,int n);
u64 nx_rotl64(u64 x,int n);
u16 bswap16c(u16 x);
u32 bswap32c(u32 x);
u64 bswap64c(u64 x);
u32 ld32be(const u8* p);
u32 ld32le(const u8* p);
u64 ld64be(const u8* p);
void st32be(u8* p,u32 v);
void st32le(u8* p,u32 v);
void st64be(u8* p,u64 v);

/* ---- SHA-256 ---- */
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } nx_sha256;
extern const u32 NXK256[64];
void nx_sha256_blk(nx_sha256* s,const u8* p);
void sha256_init(nx_sha256* s);
void sha256_update(nx_sha256* s,const void* data,int len);
void sha256_final(nx_sha256* s,u8 out[32]);
void sha256(const void* data,int len,u8 out[32]);
void sha224(const void* data,int len,u8 out[28]);

/* ---- SHA-1 ---- */
void sha1(const void* data,int len,u8 out[20]);

/* ---- MD5 ---- */
void md5(const void* data,int len,u8 out[16]);

/* ---- HMAC ---- */
void hmac_sha256(const u8* key,int klen,const u8* msg,int mlen,u8 out[32]);
void hmac_md5c(const u8* key,int klen,const u8* msg,int mlen,u8 out[16]);
void hmac_sha1c(const u8* key,int klen,const u8* msg,int mlen,u8 out[20]);
void hkdf_sha256(const u8* salt,int sl,const u8* ikm,int il,const u8* info,int inl,u8* out,int olen);
void pbkdf2_sha256(const u8* pw,int pl,const u8* salt,int sl,int iter,u8* out,int olen);

/* ---- non-crypto hashes ---- */
u32 fnv1_32(const void* d,int n);
u64 fnv1a_64(const void* d,int n);
u32 murmur3_32(const void* key,int len,u32 seed);
u32 jenkins_oaat(const void* d,int n);
u32 sdbm_hash(const char* s);
u32 elf_hash(const char* s);
u32 crc32c(u32 crc,const void* d,int n);
u16 crc16_ccitt(const void* d,int n);
u8 crc8(const void* d,int n);
u32 xxhash32(const void* in,int len,u32 seed);

/* ---- ChaCha20 ---- */
void chacha_qr(u32* s,int a,int b,int c,int d);
void chacha20_block(const u8 key[32],const u8 nonce[12],u32 counter,u8 out[64]);
void chacha20(const u8 key[32],const u8 nonce[12],u32 counter,const u8* in,u8* out,int len);

extern /* ---- base32, base58, base64url, hex ---- */
const char NX_B32[];
int base32_encode(const u8* in,int n,char* out);
int base64url_encode(const u8* in,int n,char* out);
int base16_encode(const u8* in,int n,char* out);
extern const char NX_B58[];
int base58_encode(const u8* in,int n,char* out);

/* ---- XOR / OTP / rot ---- */
void xor_bytes(u8* dst,const u8* a,const u8* b,int n);
void xor_key(u8* buf,int n,const u8* key,int kn);
void xor_cipher(const char* in,char* out,int n,u8 k);
void rot13(char* s);
void rot_n(char* s,int n);
void caesar(char* s,int shift);
void vigenere(char* s,const char* key,int dec);

/* ---- constant-time & utils ---- */
int ct_memeq(const void* a,const void* b,int n);
void secure_zero(void* p,int n);
int hash_eq(const u8* a,const u8* b,int n);
void mask_bytes(u8* p,int n,u8 m);
u32 checksum32(const void* d,int n);
u8 xor_checksum(const void* d,int n);
u16 fletcher16(const void* d,int n);
u32 fletcher32(const u16* d,int n);
int luhn_check(const char* s);
u8 parity8(u8 x);
int popcount32(u32 x);
int popcount64(u64 x);
int clz32(u32 x);
int ctz32(u32 x);
u32 reverse_bits32(u32 x);

/* ---- SipHash-2-4 ---- */
u64 siphash24(const void* in,int inlen,const u8 key[16]);

/* ---- Diffie-Hellman-ish modexp (small), and misc ---- */
u64 mulmod64(u64 a,u64 b,u64 m);
u64 powmod64(u64 b,u64 e,u64 m);
int miller_rabin(u64 n);
u64 gcd64(u64 a,u64 b);
u64 lcm64(u64 a,u64 b);
long egcd(long a,long b,long* x,long* y);
long modinv(long a,long m);

/* ---- one-shot digests to hex strings ---- */
void sha256_hex(const void* d,int n,char* out);
void sha1_hex(const void* d,int n,char* out);
void md5_hex(const void* d,int n,char* out);
void hmac_sha256_hex(const u8* k,int kl,const u8* m,int ml,char* out);
void sha224_hex(const void* d,int n,char* out);
void hmac_sha1_hex(const u8* k,int kl,const u8* m,int ml,char* out);
void hmac_md5_hex(const u8* k,int kl,const u8* m,int ml,char* out);
u64 hash64_str(const char* s);
u32 hash32_str(const char* s);
int chacha20_selftest(void);
void keystream(const u8 key[32],const u8 nonce[12],u8* out,int n);
u32 adler32c(u32 a,const void* d,int n);
u8 bcd_encode(u8 v);
u8 bcd_decode(u8 v);
u32 rol_hash(const char* s);
u32 djb2a(const char* s);
u16 internet_checksum(const void* d,int n);
void nx_random_key(u8* out,int n);
int is_prime_u32(u32 n);
u64 next_prime(u64 n);
u32 mix32(u32 a,u32 b);
u64 splitmix64_hash(u64 x);
u8 hamming_distance(u8 a,u8 b);
int hamming_bytes(const u8* a,const u8* b,int n);
u32 crc32_str(const char* s);
void obfuscate(u8* p,int n,u32 seed);
void deobfuscate(u8* p,int n,u32 seed);

/* ================= 150 general utilities (v8.08) ================= */
/* --- bit tricks (24) --- */
u32 next_pow2(u32 x);
int is_pow2(u64 x);
u32 prev_pow2(u32 x);
int log2i(u32 x);
u32 round_up(u32 x,u32 a);
u32 round_down(u32 x,u32 a);
u32 align_up(u32 x,u32 a);
u32 align_down(u32 x,u32 a);
int get_bit(u32 x,int n);
u32 set_bit(u32 x,int n);
u32 clr_bit(u32 x,int n);
u32 tgl_bit(u32 x,int n);
u32 bit_mask(int n);
u32 extract_bits(u32 x,int lo,int cnt);
u32 deposit_bits(u32 x,int lo,int cnt,u32 v);
u32 lowest_bit(u32 x);
u32 clear_lowest(u32 x);
int count_leading_zeros(u32 x);
int count_trailing_zeros(u32 x);
u8 reverse_byte(u8 b);
u16 pack_u16(u8 hi,u8 lo);
u32 pack_u32(u8 a,u8 b,u8 c,u8 d);
int sign(long x);
u32 interleave2(u16 x,u16 y);

/* --- int helpers (26) --- */
long lmax(long a,long b);
long lmin(long a,long b);
int iclamp(int x,int lo,int hi);
long lclamp(long x,long lo,long hi);
int iabs2(int x);
int isqz(long x);
void iswap(int* a,int* b);
void lswap(long* a,long* b);
void pswap(void** a,void** b);
int cmp_int(const void* a,const void* b);
int cmp_int_desc(const void* a,const void* b);
int cmp_long(const void* a,const void* b);
int cmp_double(const void* a,const void* b);
int cmp_str(const void* a,const void* b);
long isum(const int* a,int n);
int imaxv(const int* a,int n);
int iminv(const int* a,int n);
int iavg(const int* a,int n);
int digit_count(long x);
long parse_long(const char* s,int base);
int clampi_wrap(int x,int n);
int int_pow(int b,int e);
int int_sqrt(int x);

/* --- array ops (22) --- */
void arr_fill_int(int* a,int n,int v);
void arr_iota(int* a,int n,int start);
void arr_reverse_int(int* a,int n);
int arr_find_int(const int* a,int n,int v);
int arr_contains_int(const int* a,int n,int v);
int arr_count_int(const int* a,int n,int v);
void arr_copy_int(int* dst,const int* src,int n);
int arr_unique_int(int* a,int n);
int arr_remove_int(int* a,int n,int v);
void arr_rotate_int(int* a,int n,int k);
int arr_sum_range(const int* a,int lo,int hi);
int arr_min_idx(const int* a,int n);
int arr_max_idx(const int* a,int n);
void arr_shuffle_int(int* a,int n);
int arr_bsearch_int(const int* a,int n,int v);
int arr_is_sorted_int(const int* a,int n);
void arr_scale_int(int* a,int n,int f);
void arr_map_int(int* a,int n,int(*fn)(int));
int arr_reduce_int(const int* a,int n,int init,int(*fn)(int,int));
int arr_filter_int(const int* src,int n,int* dst,int(*pred)(int));
void arr_zero(void* p,int n);
int arr_equal_int(const int* a,const int* b,int n);

/* --- string extras (30) --- */
int str_count_char(const char* s,char c);
int str_index(const char* s,char c);
int str_rindex(const char* s,char c);
int str_prefix(const char* s,const char* p);
int str_suffix(const char* s,const char* suf);
int str_contains(const char* s,const char* sub);
void str_repeat(char* out,const char* s,int n);
int str_replace_all(char* s,const char* from,const char* to);
void str_pad_left(char* out,const char* s,int width,char pad);
void str_pad_right(char* out,const char* s,int width,char pad);
void str_center(char* out,const char* s,int width);
void str_toupper(char* s);
void str_tolower(char* s);
void str_capitalize(char* s);
void str_title(char* s);
void str_swapcase(char* s);
int str_is_numeric(const char* s);
int str_is_alpha(const char* s);
int str_is_alnum(const char* s);
int str_is_blank(const char* s);
int str_eq_ci(const char* a,const char* b);
void str_reverse(char* s);
int str_word_count(const char* s);
void str_squeeze(char* s);
void str_remove_char(char* s,char c);
int str_common_prefix(const char* a,const char* b);
int str_levenshtein(const char* a,const char* b);
int str_to_bool(const char* s);
void str_ellipsis(char* out,const char* s,int max);

/* --- color / graphics (14) --- */
u32 rgb_pack(u8 r,u8 g,u8 b);
void rgb_unpack(u32 c,u8* r,u8* g,u8* b);
u32 rgb_lerp(u32 a,u32 b,int t);
u8 rgb_luma(u32 c);
u32 rgb_gray(u32 c);
u32 rgb_invert(u32 c);
u32 rgb_scale(u32 c,int pct);
u32 rgb_from_hex(const char* s);
void rgb_to_hex(u32 c,char* out);
u32 hsv_to_rgb(int h,int s,int v);
u32 rgb_blend(u32 a,u32 b,int alpha);
u32 rgb_darken(u32 c,int pct);
u32 rgb_lighten(u32 c,int pct);
int rgb_distance(u32 a,u32 b);

/* --- time / misc (18) --- */
int is_leap_year(int y);
int days_in_month(int y,int m);
int day_of_year(int y,int m,int d);
int day_of_week(int y,int m,int d);
const char* month_name(int m);
const char* weekday_name(int w);
long unix_from_ymd(int y,int m,int d,int hh,int mm,int ss);
void secs_to_hms(long s,int* h,int* m,int* sec);
void fmt_duration(char* out,long secs);
void fmt_bytes(char* out,u64 n);
void fmt_commas(char* out,long n);
int roll_dice(int sides);
int rand_range(int lo,int hi);
double rand_double(void);
int coin_flip(void);
int percent_chance(int pct);
void rand_string(char* out,int n);
u32 rand_color(void);

/* --- validation / conversion (16) --- */
int valid_ipv4(const char* s);
int valid_email(const char* s);
int valid_hex(const char* s);
u32 ipv4_to_u32(const char* s);
void u32_to_ipv4(u32 ip,char* out);
int hexval2(char c);
long bin_to_long(const char* s);
void long_to_bin(long v,int bits,char* out);
long oct_to_long(const char* s);
int char_to_digit(char c);
char digit_to_char(int d);
int roman_to_int(const char* s);
void int_to_roman(int n,char* out);
long clamp_long(long x,long lo,long hi);
int approx_eq(double a,double b,double eps);
int in_range(long x,long lo,long hi);
int even(long x);

#endif /* NVX_POSIX_H */
