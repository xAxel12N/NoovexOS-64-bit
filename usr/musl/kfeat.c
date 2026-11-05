#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <errno.h>

static int fails, passes;
static void ok(const char* n, int c){
    printf("%s %s\n", c ? "ok  " : "FAIL", n); fflush(stdout);
    if(c) passes++; else fails++;
}

static sigjmp_buf g_jb;
static volatile int g_sig, g_code;
static volatile void* g_addr;
static void on_segv(int s, siginfo_t* si, void* u){
    (void)u; g_sig = s; g_code = si->si_code; g_addr = si->si_addr;
    siglongjmp(g_jb, 1);
}

static char g_altstack[65536];
static volatile int g_on_alt;
static void on_segv_alt(int s, siginfo_t* si, void* u){
    (void)s; (void)si; (void)u;
    char probe; void* sp = &probe;
    g_on_alt = (sp >= (void*)g_altstack && sp < (void*)(g_altstack + sizeof g_altstack));
    siglongjmp(g_jb, 1);
}

int main(void){
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_flags = SA_SIGINFO; sa.sa_sigaction = on_segv; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);

    long page = sysconf(_SC_PAGESIZE);

    /* PROT_NONE guard page faults on read */
    {
        char* p = mmap(0, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        int faulted = 0; g_sig = 0; g_addr = 0;
        if(sigsetjmp(g_jb, 1) == 0){ volatile char c = p[0]; (void)c; }
        else faulted = 1;
        ok("reading a PROT_NONE page faults (SIGSEGV)", faulted && g_sig == SIGSEGV);
        ok("fault reports the faulting address", g_addr == (void*)p);
        munmap(p, page);
    }

    /* PROT_READ page: read works, write faults */
    {
        char* p = mmap(0, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        int rok = 0, wfault = 0;
        if(sigsetjmp(g_jb, 1) == 0){ volatile char c = p[0]; (void)c; rok = 1; }
        if(sigsetjmp(g_jb, 1) == 0){ p[0] = 1; } else wfault = 1;
        ok("a read-only page can be read", rok);
        ok("writing a read-only page faults", wfault && g_sig == SIGSEGV);
        munmap(p, page);
    }

    /* mprotect can add then remove write permission */
    {
        char* p = mmap(0, page, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        int added = 0, removed = 0;
        if(mprotect(p, page, PROT_READ | PROT_WRITE) == 0){
            if(sigsetjmp(g_jb, 1) == 0){ p[0] = 42; added = (p[0] == 42); }
        }
        if(mprotect(p, page, PROT_READ) == 0){
            if(sigsetjmp(g_jb, 1) == 0){ p[0] = 7; } else removed = 1;
        }
        ok("mprotect PROT_WRITE makes a page writable", added);
        ok("mprotect back to PROT_READ makes writes fault again", removed);
        munmap(p, page);
    }

    /* NX: executing a non-executable page faults */
    {
        unsigned char* p = mmap(0, page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        p[0] = 0xC3;                                  /* ret */
        int xfault = 0;
        void (*fn)(void) = (void(*)(void))p;
        if(sigsetjmp(g_jb, 1) == 0){ fn(); } else xfault = 1;
        ok("executing a non-executable page faults (NX)", xfault && g_sig == SIGSEGV);
        if(mprotect(p, page, PROT_READ | PROT_EXEC) == 0){
            int ran = 0;
            if(sigsetjmp(g_jb, 1) == 0){ fn(); ran = 1; }
            ok("mprotect PROT_EXEC lets the page run", ran);
        } else ok("mprotect PROT_EXEC lets the page run", 0);
        munmap(p, page);
    }

    /* sigaltstack: the handler runs on the alternate stack */
    {
        stack_t ss; ss.ss_sp = g_altstack; ss.ss_size = sizeof g_altstack; ss.ss_flags = 0;
        int set = sigaltstack(&ss, 0);
        struct sigaction a; memset(&a, 0, sizeof a);
        a.sa_flags = SA_SIGINFO | SA_ONSTACK; a.sa_sigaction = on_segv_alt; sigemptyset(&a.sa_mask);
        sigaction(SIGSEGV, &a, 0);
        g_on_alt = 0;
        char* p = mmap(0, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(sigsetjmp(g_jb, 1) == 0){ volatile char c = p[0]; (void)c; }
        ok("sigaltstack is accepted", set == 0);
        ok("SA_ONSTACK handler runs on the alternate stack", g_on_alt);
        munmap(p, page);
        stack_t os; sigaltstack(0, &os);
        ok("sigaltstack reports the installed stack", os.ss_sp == g_altstack && os.ss_size == sizeof g_altstack);
        /* restore default handler on the normal stack */
        a.sa_flags = SA_SIGINFO; a.sa_sigaction = on_segv; sigaction(SIGSEGV, &a, 0);
    }

    /* statx returns the size of a file */
    {
        int fd = open("/tmp/kfeat.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if(fd >= 0){ if(write(fd, "hello statx", 11) < 0){} close(fd); }
        struct statx stx; memset(&stx, 0, sizeof stx);
        long r = syscall(SYS_statx, AT_FDCWD, "/tmp/kfeat.txt", 0, STATX_ALL, &stx);
        ok("statx succeeds on a regular file", r == 0);
        ok("statx reports the file size", (stx.stx_mask & STATX_SIZE) && stx.stx_size == 11);
        ok("statx reports a regular-file mode", (stx.stx_mode & S_IFMT) == S_IFREG);
        unlink("/tmp/kfeat.txt");
    }

    /* membarrier query and a global barrier */
    {
        long q = syscall(SYS_membarrier, 0 /* QUERY */, 0, 0);
        ok("membarrier query returns supported commands", q > 0);
        long g = syscall(SYS_membarrier, 1 /* GLOBAL */, 0, 0);
        ok("membarrier global barrier succeeds", g == 0);
    }

    printf("%s\n", fails ? "FAILURES" : "ALL PASS");
    return fails ? 1 : 0;
}
