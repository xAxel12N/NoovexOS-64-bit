#ifndef NVX_TYPES_H
#define NVX_TYPES_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
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







#define F_DUPFD 0
#define F_GETFD 1
#define F_SETFD 2
#define F_GETFL 3
#define F_SETFL 4
#define FD_CLOEXEC 1
#define O_CLOEXEC 0x80000
#define O_NONBLOCK 0x800














/* ================= process control ================= */
pid_t fork(void);
/* execv-like: NoovexOS exec takes a path; args are advisory (single-arg model) */
int execv(const char* path,char* const argv[]);




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



/* ================= memory ================= */
void* sbrk(long inc);
/* a simple bump+free-list malloc */
typedef struct blk { size_t sz; struct blk* next; } blk_t;
extern blk_t* g_free;





/* ================= string / mem ================= */
size_t strlen(const char* s);











/* ================= stdio (printf family, on write) ================= */
void __putc(int fd,char c);




 




/* ================= more string / stdlib / ctype ================= */
char* strstr(const char* h,const char* n);










extern char __env_buf[128];




extern int getcwd_helper;

/* ================= misc ================= */
void sleep_ms(unsigned ms);
   /* monotonic ms since boot */

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
 
extern FILE* __ffiles[16];

























/* ================= snprintf / sprintf (format into a buffer) ================= */
typedef struct { char* buf; int cap; int len; } __sbuf;







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














/* ================= unistd extras ================= */
char* getcwd(char* buf,size_t n);                                                                                                

/* ================= BSD-style sockets (TCP) ================= */
#define AF_INET     2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6
typedef unsigned int in_addr_t;
typedef unsigned short in_port_t;
struct in_addr { in_addr_t s_addr; };
struct sockaddr_in { unsigned short sin_family; in_port_t sin_port; struct in_addr sin_addr; char pad[8]; };
struct sockaddr { unsigned short sa_family; char sa_data[14]; };







/* resolve a hostname (or dotted IP) to a big-endian IP; returns 0 on failure */
unsigned int resolve(const char* host);





/* inet_addr: dotted string -> big-endian (via resolve, which parses dotted too) */
in_addr_t inet_addr(const char* s);

/* ================= (1) scanf / sscanf ================= */
const char* __scan_num(const char* p,long* out,int base);





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










/* ================= (4) time.h ================= */
typedef long time_t; typedef long clock_t;
#define CLOCKS_PER_SEC 1000
   


/* ================= (5) dirent: opendir/readdir/closedir ================= */
struct dirent { char d_name[64]; unsigned char d_type; unsigned long d_ino; };
#define DT_REG 8
#define DT_DIR 4
typedef struct { int idx; struct dirent ent; char path[128]; } DIR;
extern DIR __dirs[4]; extern int __dir_used[4];




/* ================= 15 more standard libc functions ================= */
/* 1. strrchr - last occurrence */
char* strrchr(const char* s,int c);











#define RAND_MAX 0x7fffffff


typedef struct { int quot,rem; } div_t;










/* ================= 25 more standard libc functions ================= */
/* --- math.h additions --- */
double exp(double x);




















typedef struct { long quot,rem; } ldiv_t;











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



#define S_IFLNK 0120000
#define S_ISLNK(m) (((m)&0170000)==0120000)






struct utimbuf { unsigned long actime, modtime; };






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


/* ================= 30 more libc functions ================= */
/* --- string / mem (1-9) --- */
void* memmem(const void* h,size_t hl,const void* n,size_t nl);








/* --- math (10-18) --- */
double cbrt(double x);








extern /* --- stdlib / process (19-26) --- */
void (*__atexit_fn[8])(void); extern int __atexit_n;

__attribute__((noreturn)) void abort(void);
   




/* --- stdio (27-30) --- */
int fileno(FILE* f);




/* ================= 50 more libc functions (app-oriented) ================= */
/* --- ctype extras & conversions (1-6) --- */
int isblank(int c);





/* --- string search / tokenize (7-14) --- */
char* strtok_r(char* s,const char* delim,char** save);







/* --- math extras (15-24) --- */
double deg2rad(double d);









/* --- stdlib / memory (25-32) --- */
void* memdup(const void* p,size_t n);







/* --- time formatting (33-40) --- */
struct tm { int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst; };
extern int __mdays[12];


/* TZ: POSIX style "CET-1" / "EST5" (offset west of UTC) or ISO style "+02:00" (east of UTC). Unset = UTC. */
long __tz_offset(void);

extern const char* __wd[7];
extern const char* __mo[12];



/* --- app utilities (41-50) --- */
const char* getprogname(void);










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



                                    /* returns key or -1 */

/* ================= ANSI cursor / screen control (terminal escapes) ================= */
void term_clear_screen(void);






/* ================= select / poll (readiness over the fd set) ================= */
typedef struct { unsigned long b[16]; } fd_set;




struct timeval { long tv_sec,tv_usec; };
/* single-process model: stdin ready = a key is waiting; other fds treated ready. */
int select(int nfds,fd_set* rd,fd_set* wr,fd_set* ex,struct timeval* tv);
struct pollfd { int fd; short events; short revents; };
#define POLLIN 1
#define POLLOUT 4


/* ================= pthreads (minimal, over the kernel thread_create) ================= */
/* NoovexOS uses a cooperative scheduler; these map to kernel threads (syscall 61)
   and a spinlock-based mutex. Suitable for simple concurrent tasks, not a full
   preemptive pthread implementation. */
typedef unsigned long pthread_t;
typedef struct { volatile int lock; } pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {0}








/* ================= dlopen / dlsym (honest stub: no dynamic linker) ================= */
/* NoovexOS statically links programs; there is no runtime dynamic loader yet.
   These exist so code that references them compiles, but they always fail. */
#define RTLD_LAZY 1
#define RTLD_NOW  2





/* ================= 100 more libc functions (v7.94) ================= */

/* ---- strings (17) ---- */
char* strndup(const char* s,size_t n);

















/* ---- stdlib (12) ---- */
typedef struct { long long quot,rem; } lldiv_t;



extern unsigned long long __r48;












/* ---- float math (15) ---- */
float sqrtf(float x);















/* ---- extra math (13) ---- */
double asinh(double x);













/* ---- stdio (11) ---- */
typedef long fpos_t;














/* ---- ctype (2) ---- */
int isascii(int c);



#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1











/* ---- unistd / identity (10) ---- */
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1











extern char* optarg; extern int optind, opterr, optopt; extern int __optpos;


/* ---- hashing / encoding / utf-8 (10) ---- */
unsigned crc32(unsigned crc,const void* data,size_t n);



extern const char __b64[];









/* ================= file management: create, move, shrink, sync, delete (v7.95) ================= */
int rename(const char* from,const char* to);







   


#ifndef NVX_U_TYPES
#define NVX_U_TYPES
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long u64;
#endif
/* ================= 50 more math (v8.08) ================= */
double remainder(double x,double y);



















































/* ================= 80 more file/path helpers (v8.08) ================= */
char* uhex8(char* p,unsigned v);


















































































/* ================= 100 cryptographic (v8.08) — userland, self-contained ================= */
u32 nx_rotr32(u32 x,int n);













/* ---- SHA-256 ---- */
typedef struct { u32 h[8]; u64 len; u8 buf[64]; int n; } nx_sha256;
extern const u32 NXK256[64];
































extern /* ---- base32, base58, base64url, hex ---- */
const char NX_B32[];



extern const char NX_B58[];




























/* ---- Diffie-Hellman-ish modexp (small), and misc ---- */
u64 mulmod64(u64 a,u64 b,u64 m);



































/* ================= 150 general utilities (v8.08) ================= */
/* --- bit tricks (24) --- */
u32 next_pow2(u32 x);
























/* --- int helpers (26) --- */
long lmax(long a,long b);























/* --- array ops (22) --- */
void arr_fill_int(int* a,int n,int v);






















/* --- string extras (30) --- */
int str_count_char(const char* s,char c);





























/* --- color / graphics (14) --- */
u32 rgb_pack(u8 r,u8 g,u8 b);














/* --- time / misc (18) --- */
int is_leap_year(int y);


















/* --- validation / conversion (16) --- */
int valid_ipv4(const char* s);

















#endif /* NVX_POSIX_H */

extern void __nvx_sigtramp(void);
#endif
