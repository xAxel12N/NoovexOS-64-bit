#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>

static int pass, fail;
static void ck(int ok, const char* what) {
    if (ok) pass++;
    else { fail++; printf("sched: FAIL %s (errno %d)\n", what, errno); }
}

static double now(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static long rawprio(void) { return syscall(SYS_getpriority, PRIO_PROCESS, 0); }
static int ssched(int pol, struct sched_param* p) { return (int)syscall(SYS_sched_setscheduler, 0, pol, p); }
static int gsched(void) { return (int)syscall(SYS_sched_getscheduler, 0); }
static int gparam(struct sched_param* p) { return (int)syscall(SYS_sched_getparam, 0, p); }

static int stat_nice(void) {
    char b[1024]; int fd = open("/proc/self/stat", O_RDONLY); if (fd < 0) return -999;
    int n = read(fd, b, sizeof b - 1); close(fd); if (n <= 0) return -999; b[n] = 0;
    char* p = strrchr(b, ')'); if (!p) return -999; p += 2;
    int field = 3; while (*p && field < 19) { if (*p == ' ') field++; p++; }
    return atoi(p);
}

static void spinner(int wfd, int nice_v, double secs) {
    setpriority(PRIO_PROCESS, 0, nice_v);
    double t0 = now(); unsigned long long n = 0;
    while (now() - t0 < secs) { for (volatile int i = 0; i < 20000; i++) {} n++; }
    write(wfd, &n, sizeof n);
    _exit(0);
}

int main(void) {
    errno = 0;
    ck(rawprio() == 20, "raw getpriority is 20 at nice 0");
    ck(getpriority(PRIO_PROCESS, 0) == 0, "getpriority returns nice 0");
    ck(setpriority(PRIO_PROCESS, 0, 5) == 0 && getpriority(PRIO_PROCESS, 0) == 5, "setpriority to nice 5");
    ck(stat_nice() == 5, "/proc/self/stat shows nice 5");
    ck(setpriority(PRIO_PROCESS, 0, 50) == 0 && getpriority(PRIO_PROCESS, 0) == 19, "nice above 19 is clamped");
    ck(setpriority(PRIO_PROCESS, 0, 0) == 0 && getpriority(PRIO_PROCESS, 0) == 0, "root lowers nice back to 0");
    errno = 0; ck(setpriority(7, 0, 0) == -1 && errno == EINVAL, "bad 'which' gives EINVAL");
    errno = 0; ck(getpriority(PRIO_PROCESS, 999999) == -1 && errno == ESRCH, "unknown pid gives ESRCH");
    ck(gsched() == SCHED_OTHER, "policy starts as SCHED_OTHER");
    ck(sched_get_priority_max(SCHED_FIFO) == 99 && sched_get_priority_min(SCHED_FIFO) == 1, "FIFO priority range 1-99");
    ck(sched_get_priority_max(SCHED_OTHER) == 0, "SCHED_OTHER priority range 0");
    struct sched_param sp; sp.sched_priority = 10;
    ck(ssched(SCHED_FIFO, &sp) == 0 && gsched() == SCHED_FIFO, "switch to SCHED_FIFO 10");
    struct sched_param g; memset(&g, 0, sizeof g);
    ck(gparam(&g) == 0 && g.sched_priority == 10, "sched_getparam reads 10");
    sp.sched_priority = 0;
    ck(ssched(SCHED_OTHER, &sp) == 0 && gsched() == SCHED_OTHER, "back to SCHED_OTHER");
    errno = 0; ck(ssched(SCHED_FIFO, &sp) == -1 && errno == EINVAL, "FIFO with priority 0 is refused");
    struct timespec rr; ck(sched_rr_get_interval(0, &rr) == 0 && rr.tv_sec == 0 && rr.tv_nsec > 0, "round-robin interval");
    ck(ssched(SCHED_IDLE, &sp) == 0 && gsched() == SCHED_IDLE, "SCHED_IDLE accepted");
    ssched(SCHED_OTHER, &sp);

    int ncpu = (int)sysconf(_SC_NPROCESSORS_ONLN); if (ncpu < 1) ncpu = 1; if (ncpu > 8) ncpu = 8;
    int k = ncpu + 1; int pa[2], pb[2]; pipe(pa); pipe(pb);
    for (int i = 0; i < k; i++) { if (fork() == 0) spinner(pa[1], 0, 3.0); if (fork() == 0) spinner(pb[1], 10, 3.0); }
    for (int i = 0; i < 2 * k; i++) wait(0);
    unsigned long long sa = 0, sb = 0, v;
    for (int i = 0; i < k; i++) { if (read(pa[0], &v, sizeof v) == sizeof v) sa += v; if (read(pb[0], &v, sizeof v) == sizeof v) sb += v; }
    double ratio = sb ? (double)sa / (double)sb : 999.0;
    printf("sched: %d cores, %d busy programs at nice 0 and %d at nice 10: work ratio %.1f (ideal about 9)\n", ncpu, k, k, ratio);
    ck(ratio > 3.0, "nice 0 programs get clearly more time than nice 10 ones");
    printf("sched: done %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
