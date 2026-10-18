#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;
static int npass, nfail;
static char why[200];
static void res(const char* name, int ok) {
    if (ok) npass++; else nfail++;
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name, ok ? "" : " - ", ok ? "" : why);
    fflush(stdout);
    why[0] = 0;
}
#define FAILF(...) do { snprintf(why, sizeof why, __VA_ARGS__); return 0; } while (0)

static int send_fd(int s, int fd, const char* msg) {
    struct msghdr m; memset(&m, 0, sizeof m);
    struct iovec io = { (void*)msg, strlen(msg) + 1 };
    char cb[CMSG_SPACE(sizeof(int))]; memset(cb, 0, sizeof cb);
    m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = cb; m.msg_controllen = sizeof cb;
    struct cmsghdr* c = CMSG_FIRSTHDR(&m); c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof(int));
    return (int)sendmsg(s, &m, 0);
}
static int recv_fd(int s, char* buf, int len, int flags) {
    struct msghdr m; memset(&m, 0, sizeof m);
    struct iovec io = { buf, (size_t)len };
    char cb[CMSG_SPACE(sizeof(int) * 4)]; memset(cb, 0, sizeof cb);
    m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = cb; m.msg_controllen = sizeof cb;
    if (recvmsg(s, &m, flags) < 0) return -2;
    for (struct cmsghdr* c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) { int fd; memcpy(&fd, CMSG_DATA(c), sizeof fd); return fd; }
    return -1;
}

static int t_memfd_fork(void) {
    int fd = memfd_create("nvx-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) FAILF("memfd_create: %s", strerror(errno));
    if (ftruncate(fd, 1 << 20)) FAILF("ftruncate: %s", strerror(errno));
    char* p = mmap(0, 1 << 20, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) FAILF("mmap: %s", strerror(errno));
    pid_t c = fork();
    if (c == 0) { strcpy(p + 4096 * 100, "child-wrote"); _exit(0); }
    int st; waitpid(c, &st, 0);
    if (strcmp(p + 4096 * 100, "child-wrote")) FAILF("parent does not see the child's write");
    munmap(p, 1 << 20); close(fd); return 1;
}

static int t_scm_rights(void) {
    int sv[2]; if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv)) FAILF("socketpair: %s", strerror(errno));
    pid_t c = fork();
    if (c == 0) {
        close(sv[0]); char b[64];
        int fd = recv_fd(sv[1], b, sizeof b, MSG_CMSG_CLOEXEC);
        if (fd < 0) _exit(10 - fd);
        char* p = mmap(0, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) _exit(20);
        if (strcmp(p, "hello from parent")) _exit(21);
        strcpy(p + 32768, "hello from child");
        _exit(0);
    }
    close(sv[1]);
    int fd = memfd_create("nvx-scm", MFD_CLOEXEC); if (fd < 0) FAILF("memfd_create: %s", strerror(errno));
    if (ftruncate(fd, 65536)) FAILF("ftruncate: %s", strerror(errno));
    char* p = mmap(0, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    strcpy(p, "hello from parent");
    if (send_fd(sv[0], fd, "shm") < 0) FAILF("sendmsg SCM_RIGHTS: %s", strerror(errno));
    int st; waitpid(c, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st)) {
        int e = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        FAILF("child: %s", e == 11 ? "no fd arrived with the message" : e == 12 ? "recvmsg failed" : e == 20 ? "mmap of the passed fd failed" : e == 21 ? "passed fd shows different memory" : "crashed");
    }
    if (strcmp(p + 32768, "hello from child")) FAILF("child's write through the passed fd is not visible");
    close(sv[0]); return 1;
}

static int t_devshm(void) {
    char nm[64]; snprintf(nm, sizeof nm, "/dev/shm/org.mozilla.ipc.%d.test", getpid());
    int fd = open(nm, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) FAILF("open %s: %s", nm, strerror(errno));
    unlink(nm);
    if (ftruncate(fd, 262144)) FAILF("ftruncate: %s", strerror(errno));
    int* p = mmap(0, 262144, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) FAILF("mmap: %s", strerror(errno));
    pid_t c = fork(); if (c == 0) { p[60000] = 4242; _exit(0); }
    int st; waitpid(c, &st, 0);
    if (p[60000] != 4242) FAILF("child's write not visible through a /dev/shm file");
    return 1;
}

static int t_big_shm(void) {
    size_t sz = 64u << 20;
    int fd = memfd_create("nvx-big", MFD_CLOEXEC); if (fd < 0) FAILF("memfd_create: %s", strerror(errno));
    if (ftruncate(fd, sz)) FAILF("ftruncate 64 MB: %s", strerror(errno));
    unsigned char* p = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) FAILF("mmap 64 MB: %s", strerror(errno));
    pid_t c = fork();
    if (c == 0) { unsigned char* q = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); if (q == MAP_FAILED) _exit(9); for (size_t i = 0; i < sz; i += 4096) q[i] = (unsigned char)(i >> 12); _exit(0); }
    int st; waitpid(c, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st)) FAILF("child could not map 64 MB (status %d)", st);
    for (size_t i = 0; i < sz; i += 4096) if (p[i] != (unsigned char)(i >> 12)) FAILF("page %zu differs", i >> 12);
    return 1;
}

static int t_pshared_mutex(void) {
    struct sh { pthread_mutex_t m; pthread_cond_t cv; int v; }* s = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (s == MAP_FAILED) FAILF("mmap anonymous shared: %s", strerror(errno));
    pthread_mutexattr_t ma; pthread_mutexattr_init(&ma); pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED); pthread_mutex_init(&s->m, &ma);
    pthread_condattr_t ca; pthread_condattr_init(&ca); pthread_condattr_setpshared(&ca, PTHREAD_PROCESS_SHARED); pthread_cond_init(&s->cv, &ca);
    s->v = 0;
    pid_t c = fork();
    if (c == 0) { for (int i = 0; i < 2000; i++) { pthread_mutex_lock(&s->m); s->v++; pthread_mutex_unlock(&s->m); } pthread_mutex_lock(&s->m); s->v += 1000000; pthread_cond_signal(&s->cv); pthread_mutex_unlock(&s->m); _exit(0); }
    for (int i = 0; i < 2000; i++) { pthread_mutex_lock(&s->m); s->v++; pthread_mutex_unlock(&s->m); }
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 20;
    pthread_mutex_lock(&s->m); int r = 0; while (s->v < 1000000 && r == 0) r = pthread_cond_timedwait(&s->cv, &s->m, &ts); int v = s->v; pthread_mutex_unlock(&s->m);
    int st; waitpid(c, &st, 0);
    if (r) FAILF("condition variable wakeup across processes timed out");
    if (v != 1004000) FAILF("counter %d, expected 1004000 (lost updates)", v);
    return 1;
}

static int t_sem(void) {
    sem_t* s = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sem_init(s, 1, 0)) FAILF("sem_init: %s", strerror(errno));
    pid_t c = fork(); if (c == 0) { usleep(100000); sem_post(s); _exit(0); }
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 10;
    int r = sem_timedwait(s, &ts); int e = errno;
    int st; waitpid(c, &st, 0);
    if (r) FAILF("sem_timedwait: %s", strerror(e));
    return 1;
}

static int t_spawn(const char* self) {
    int sv[2]; if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) FAILF("socketpair: %s", strerror(errno));
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], 7);
    char* av[] = { (char*)self, (char*)"--child", 0 };
    pid_t c; int r = posix_spawn(&c, self, &fa, 0, av, environ);
    if (r) FAILF("posix_spawn: %s", strerror(r));
    close(sv[1]);
    char b[64] = ""; struct pollfd pf = { sv[0], POLLIN, 0 };
    if (poll(&pf, 1, 15000) <= 0) FAILF("no reply from the spawned child within 15 s");
    int n = (int)read(sv[0], b, sizeof b - 1); if (n > 0) b[n] = 0;
    int st; waitpid(c, &st, 0);
    if (strcmp(b, "child ok")) FAILF("child replied '%s'", b);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 3) FAILF("child exit status %d", st);
    return 1;
}

static int t_epoll(void) {
    int sv[2]; if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv)) FAILF("socketpair: %s", strerror(errno));
    int ep = epoll_create1(EPOLL_CLOEXEC); if (ep < 0) FAILF("epoll_create1: %s", strerror(errno));
    int ev = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC); if (ev < 0) FAILF("eventfd: %s", strerror(errno));
    struct epoll_event e = { EPOLLIN, { .fd = sv[0] } }; if (epoll_ctl(ep, EPOLL_CTL_ADD, sv[0], &e)) FAILF("epoll_ctl socket: %s", strerror(errno));
    struct epoll_event e2 = { EPOLLIN, { .fd = ev } }; if (epoll_ctl(ep, EPOLL_CTL_ADD, ev, &e2)) FAILF("epoll_ctl eventfd: %s", strerror(errno));
    pid_t c = fork(); if (c == 0) { usleep(50000); if (write(sv[1], "x", 1) != 1) _exit(1); _exit(0); }
    struct epoll_event out[4]; int n = epoll_wait(ep, out, 4, 5000);
    int st; waitpid(c, &st, 0);
    if (n != 1 || out[0].data.fd != sv[0]) FAILF("epoll_wait returned %d instead of waking on the socket", n);
    char x; if (read(sv[0], &x, 1) != 1) FAILF("read after wakeup failed");
    uint64_t one = 1; if (write(ev, &one, 8) != 8) FAILF("eventfd write failed");
    n = epoll_wait(ep, out, 4, 1000);
    if (n != 1 || out[0].data.fd != ev) FAILF("eventfd did not wake epoll (%d)", n);
    return 1;
}

static volatile int got_chld;
static void on_chld(int s) { (void)s; got_chld = 1; }
static int t_sigchld(void) {
    got_chld = 0;
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_chld; sigaction(SIGCHLD, &sa, 0);
    pid_t c = fork(); if (c == 0) _exit(0);
    for (int i = 0; i < 200 && !got_chld; i++) usleep(10000);
    int st; waitpid(c, &st, 0); signal(SIGCHLD, SIG_DFL);
    if (!got_chld) FAILF("SIGCHLD was not delivered");
    return 1;
}

static void* slow_thread(void* a) { (void)a; usleep(200000); return 0; }
static int t_threads_fork(void) {
    pthread_t t; pthread_create(&t, 0, slow_thread, 0);
    pid_t c = fork(); if (c == 0) { char* av[] = { (char*)"/bin/sh", (char*)"-c", (char*)"exit 5", 0 }; execve("/bin/sh", av, environ); _exit(99); }
    int st; waitpid(c, &st, 0); pthread_join(t, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 5) FAILF("fork+exec from a threaded process gave status %d", st);
    return 1;
}

static int t_pdeathsig(void) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL)) FAILF("prctl PR_SET_PDEATHSIG: %s", strerror(errno));
    prctl(PR_SET_PDEATHSIG, 0);
    return 1;
}

static int t_seqpacket(void) {
    int sv[2]; if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv)) FAILF("socketpair SOCK_SEQPACKET: %s", strerror(errno));
    if (write(sv[0], "abc", 3) != 3 || write(sv[0], "defg", 4) != 4) FAILF("write failed");
    char b[16]; int a = (int)read(sv[1], b, 16); int d = (int)read(sv[1], b, 16);
    if (a != 3 || d != 4) FAILF("message boundaries not kept (%d,%d)", a, d);
    return 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--child")) { if (write(7, "child ok", 8) != 8) return 1; return 3; }
    char self[256]; ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1); if (n > 0) self[n] = 0; else snprintf(self, sizeof self, "%s", argv[0]);
    printf("Firefox multi-process readiness test\n");
    res("memfd shared across fork", t_memfd_fork());
    res("shared memory passed over a socket (SCM_RIGHTS)", t_scm_rights());
    res("/dev/shm shared memory files", t_devshm());
    res("64 MB shared buffer in two processes", t_big_shm());
    res("cross-process mutex + condition variable", t_pshared_mutex());
    res("cross-process semaphore", t_sem());
    res("posix_spawn with fd remapping", t_spawn(self));
    res("epoll + eventfd across processes", t_epoll());
    res("SIGCHLD delivery", t_sigchld());
    res("fork+exec from a threaded process", t_threads_fork());
    res("parent-death signal (prctl)", t_pdeathsig());
    res("SOCK_SEQPACKET socketpair", t_seqpacket());
    printf("%d passed, %d failed\n", npass, nfail);
    return nfail ? 1 : 0;
}
