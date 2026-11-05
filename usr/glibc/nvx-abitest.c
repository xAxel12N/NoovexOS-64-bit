#define _GNU_SOURCE
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <linux/capability.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <linux/futex.h>

static int pass, total;
static void res(const char* name, int ok, const char* detail) { total++; if (ok) pass++; printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail && *detail ? " - " : "", detail ? detail : ""); }

static volatile int alarms;
static void on_alrm(int s) { (void)s; alarms++; }

static int send_fds(int s, const int* fds, int n, const char* msg, int ml) {
    char cbuf[CMSG_SPACE(sizeof(int) * 64)]; memset(cbuf, 0, sizeof cbuf);
    struct iovec io = { (void*)msg, (size_t)ml }; struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = CMSG_SPACE(sizeof(int) * n);
    struct cmsghdr* c = CMSG_FIRSTHDR(&m); c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int) * n);
    memcpy(CMSG_DATA(c), fds, sizeof(int) * n); return (int)sendmsg(s, &m, 0); }
static int recv_fds(int s, int* fds, int max, char* msg, int ml) {
    char cbuf[CMSG_SPACE(sizeof(int) * 64)]; struct iovec io = { msg, (size_t)ml }; struct msghdr m; memset(&m, 0, sizeof m);
    m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = cbuf; m.msg_controllen = sizeof cbuf;
    int r = (int)recvmsg(s, &m, 0); if (r < 0) return -1; int n = 0;
    for (struct cmsghdr* c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) { int k = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int)); for (int i = 0; i < k && n < max; i++) memcpy(&fds[n++], CMSG_DATA(c) + i * sizeof(int), sizeof(int)); }
    return n; }
static long fx(volatile int* a, int op, int v, const struct timespec* t) { return syscall(SYS_futex, a, op, v, t, 0, 0); }
static int rd_until(int fd, char* b, int cap, const char* want, int ms) {
    int n = 0; b[0] = 0;
    for (int t = 0; t < ms / 10 && n < cap - 1; t++) { struct pollfd q = { fd, POLLIN, 0 }; if (poll(&q, 1, 10) > 0) { int k = (int)read(fd, b + n, (size_t)(cap - 1 - n)); if (k <= 0) break; n += k; b[n] = 0; if (strstr(b, want)) break; } }
    return n; }
static void pty_tests(void) {
    char d[256], b[256];
    int m = posix_openpt(O_RDWR | O_NOCTTY); int g = m >= 0 ? grantpt(m) : -1; int u = m >= 0 ? unlockpt(m) : -1; char* nm = m >= 0 ? ptsname(m) : NULL; char name[64]; snprintf(name, sizeof name, "%s", nm ? nm : "-");
    int s = nm ? open(name, O_RDWR | O_NOCTTY) : -1; struct stat st; int sst = nm ? stat(name, &st) : -1; int tty = s >= 0 ? isatty(s) : 0; char tn[64] = "-"; if (s >= 0 && ttyname_r(s, tn, sizeof tn)) snprintf(tn, sizeof tn, "err");
    snprintf(d, sizeof d, "master %d grant %d unlock %d name %s slave %d chr %d isatty %d ttyname %s", m, g, u, name, s, sst == 0 && S_ISCHR(st.st_mode), tty, tn);
    res("pseudo-terminal opens through /dev/ptmx and /dev/pts", m >= 0 && g == 0 && u == 0 && s >= 0 && sst == 0 && S_ISCHR(st.st_mode) && tty && !strcmp(tn, name), d);
    if (m < 0 || s < 0) return;
    if (write(m, "hello\r", 6) != 6) printf("short write\n");
    char in[64] = {0}; struct pollfd q = { s, POLLIN, 0 }; int pr = poll(&q, 1, 1000); int ni = pr > 0 ? (int)read(s, in, sizeof in - 1) : -1; if (ni > 0) in[ni] = 0;
    int ne = rd_until(m, b, sizeof b, "hello\r\n", 1000);
    snprintf(d, sizeof d, "slave got %d '%s' echo %d", ni, ni == 6 ? "hello\\n" : in, ne);
    res("pty line mode: Enter becomes newline, typed text is echoed", ni == 6 && !memcmp(in, "hello\n", 6) && strstr(b, "hello\r\n") != NULL, d);
    if (write(s, "x\n", 2) != 2) printf("short write\n"); int no = rd_until(m, b, sizeof b, "x\r\n", 1000);
    struct winsize w = { 40, 120, 0, 0 }, w2 = { 0, 0, 0, 0 }; ioctl(m, TIOCSWINSZ, &w); ioctl(s, TIOCGWINSZ, &w2);
    snprintf(d, sizeof d, "out %d '%s' size %dx%d", no, strstr(b, "x\r\n") ? "x\\r\\n" : "?", w2.ws_col, w2.ws_row);
    res("pty output turns newline into CR LF, window size is shared", strstr(b, "x\r\n") != NULL && w2.ws_col == 120 && w2.ws_row == 40, d);
    pid_t c = fork();
    if (c == 0) { setsid(); int t = open(name, O_RDWR); if (t < 0) _exit(3); ioctl(t, TIOCSCTTY, 0); if (write(t, "ready\n", 6) != 6) _exit(4); for (int i = 0; i < 300; i++) usleep(10000); _exit(0); }
    int nr = rd_until(m, b, sizeof b, "ready", 3000);
    close(s);
    if (write(m, "\003", 1) != 1) printf("short write\n");
    int stc = 0; int wr = -1; for (int i = 0; i < 300; i++) { wr = (int)waitpid(c, &stc, WNOHANG); if (wr == c) break; usleep(10000); }
    char tail[64]; int k = 0, eio = 0; for (int i = 0; i < 50; i++) { int z = (int)read(m, tail, sizeof tail); if (z < 0) { eio = errno == EIO; break; } if (z == 0) break; k += z; }
    snprintf(d, sizeof d, "ready %d waited %d signal %d master-after-exit %s", nr, wr == c, WIFSIGNALED(stc) ? WTERMSIG(stc) : 0, eio ? "EIO" : "no EIO");
    res("Ctrl+C on a pty stops the program, the terminal sees it exit", nr > 0 && wr == c && WIFSIGNALED(stc) && WTERMSIG(stc) == SIGINT && eio, d);
    close(m);
}
static void procfiles_test(void) {
    char d[256], b[4096]; int okstat = 0, okps = 0, okpos = 0, okcmd = 0;
    FILE* f = fopen("/proc/self/stat", "r"); if (f) { if (fgets(b, sizeof b, f)) { int pid = 0; char comm[64] = ""; char st = 0; if (sscanf(b, "%d (%63[^)]) %c", &pid, comm, &st) == 3 && pid == getpid() && !strncmp(comm, program_invocation_short_name, 15)) okstat = 1; } fclose(f); }
    f = fopen("/proc/stat", "r"); if (f) { if (fgets(b, sizeof b, f) && !strncmp(b, "cpu ", 4)) okps = 1; fclose(f); }
    f = fopen("/sys/devices/system/cpu/possible", "r"); if (f) { if (fgets(b, sizeof b, f) && b[0] == '0') okpos = 1; fclose(f); }
    f = fopen("/proc/self/cmdline", "r"); if (f) { size_t n = fread(b, 1, sizeof b - 1, f); b[n] = 0; if (n > 0 && strstr(b, program_invocation_short_name)) okcmd = 1; fclose(f); }
    snprintf(d, sizeof d, "self/stat %d stat %d cpu/possible %d cmdline %d", okstat, okps, okpos, okcmd);
    res("/proc/self/stat, /proc/stat, /proc/self/cmdline and /sys/devices/system/cpu/possible", okstat && okps && okpos && okcmd, d);
}
static void cpuinfo_test(void) {
    char d[256]; FILE* f = fopen("/proc/cpuinfo", "r"); int blocks = 0, flags = 0, ok_fields = 1, has_sse2 = 1, has_pni = 1, want_pni = __builtin_cpu_supports("sse3"); char ln[4096];
    const char* need[] = { "vendor_id\t: ", "cpu family\t: ", "model\t\t: ", "model name\t: ", "stepping\t: ", "cpu MHz\t\t: ", "siblings\t: ", "cpu cores\t: ", "flags\t\t: " }; int seen = 0;
    while (f && fgets(ln, sizeof ln, f)) {
        if (!strncmp(ln, "processor\t: ", 12)) { if (blocks && seen != 511) ok_fields = 0; blocks++; seen = 0; }
        for (int i = 0; i < 9; i++) if (!strncmp(ln, need[i], strlen(need[i]))) seen |= 1 << i;
        if (!strncmp(ln, "flags\t\t: ", 9)) { flags++; char* t = strchr(ln, '\n'); if (t) *t = ' '; if (!strstr(ln, " sse2 ")) has_sse2 = 0; if (!strstr(ln, " pni ")) has_pni = 0; }
    }
    if (blocks && seen != 511) ok_fields = 0;
    if (f) fclose(f);
    long on = sysconf(_SC_NPROCESSORS_ONLN);
    snprintf(d, sizeof d, "blocks %d online %ld fields %d sse2 %d pni %d (cpu has sse3 %d)", blocks, on, ok_fields, has_sse2, has_pni, want_pni ? 1 : 0);
    res("/proc/cpuinfo has one Linux-format block per online CPU with real flags", blocks >= 1 && blocks == on && flags == blocks && ok_fields && has_sse2 && (!want_pni || has_pni), d);
}
static void mp_tests(void) {
    char d[256];
    {
        int sv[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
        pid_t c = fork();
        if (c == 0) { close(sv[0]); int fd = -1; char m[4]; int n = recv_fds(sv[1], &fd, 1, m, 4); if (n != 1) _exit(2);
            volatile int* p = mmap(0, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); if (p == MAP_FAILED) _exit(3);
            for (int i = 1; i < 1000; i++) p[i] = i * 7;
            __atomic_store_n(&p[0], 1, __ATOMIC_SEQ_CST); fx(p, FUTEX_WAKE, 1, 0); _exit(0); }
        close(sv[1]);
        int mf = (int)syscall(SYS_memfd_create, "abitest", 0); ftruncate(mf, 65536);
        volatile int* p = mmap(0, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, mf, 0);
        send_fds(sv[0], &mf, 1, "fd", 2);
        struct timespec t = { 3, 0 }; while (!__atomic_load_n(&p[0], __ATOMIC_SEQ_CST)) { if (fx(p, FUTEX_WAIT, 0, &t) < 0 && errno == ETIMEDOUT) break; }
        int ok = p[0] == 1; for (int i = 1; i < 1000; i++) if (p[i] != i * 7) ok = 0;
        int st = 0; waitpid(c, &st, 0);
        snprintf(d, sizeof d, "flag %d child %d", p[0], WEXITSTATUS(st));
        res("memfd sent over a socket is shared, futex wakes the other process", ok && st == 0, d);
        close(sv[0]); close(mf);
    }
    {
        volatile int* p = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        volatile int* q = mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        pid_t c = fork(); if (c == 0) { q[1500] = 77; p[1] = 42; __atomic_store_n(&p[0], 1, __ATOMIC_SEQ_CST); fx(p, FUTEX_WAKE, 1, 0); _exit(0); }
        struct timespec t = { 3, 0 }; while (!__atomic_load_n(&p[0], __ATOMIC_SEQ_CST)) { if (fx(p, FUTEX_WAIT, 0, &t) < 0 && errno == ETIMEDOUT) break; }
        int st; waitpid(c, &st, 0); snprintf(d, sizeof d, "%d %d", p[1], q[1500]);
        res("MAP_SHARED|MAP_ANONYMOUS memory stays shared after fork", p[1] == 42 && q[1500] == 77, d);
    }
    {
        char nm[64]; snprintf(nm, sizeof nm, "/dev/shm/.nvx-abitest-%d", (int)getpid());
        int fd = open(nm, O_RDWR | O_CREAT | O_EXCL, 0600); unlink(nm); ftruncate(fd, 4096);
        volatile int* p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        pid_t c = fork(); if (c == 0) { volatile int* q = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); q[3] = 99; _exit(0); }
        int st; waitpid(c, &st, 0); snprintf(d, sizeof d, "fd %d value %d", fd, fd >= 0 && p != MAP_FAILED ? p[3] : -1);
        res("/dev/shm file mapped in two processes is shared", fd >= 0 && p != MAP_FAILED && p[3] == 99, d);
        close(fd);
    }
    {
        int sv[2]; socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv);
        int pf[40]; for (int i = 0; i < 20; i++) pipe(pf + i * 2);
        int give[20]; for (int i = 0; i < 20; i++) give[i] = pf[i * 2];
        send_fds(sv[0], give, 20, "twenty", 6); send(sv[0], "next", 4, 0);
        int got[64]; char m[16] = {0}; int n = recv_fds(sv[1], got, 64, m, 16);
        char m2[16] = {0}; int r2 = (int)recv(sv[1], m2, 16, 0);
        int ok = n == 20 && !strcmp(m, "twenty") && r2 == 4;
        for (int i = 0; ok && i < 20; i++) { if (write(pf[i * 2 + 1], "x", 1) != 1) ok = 0; char b; if (read(got[i], &b, 1) != 1 || b != 'x') ok = 0; }
        snprintf(d, sizeof d, "fds %d msg '%s' next %d", n, m, r2);
        res("20 descriptors in one SEQPACKET message, boundaries kept", ok, d);
        for (int i = 0; i < 40; i++) close(pf[i]);
        for (int i = 0; i < n; i++) close(got[i]);
        close(sv[0]); close(sv[1]);
    }
    {
        int sv[2]; socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv); int ev = eventfd(0, EFD_NONBLOCK);
        pid_t c = fork();
        if (c == 0) { int ep = epoll_create1(0); struct epoll_event e = { EPOLLIN, { .u32 = 1 } }, e2 = { EPOLLIN, { .u32 = 2 } };
            epoll_ctl(ep, EPOLL_CTL_ADD, sv[1], &e); epoll_ctl(ep, EPOLL_CTL_ADD, ev, &e2);
            int seen = 0; for (int k = 0; k < 20 && seen != 3; k++) { struct epoll_event o[4]; int n = epoll_wait(ep, o, 4, 200);
                for (int i = 0; i < n; i++) { seen |= (int)o[i].data.u32; if (o[i].data.u32 == 1) { char b[8]; read(sv[1], b, 8); } else { uint64_t v; read(ev, &v, 8); } } }
            _exit(seen == 3 ? 0 : 1); }
        usleep(100000); write(sv[0], "ping", 4); uint64_t one = 1; write(ev, &one, 8);
        int st; waitpid(c, &st, 0); snprintf(d, sizeof d, "child %d", WEXITSTATUS(st));
        res("epoll in a forked child sees socket and eventfd from the parent", WIFEXITED(st) && WEXITSTATUS(st) == 0, d);
        close(sv[0]); close(sv[1]); close(ev);
    }
    {
        int ep = epoll_create1(0); int a[2]; pipe(a); struct epoll_event e = { EPOLLIN, { .u32 = 5 } };
        int r1 = epoll_ctl(ep, EPOLL_CTL_ADD, a[0], &e); close(a[0]); int b[2]; pipe(b); int same = b[0] == a[0];
        int r2 = epoll_ctl(ep, EPOLL_CTL_ADD, b[0], &e); write(b[1], "z", 1); struct epoll_event o; int n = epoll_wait(ep, &o, 1, 500);
        snprintf(d, sizeof d, "add %d reuse %d add-again %d wait %d", r1, same, r2, n);
        res("epoll forgets a closed descriptor whose number is reused", r1 == 0 && r2 == 0 && n == 1, d);
        close(ep); close(a[1]); close(b[0]); close(b[1]);
    }
}
int main(void) {
    char d[256];
    const char* dir = "/tmp/nvx-abitest";
    mkdir(dir, 0755);
    {
        int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        int wd = fd >= 0 ? inotify_add_watch(fd, dir, IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO) : -1;
        int f = open("/tmp/nvx-abitest/a.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (f >= 0) { if (write(f, "hi", 2) != 2) printf("short write\n"); close(f); }
        rename("/tmp/nvx-abitest/a.txt", "/tmp/nvx-abitest/b.txt");
        unlink("/tmp/nvx-abitest/b.txt");
        char buf[4096] __attribute__((aligned(8)));
        int n = fd >= 0 ? (int)read(fd, buf, sizeof buf) : -1;
        unsigned seen = 0; unsigned ck_from = 0, ck_to = 1; int names = 0;
        for (int o = 0; o < n;) { struct inotify_event* e = (struct inotify_event*)(buf + o); seen |= e->mask; if (e->mask & IN_MOVED_FROM) ck_from = e->cookie; if (e->mask & IN_MOVED_TO) ck_to = e->cookie; if (e->len && (!strcmp(e->name, "a.txt") || !strcmp(e->name, "b.txt"))) names++; o += (int)sizeof(struct inotify_event) + (int)e->len; }
        unsigned want = IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
        snprintf(d, sizeof d, "fd %d wd %d bytes %d mask %x names %d", fd, wd, n, seen, names);
        res("inotify create/modify/close/move/delete", fd >= 0 && wd > 0 && (seen & want) == want && ck_from == ck_to && names >= 6, d);
        struct pollfd p = { fd, POLLIN, 0 };
        res("inotify empty queue is not readable", poll(&p, 1, 0) == 0 && read(fd, buf, sizeof buf) < 0 && errno == EAGAIN, "");
        res("inotify_rm_watch", inotify_rm_watch(fd, wd) == 0 && inotify_rm_watch(fd, wd) < 0, "");
        if (fd >= 0) close(fd);
    }
    {
        sigset_t m; sigemptyset(&m); sigaddset(&m, SIGUSR1); sigaddset(&m, SIGUSR2);
        sigprocmask(SIG_BLOCK, &m, 0);
        int fd = signalfd(-1, &m, SFD_NONBLOCK);
        struct pollfd p = { fd, POLLIN, 0 };
        int before = poll(&p, 1, 0);
        raise(SIGUSR2);
        int after = poll(&p, 1, 0);
        struct signalfd_siginfo si; memset(&si, 0, sizeof si);
        int n = (int)read(fd, &si, sizeof si);
        int n2 = (int)read(fd, &si, sizeof si);
        snprintf(d, sizeof d, "read %d signo %u", n, si.ssi_signo);
        res("signalfd", fd >= 0 && before == 0 && after == 1 && n == (int)sizeof si && si.ssi_signo == SIGUSR2 && n2 < 0 && errno == EAGAIN, d);
        close(fd);
        sigprocmask(SIG_UNBLOCK, &m, 0);
    }
    {
        pid_t c = fork();
        if (c == 0) _exit(7);
        siginfo_t si; memset(&si, 0, sizeof si);
        int r = waitid(P_PID, (id_t)c, &si, WEXITED);
        snprintf(d, sizeof d, "r %d code %d status %d pid %d", r, si.si_code, si.si_status, si.si_pid);
        res("waitid", r == 0 && si.si_code == CLD_EXITED && si.si_status == 7 && si.si_pid == c, d);
    }
    {
        int fd = open("/tmp/nvx-abitest/lock", O_CREAT | O_RDWR, 0644);
        int a = flock(fd, LOCK_EX);
        struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10 };
        int b = fcntl(fd, F_SETLK, &fl);
        pid_t c = fork();
        if (c == 0) {
            int f2 = open("/tmp/nvx-abitest/lock", O_RDWR);
            int r = flock(f2, LOCK_EX | LOCK_NB);
            int e = errno;
            struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 10 };
            int r2 = fcntl(f2, F_SETLK, &fl);
            int e2 = errno;
            struct flock q = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 100, .l_len = 1 };
            fcntl(f2, F_GETLK, &q);
            _exit((r < 0 && e == EWOULDBLOCK ? 1 : 0) | (r2 < 0 && (e2 == EAGAIN || e2 == EACCES) ? 2 : 0) | (q.l_type == F_UNLCK ? 4 : 0));
        }
        int st = 0; waitpid(c, &st, 0);
        int child = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        snprintf(d, sizeof d, "flock %d setlk %d child %d", a, b, child);
        res("flock and fcntl record locks", a == 0 && b == 0 && child == 7, d);
        flock(fd, LOCK_UN);
        pid_t c2 = fork();
        if (c2 == 0) { int f2 = open("/tmp/nvx-abitest/lock", O_RDWR); _exit(flock(f2, LOCK_EX | LOCK_NB) == 0 ? 0 : 1); }
        waitpid(c2, &st, 0);
        res("flock released after LOCK_UN", WIFEXITED(st) && WEXITSTATUS(st) == 0, "");
        close(fd);
    }
    {
        signal(SIGALRM, on_alrm);
        timer_t t; struct sigevent ev; memset(&ev, 0, sizeof ev); ev.sigev_notify = SIGEV_SIGNAL; ev.sigev_signo = SIGALRM;
        int r = timer_create(CLOCK_MONOTONIC, &ev, &t);
        struct itimerspec its = { { 0, 50000000 }, { 0, 50000000 } };
        int r2 = r == 0 ? timer_settime(t, 0, &its, 0) : -1;
        struct timespec ts = { 0, 20000000 };
        for (int i = 0; i < 30 && alarms < 4; i++) nanosleep(&ts, 0);
        struct itimerspec cur; int r3 = r == 0 ? timer_gettime(t, &cur) : -1;
        int r4 = r == 0 ? timer_delete(t) : -1;
        snprintf(d, sizeof d, "create %d set %d alarms %d get %d del %d", r, r2, alarms, r3, r4);
        res("timer_create periodic SIGALRM", r == 0 && r2 == 0 && alarms >= 4 && r3 == 0 && cur.it_interval.tv_nsec == 50000000 && r4 == 0, d);
    }
    {
        int fd = open("/tmp/nvx-abitest/v", O_CREAT | O_RDWR | O_TRUNC, 0644);
        char a[] = "hello ", b[] = "world";
        struct iovec w[2] = { { a, 6 }, { b, 5 } };
        ssize_t n = pwritev(fd, w, 2, 3);
        char x[6] = { 0 }, y[6] = { 0 };
        struct iovec rv[2] = { { x, 5 }, { y, 5 } };
        ssize_t m = preadv(fd, rv, 2, 4);
        snprintf(d, sizeof d, "wrote %zd read %zd '%s' '%s'", n, m, x, y);
        res("preadv/pwritev", n == 11 && m == 10 && !memcmp(x, "ello ", 5) && !memcmp(y, "world", 5), d);
        close(fd);
    }
    {
        int sv[2]; int r = socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
        struct mmsghdr mm[3]; struct iovec io[3]; char t[3][8] = { "one", "two", "three" };
        memset(mm, 0, sizeof mm);
        for (int i = 0; i < 3; i++) { io[i].iov_base = t[i]; io[i].iov_len = strlen(t[i]); mm[i].msg_hdr.msg_iov = &io[i]; mm[i].msg_hdr.msg_iovlen = 1; }
        int s = r == 0 ? sendmmsg(sv[0], mm, 3, 0) : -1;
        struct mmsghdr rm[3]; struct iovec ri[3]; char rb[3][16];
        memset(rm, 0, sizeof rm); memset(rb, 0, sizeof rb);
        for (int i = 0; i < 3; i++) { ri[i].iov_base = rb[i]; ri[i].iov_len = 15; rm[i].msg_hdr.msg_iov = &ri[i]; rm[i].msg_hdr.msg_iovlen = 1; }
        int g = r == 0 ? recvmmsg(sv[1], rm, 3, 0, 0) : -1;
        snprintf(d, sizeof d, "sent %d got %d '%s' '%s' '%s'", s, g, rb[0], rb[1], rb[2]);
        res("sendmmsg/recvmmsg", s == 3 && g == 3 && !strcmp(rb[2], "three") && rm[1].msg_len == 3, d);
    }
    {
        int fd = open("/tmp/nvx-abitest", O_RDONLY | O_DIRECTORY);
        char buf[2048];
        long n = syscall(SYS_getdents, fd, buf, sizeof buf);
        int dots = 0;
        for (long o = 0; o < n;) { unsigned short rl = *(unsigned short*)(buf + o + 16); const char* nm = buf + o + 18; if (!strcmp(nm, ".") || !strcmp(nm, "..")) { if (buf[o + rl - 1] == DT_DIR) dots++; } o += rl; }
        snprintf(d, sizeof d, "bytes %ld dot entries %d", n, dots);
        res("getdents (old syscall)", n > 0 && dots == 2, d);
        close(fd);
    }
    {
        struct __user_cap_header_struct h = { _LINUX_CAPABILITY_VERSION_3, 0 };
        struct __user_cap_data_struct cd[2];
        int r = (int)syscall(SYS_capget, &h, cd);
        res("capget", r == 0, "");
        struct timeval tv[2] = { { 1000000000, 0 }, { 1100000000, 0 } };
        int fd = open("/tmp/nvx-abitest/t", O_CREAT | O_WRONLY, 0644); close(fd);
        int u = utimes("/tmp/nvx-abitest/t", tv);
        struct stat st; stat("/tmp/nvx-abitest/t", &st);
        snprintf(d, sizeof d, "utimes %d mtime %ld", u, (long)st.st_mtime);
        res("utimes", u == 0 && st.st_mtime == 1100000000, d);
    }
    cpuinfo_test();
    procfiles_test();
    pty_tests();
    mp_tests();
    printf("%d of %d ABI checks passed\n", pass, total);
    return pass == total ? 0 : 1;
}
