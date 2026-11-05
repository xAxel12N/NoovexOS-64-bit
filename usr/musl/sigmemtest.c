#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>

static int fails, passes;
static void check(int ok, const char* name, const char* det){
    if(ok){ passes++; printf("ok   %s\n", name); }
    else { fails++; printf("FAIL %s%s%s\n", name, det ? ": " : "", det ? det : ""); }
    fflush(stdout);
}

static sigjmp_buf g_jb;
static volatile void* g_addr;
static volatile int g_code, g_sig;
static volatile unsigned long g_sp;
static void on_fault(int s, siginfo_t* si, void* uc){
    (void)uc; char here; g_sp = (unsigned long)&here;
    g_sig = s; g_addr = si->si_addr; g_code = si->si_code;
    siglongjmp(g_jb, 1);
}
static int trap(void (*fn)(void*), void* arg){
    g_sig = 0; g_addr = 0; g_code = 0;
    if(sigsetjmp(g_jb, 1) == 0){ fn(arg); return 0; }
    return 1;
}
static void do_write(void* p){ *(volatile int*)p = 42; }
static void do_read(void* p){ volatile int x = *(volatile int*)p; (void)x; }
static void do_call(void* p){ ((void(*)(void))p)(); }

static volatile int g_skipped;
static void on_skip(int s, siginfo_t* si, void* ucv){
    (void)s; (void)si; ucontext_t* uc = (ucontext_t*)ucv;
    uc->uc_mcontext.gregs[REG_RIP] += 2;
    uc->uc_mcontext.gregs[REG_RAX] = 1234;
    g_skipped = 1;
}

static volatile int g_tk_code, g_tk_sig;
static void on_usr(int s, siginfo_t* si, void* uc){ (void)uc; g_tk_sig = s; g_tk_code = si->si_code; }

static volatile int g_depth;
static void on_alt(int s){ (void)s; char here; g_sp = (unsigned long)&here; g_depth++; }

int main(void){
    long pg = sysconf(_SC_PAGESIZE);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_fault; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0); sigaction(SI