#define _GNU_SOURCE
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/signalfd.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/stat.h>

#ifndef AF_NETLINK
#define AF_NETLINK 16
#endif
#define NETLINK_ROUTE 0
#define NLM_F_REQUEST 1
#define NLM_F_DUMP 0x300
#define NLMSG_DONE 3
#define RTM_NEWLINK 16
#define RTM_GETLINK 18
struct nlmsghdr { uint32_t nlmsg_len; uint16_t nlmsg_type, nlmsg_flags; uint32_t nlmsg_seq, nlmsg_pid; };
struct sockaddr_nl { unsigned short nl_family, nl_pad; uint32_t nl_pid, nl_groups; };
struct rtgenmsg { unsigned char rtgen_family; };
#define NLMSG_ALIGN(l) (((l) + 3) & ~3)
#define NLMSG_OK(h, l) ((l) >= (int)sizeof(struct nlmsghdr) && (h)->nlmsg_len >= sizeof(struct nlmsghdr) && (h)->nlmsg_len <= (unsigned)(l))
#define NLMSG_NEXT(h, l) ((l) -= NLMSG_ALIGN((h)->nlmsg_len), (struct nlmsghdr*)((char*)(h) + NLMSG_ALIGN((h)->nlmsg_len)))

static int pass, total, skipped;
static void res(const char* name, int ok, const char* detail) {
    total++; if (ok) pass++;
    printf("abi2: %s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail && *detail ? " - " : "", detail ? detail : "");
}
static void skip(const char* name, const char* why) { skipped++; printf("abi2: SKIP %s - %s\n", name, why); }

static volatile int rt_n, rt_vals[16], rt_codes[16], rt_sigs[16];
static void on_rt(int s, siginfo_t* si, void* uc) { (void)uc; if (rt_n < 16) { rt_sigs[rt_n] = s; rt_vals[rt_n] = si->si_value.sival_int; rt_codes[rt_n] = si->si_code; rt_n++; } }
static volatile int thr_cb, thr_val;
static void on_timer_thread(union sigval v) { thr_val = v.sival_int; thr_cb++; }
static void sleep_ms(int ms) { struct timespec t = { ms / 1000, (ms % 1000) * 1000000L }; while (nanosleep(&t, &t) && errno == EINTR) {} }

static void rt_tests(void) {
    char d[200];
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_rt; sa.sa_flags = SA_SIGINFO; sigemptyset(&sa.sa_mask);
    int s2 = SIGRTMIN + 2, s3 = SIGRTMIN + 3, s4 = SIGRTMIN + 4;
    int a = sigaction(s2, &sa, 0); rt_n = 0;
    union sigval v; v.sival_int = 42;
    int q = sigqueue(getpid(), s2, v);
    snprintf(d, sizeof d, "sigaction %d sigqueue %d errno %d calls %d sig %d val %d code %d", a, q, q ? errno : 0, rt_n, rt_n ? rt_sigs[0] : 0, rt_n ? rt_vals[0] : 0, rt_n ? rt_codes[0] : 0);
    res("real-time signal with sigqueue value", a == 0 && q == 0 && rt_n == 1 && rt_sigs[0] == s2 && rt_vals[0] == 42 && rt_codes[0] == SI_QUEUE, d);

    sigaction(s3, &sa, 0); sigset_t m, old; sigemptyset(&m); sigaddset(&m, s3); sigprocmask(SIG_BLOCK, &m, &old); rt_n = 0;
    for (int i = 1; i <= 3; i++) { v.sival_int = i * 10; sigqueue(getpid(), s3, v); }
    int before = rt_n; sigprocmask(SIG_SETMASK, &old, 0);
    snprintf(d, sizeof d, "while blocked %d after %d values %d %d %d", before, rt_n, rt_vals[0], rt_vals[1], rt_vals[2]);
    res("real-time signals are queued (3 sent while blocked, 3 delivered in order)", before == 0 && rt_n == 3 && rt_vals[0] == 10 && rt_vals[1] == 20 && rt_vals[2] == 30, d);

    sigemptyset(&m); sigaddset(&m, s4); sigaddset(&m, SIGRTMAX); sigprocmask(SIG_BLOCK, &m, 0);
    v.sival_int = 7; sigqueue(getpid(), s4, v);
    sigset_t pend; sigemptyset(&pend); sigpending(&pend);
    int ispend = sigismember(&pend, s4);
    siginfo_t si; memset(&si, 0, sizeof si); struct timespec to = { 1, 0 };
    int w = sigtimedwait(&m, &si, &to);
    snprintf(d, sizeof d, "pending %d sigtimedwait %d val %d code %d pid %d", ispend, w, si.si_value.sival_int, si.si_code, si.si_pid);
    res("sigtimedwait takes a queued real-time signal with its value", ispend == 1 && w == s4 && si.si_value.sival_int == 7 && si.si_code == SI_QUEUE && si.si_pid == getpid(), d);

    v.sival_int = 64; int q64 = sigqueue(getpid(), SIGRTMAX, v); memset(&si, 0, sizeof si);
    int w64 = sigtimedwait(&m, &si, &to);
    snprintf(d, sizeof d, "SIGRTMAX %d sigqueue %d wait %d val %d", SIGRTMAX, q64, w64, si.si_value.sival_int);
    res("signal 64 (SIGRTMAX)", q64 == 0 && w64 == SIGRTMAX && si.si_value.sival_int == 64, d);

    sigset_t cur; sigprocmask(SIG_SETMASK, 0, &cur);
    res("sigprocmask keeps real-time bits", sigismember(&cur, s4) == 1 && sigismember(&cur, SIGRTMAX) == 1, "");
    sigprocmask(SIG_UNBLOCK, &m, 0);

    int sfd_sig = SIGRTMIN + 5; sigemptyset(&m); sigaddset(&m, sfd_sig); sigprocmask(SIG_BLOCK, &m, 0);
    int sfd = signalfd(-1, &m, SFD_NONBLOCK); v.sival_int = 99; sigqueue(getpid(), sfd_sig, v);
    struct signalfd_siginfo ssi; memset(&ssi, 0, sizeof ssi); int rn = sfd >= 0 ? (int)read(sfd, &ssi, sizeof ssi) : -1;
    snprintf(d, sizeof d, "signalfd %d read %d signo %u int %d code %d", sfd, rn, ssi.ssi_signo, ssi.ssi_int, ssi.ssi_code);
    res("signalfd reports a real-time signal and its value", rn == (int)sizeof ssi && (int)ssi.ssi_signo == sfd_sig && ssi.ssi_int == 99 && ssi.ssi_code == SI_QUEUE, d);
    if (sfd >= 0) close(sfd);
    sigprocmask(SIG_UNBLOCK, &m, 0);

    timer_t t; struct sigevent ev; memset(&ev, 0, sizeof ev); ev.sigev_notify = SIGEV_SIGNAL; ev.sigev_signo = SIGRTMIN + 6; ev.sigev_value.sival_int = 1234;
    sigaction(SIGRTMIN + 6, &sa, 0); rt_n = 0;
    int tc = timer_create(CLOCK_MONOTONIC, &ev, &t);
    struct itimerspec its; memset(&its, 0, sizeof its); its.it_value.tv_nsec = 50000000;
    int ts = tc == 0 ? timer_settime(t, 0, &its, 0) : -1;
    for (int i = 0; i < 40 && !rt_n; i++) sleep_ms(10);
    snprintf(d, sizeof d, "create %d settime %d calls %d val %d code %d", tc, ts, rt_n, rt_n ? rt_vals[0] : 0, rt_n ? rt_codes[0] : 0);
    res("POSIX timer sends a real-time signal with sigev_value (SI_TIMER)", tc == 0 && ts == 0 && rt_n == 1 && rt_vals[0] == 1234 && rt_codes[0] == SI_TIMER, d);
    if (tc == 0) timer_delete(t);

    memset(&ev, 0, sizeof ev); ev.sigev_notify = SIGEV_THREAD; ev.sigev_notify_function = on_timer_thread; ev.sigev_value.sival_int = 777; thr_cb = 0;
    tc = timer_create(CLOCK_MONOTONIC, &ev, &t);
    memset(&its, 0, sizeof its); its.it_value.tv_nsec = 50000000;
    ts = tc == 0 ? timer_settime(t, 0, &its, 0) : -1;
    for (int i = 0; i < 100 && !thr_cb; i++) sleep_ms(10);
    snprintf(d, sizeof d, "create %d errno %d settime %d callbacks %d value %d", tc, tc ? errno : 0, ts, thr_cb, thr_val);
    res("timer with SIGEV_THREAD calls the function in a thread", tc == 0 && ts == 0 && thr_cb == 1 && thr_val == 777, d);
    if (tc == 0) timer_delete(t);
}

static int got_thread_sig; static volatile int thr_tid;
static void* rt_thread(void* arg) { (void)arg; thr_tid = (int)syscall(SYS_gettid); sigset_t m; sigemptyset(&m); sigaddset(&m, SIGRTMIN + 7); siginfo_t si; struct timespec to = { 2, 0 }; int s = sigtimedwait(&m, &si, &to); got_thread_sig = s == SIGRTMIN + 7 ? si.si_value.sival_int : -1; return 0; }
static void thread_tests(void) {
    sigset_t m; sigemptyset(&m); sigaddset(&m, SIGRTMIN + 7); pthread_sigmask(SIG_BLOCK, &m, 0);
    pthread_t th; got_thread_sig = 0; pthread_create(&th, 0, rt_thread, 0); sleep_ms(50);
    siginfo_t qi; memset(&qi, 0, sizeof qi); qi.si_signo = SIGRTMIN + 7; qi.si_code = SI_QUEUE; qi.si_pid = getpid(); qi.si_uid = getuid(); qi.si_value.sival_int = 55; int r = (int)syscall(SYS_rt_tgsigqueueinfo, getpid(), thr_tid, SIGRTMIN + 7, &qi);
    pthread_join(th, 0);
    char d[100]; snprintf(d, sizeof d, "rt_tgsigqueueinfo %d thread got %d", r, got_thread_sig);
    res("rt_tgsigqueueinfo to one thread", r == 0 && got_thread_sig == 55, d);
    pthread_sigmask(SIG_UNBLOCK, &m, 0);
}

static void unix_dgram_tests(void) {
    char d[200];
    const char* pa = "/tmp/abi2-a.sock"; const char* pb = "/tmp/abi2-b.sock"; unlink(pa); unlink(pb);
    int a = socket(AF_UNIX, SOCK_DGRAM, 0), b = socket(AF_UNIX, SOCK_DGRAM, 0);
    struct sockaddr_un sa = { 0 }, sb = { 0 }; sa.sun_family = sb.sun_family = AF_UNIX; strcpy(sa.sun_path, pa); strcpy(sb.sun_path, pb);
    int ba = bind(a, (struct sockaddr*)&sa, sizeof sa), bb = bind(b, (struct sockaddr*)&sb, sizeof sb);
    int s1 = (int)sendto(b, "hello", 5, 0, (struct sockaddr*)&sa, sizeof sa);
    int s2 = (int)sendto(b, "world!!", 7, 0, (struct sockaddr*)&sa, sizeof sa);
    char buf[64]; struct sockaddr_un from = { 0 }; socklen_t fl = sizeof from;
    int r1 = (int)recvfrom(a, buf, sizeof buf, 0, (struct sockaddr*)&from, &fl); buf[r1 > 0 ? r1 : 0] = 0;
    int same = r1 == 5 && !memcmp(buf, "hello", 5) && from.sun_family == AF_UNIX && !strcmp(from.sun_path, pb);
    int r2 = (int)recv(a, buf, sizeof buf, 0);
    snprintf(d, sizeof d, "socket %d %d bind %d %d send %d %d recv %d (%s) from '%s' recv %d", a, b, ba, bb, s1, s2, r1, r1 > 0 ? "data" : "none", from.sun_path, r2);
    res("Unix datagram socket: sendto a bound path, recvfrom returns the sender", a >= 0 && b >= 0 && ba == 0 && bb == 0 && s1 == 5 && s2 == 7 && same && r2 == 7, d);

    int c = connect(b, (struct sockaddr*)&sa, sizeof sa);
    send(b, "0123456789", 10, 0);
    struct iovec io = { buf, 4 }; struct msghdr mh = { 0 }; mh.msg_iov = &io; mh.msg_iovlen = 1;
    int r3 = (int)recvmsg(a, &mh, 0);
    struct pollfd p = { a, POLLIN, 0 }; int pr = poll(&p, 1, 0);
    snprintf(d, sizeof d, "connect %d recvmsg %d flags %x poll after %d", c, r3, mh.msg_flags, pr);
    res("Unix datagram: connect + send, a short read truncates with MSG_TRUNC", c == 0 && r3 == 4 && (mh.msg_flags & MSG_TRUNC) && pr == 0, d);

    int x = socket(AF_UNIX, SOCK_DGRAM, 0); struct sockaddr_un ab = { 0 }; ab.sun_family = AF_UNIX; memcpy(ab.sun_path, "\0abi2-abstract", 14);
    int bx = bind(x, (struct sockaddr*)&ab, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 14));
    int u = socket(AF_UNIX, SOCK_DGRAM, 0);
    int su = (int)sendto(u, "abc", 3, 0, (struct sockaddr*)&ab, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 14));
    int rx = (int)recv(x, buf, sizeof buf, MSG_DONTWAIT);
    snprintf(d, sizeof d, "bind %d send %d recv %d", bx, su, rx);
    res("Unix datagram: abstract address, unbound sender", bx == 0 && su == 3 && rx == 3, d);

    struct sockaddr_un nob = { 0 }; nob.sun_family = AF_UNIX; strcpy(nob.sun_path, "/tmp/abi2-nobody.sock"); unlink(nob.sun_path);
    int sn = (int)sendto(u, "x", 1, 0, (struct sockaddr*)&nob, sizeof nob); int en = errno;
    snprintf(d, sizeof d, "sendto %d errno %d", sn, en);
    res("Unix datagram: sendto a missing path fails", sn < 0 && (en == ENOENT || en == ECONNREFUSED), d);
    close(a); close(b); close(x); close(u); unlink(pa); unlink(pb);

    int sv[2]; int sp = socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
    int fdp = open("/tmp/abi2-pass.txt", O_CREAT | O_RDWR | O_TRUNC, 0644); if (fdp >= 0) { if (write(fdp, "passed", 6) != 6) {} }
    char cb[CMSG_SPACE(sizeof(int))]; memset(cb, 0, sizeof cb); struct iovec io2 = { "f", 1 }; struct msghdr m2 = { 0 }; m2.msg_iov = &io2; m2.msg_iovlen = 1; m2.msg_control = cb; m2.msg_controllen = sizeof cb;
    struct cmsghdr* cm = CMSG_FIRSTHDR(&m2); cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS; cm->cmsg_len = CMSG_LEN(sizeof(int)); memcpy(CMSG_DATA(cm), &fdp, sizeof(int));
    int sm = (int)sendmsg(sv[0], &m2, 0);
    char cr[CMSG_SPACE(sizeof(int))]; char one; struct iovec io3 = { &one, 1 }; struct msghdr m3 = { 0 }; m3.msg_iov = &io3; m3.msg_iovlen = 1; m3.msg_control = cr; m3.msg_controllen = sizeof cr;
    int rm = (int)recvmsg(sv[1], &m3, 0); int nfd = -1; struct cmsghdr* c3 = CMSG_FIRSTHDR(&m3); if (c3 && c3->cmsg_type == SCM_RIGHTS) memcpy(&nfd, CMSG_DATA(c3), sizeof(int));
    char rb[8] = { 0 }; int rr = nfd >= 0 ? (int)pread(nfd, rb, 6, 0) : -1;
    snprintf(d, sizeof d, "socketpair %d sendmsg %d recvmsg %d fd %d read %d '%s'", sp, sm, rm, nfd, rr, rb);
    res("SCM_RIGHTS over a Unix datagram pair", sp == 0 && sm == 1 && rm == 1 && nfd >= 0 && rr == 6 && !strcmp(rb, "passed"), d);
    if (nfd >= 0) close(nfd); if (fdp >= 0) close(fdp); close(sv[0]); close(sv[1]); unlink("/tmp/abi2-pass.txt");
}

static void cred_tests(void) {
    char d[200];
    int sv[2]; socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    struct ucred uc = { 0 }; socklen_t l = sizeof uc;
    int g = getsockopt(sv[0], SOL_SOCKET, SO_PEERCRED, &uc, &l);
    snprintf(d, sizeof d, "getsockopt %d pid %d uid %d gid %d (me %d %d %d)", g, uc.pid, uc.uid, uc.gid, getpid(), getuid(), getgid());
    res("SO_PEERCRED on a socket pair", g == 0 && uc.pid == getpid() && uc.uid == getuid() && uc.gid == getgid(), d);
    close(sv[0]); close(sv[1]);

    const char* path = "/tmp/abi2-cred.sock"; unlink(path);
    int ls = socket(AF_UNIX, SOCK_STREAM, 0); struct sockaddr_un sa = { 0 }; sa.sun_family = AF_UNIX; strcpy(sa.sun_path, path);
    bind(ls, (struct sockaddr*)&sa, sizeof sa); listen(ls, 4);
    pid_t ch = fork();
    if (ch == 0) { int c = socket(AF_UNIX, SOCK_STREAM, 0); if (connect(c, (struct sockaddr*)&sa, sizeof sa) == 0) { if (write(c, "x", 1) != 1) {} sleep_ms(300); } _exit(0); }
    int as = accept(ls, 0, 0);
    struct ucred pc = { 0 }; l = sizeof pc; getsockopt(as, SOL_SOCKET, SO_PEERCRED, &pc, &l);
    int on = 1; setsockopt(as, SOL_SOCKET, SO_PASSCRED, &on, sizeof on);
    int pon = 0; l = sizeof pon; getsockopt(as, SOL_SOCKET, SO_PASSCRED, &pon, &l);
    char one; char cb[CMSG_SPACE(sizeof(struct ucred))]; struct iovec io = { &one, 1 }; struct msghdr m = { 0 }; m.msg_iov = &io; m.msg_iovlen = 1; m.msg_control = cb; m.msg_controllen = sizeof cb;
    int rn = (int)recvmsg(as, &m, 0); struct ucred mc = { 0 }; struct cmsghdr* c = CMSG_FIRSTHDR(&m); if (c && c->cmsg_type == SCM_CREDENTIALS) memcpy(&mc, CMSG_DATA(c), sizeof mc);
    waitpid(ch, 0, 0);
    snprintf(d, sizeof d, "child %d peercred pid %d, passcred get %d, recvmsg %d cred pid %d", ch, pc.pid, pon, rn, mc.pid);
    res("SO_PEERCRED and SCM_CREDENTIALS name the connecting process", pc.pid == ch && pon == 1 && rn == 1 && mc.pid == ch, d);
    close(as); close(ls); unlink(path);
}

static int v6_any_listen(int type, int* port) {
    int s = socket(AF_INET6, type, 0); if (s < 0) return -1;
    int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in6 a = { 0 }; a.sin6_family = AF_INET6; a.sin6_addr = in6addr_any; a.sin6_port = 0;
    if (bind(s, (struct sockaddr*)&a, sizeof a)) { close(s); return -2; }
    socklen_t l = sizeof a; getsockname(s, (struct sockaddr*)&a, &l); *port = ntohs(a.sin6_port);
    if (type == SOCK_STREAM && listen(s, 4)) { close(s); return -3; }
    return s; }

static void v6_tests(void) {
    char d[240]; int port = 0;
    int ls = v6_any_listen(SOCK_STREAM, &port);
    int c = socket(AF_INET6, SOCK_STREAM, 0); struct sockaddr_in6 to = { 0 }; to.sin6_family = AF_INET6; to.sin6_addr = in6addr_loopback; to.sin6_port = htons((uint16_t)port);
    int cr = c >= 0 ? connect(c, (struct sockaddr*)&to, sizeof to) : -1;
    struct sockaddr_in6 pa = { 0 }; socklen_t pl = sizeof pa; int as = ls >= 0 ? accept(ls, (struct sockaddr*)&pa, &pl) : -1;
    int w = as >= 0 ? (int)write(c, "ping6", 5) : -1; char b[16] = { 0 }; int r = as >= 0 ? (int)read(as, b, sizeof b) : -1;
    struct sockaddr_in6 pn = { 0 }; socklen_t pnl = sizeof pn; int gp = c >= 0 ? getpeername(c, (struct sockaddr*)&pn, &pnl) : -1;
    char t1[64] = "", t2[64] = ""; inet_ntop(AF_INET6, &pa.sin6_addr, t1, sizeof t1); inet_ntop(AF_INET6, &pn.sin6_addr, t2, sizeof t2);
    snprintf(d, sizeof d, "listen %d port %d connect %d accept %d fam %d from %s read %d getpeername %d %s", ls, port, cr, as, pa.sin6_family, t1, r, gp, t2);
    res("IPv6 TCP over loopback (::1): listen on ::, connect, accept, getpeername", ls >= 0 && cr == 0 && as >= 0 && pa.sin6_family == AF_INET6 && w == 5 && r == 5 && !memcmp(b, "ping6", 5) && gp == 0 && pn.sin6_family == AF_INET6 && ntohs(pn.sin6_port) == port, d);
    if (as >= 0) close(as); if (c >= 0) close(c);

    int c4 = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in t4 = { 0 }; t4.sin_family = AF_INET; t4.sin_port = htons((uint16_t)port); t4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int cr4 = connect(c4, (struct sockaddr*)&t4, sizeof t4); pl = sizeof pa; memset(&pa, 0, sizeof pa); int as4 = cr4 == 0 ? accept(ls, (struct sockaddr*)&pa, &pl) : -1;
    inet_ntop(AF_INET6, &pa.sin6_addr, t1, sizeof t1);
    snprintf(d, sizeof d, "connect %d accept %d fam %d addr %s", cr4, as4, pa.sin6_family, t1);
    res("IPv4 client reaches an IPv6 listener (address shown as ::ffff:127.0.0.1)", cr4 == 0 && as4 >= 0 && pa.sin6_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(&pa.sin6_addr), d);
    if (as4 >= 0) close(as4); close(c4);

    int cm = socket(AF_INET6, SOCK_STREAM, 0); struct sockaddr_in6 tm = { 0 }; tm.sin6_family = AF_INET6; tm.sin6_port = htons((uint16_t)port); inet_pton(AF_INET6, "::ffff:127.0.0.1", &tm.sin6_addr);
    int crm = connect(cm, (struct sockaddr*)&tm, sizeof tm); int asm_ = crm == 0 ? accept(ls, 0, 0) : -1;
    snprintf(d, sizeof d, "connect %d errno %d accept %d", crm, crm ? errno : 0, asm_);
    res("IPv6 socket connects to ::ffff:127.0.0.1", crm == 0 && asm_ >= 0, d);
    if (asm_ >= 0) close(asm_); close(cm); if (ls >= 0) close(ls);

    int up = 0; int us = v6_any_listen(SOCK_DGRAM, &up); int uc = socket(AF_INET6, SOCK_DGRAM, 0);
    struct sockaddr_in6 ut = { 0 }; ut.sin6_family = AF_INET6; ut.sin6_addr = in6addr_loopback; ut.sin6_port = htons((uint16_t)up);
    int su = (int)sendto(uc, "udp6", 4, 0, (struct sockaddr*)&ut, sizeof ut);
    struct sockaddr_in6 uf = { 0 }; socklen_t ufl = sizeof uf; struct pollfd p = { us, POLLIN, 0 }; poll(&p, 1, 1000);
    int ru = (int)recvfrom(us, b, sizeof b, MSG_DONTWAIT, (struct sockaddr*)&uf, &ufl);
    snprintf(d, sizeof d, "socket %d %d sendto %d recvfrom %d fam %d len %u", us, uc, su, ru, uf.sin6_family, (unsigned)ufl);
    res("IPv6 UDP over loopback, recvfrom gives an IPv6 address", us >= 0 && uc >= 0 && su == 4 && ru == 4 && uf.sin6_family == AF_INET6 && ufl == sizeof uf, d);
    close(us); close(uc);

    int v6o = socket(AF_INET6, SOCK_STREAM, 0); int on = 1; int so = setsockopt(v6o, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof on); int gv = -1; socklen_t gl = sizeof gv; getsockopt(v6o, IPPROTO_IPV6, IPV6_V6ONLY, &gv, &gl); close(v6o);
    struct addrinfo hint = { 0 }, *ai = 0; hint.ai_family = AF_INET6; hint.ai_flags = AI_NUMERICHOST; int ga = getaddrinfo("::1", "80", &hint, &ai);
    snprintf(d, sizeof d, "setsockopt %d get %d getaddrinfo %d", so, gv, ga);
    res("IPV6_V6ONLY option and getaddrinfo(\"::1\")", so == 0 && gv == 1 && ga == 0 && ai && ai->ai_family == AF_INET6, d);
    if (ai) freeaddrinfo(ai);
}

static int getint(int s, int lvl, int opt) { int v = -1; socklen_t l = sizeof v; if (getsockopt(s, lvl, opt, &v, &l)) return -100; return v; }
static void opt_tests(void) {
    char d[240];
    int s = socket(AF_INET, SOCK_STREAM, 0); int one = 1;
    int r = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one) | setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one) | setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one) | setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    int idle = 77; r |= setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
    snprintf(d, sizeof d, "set %d reuseaddr %d reuseport %d keepalive %d nodelay %d keepidle %d", r, getint(s, SOL_SOCKET, SO_REUSEADDR) != 0, getint(s, SOL_SOCKET, SO_REUSEPORT) != 0, getint(s, SOL_SOCKET, SO_KEEPALIVE) != 0, getint(s, IPPROTO_TCP, TCP_NODELAY) != 0, getint(s, IPPROTO_TCP, TCP_KEEPIDLE));
    res("socket options are kept: SO_REUSEADDR, SO_REUSEPORT, SO_KEEPALIVE, TCP_NODELAY, TCP_KEEPIDLE", r == 0 && getint(s, SOL_SOCKET, SO_REUSEADDR) && getint(s, SOL_SOCKET, SO_REUSEPORT) && getint(s, SOL_SOCKET, SO_KEEPALIVE) && getint(s, IPPROTO_TCP, TCP_NODELAY) && getint(s, IPPROTO_TCP, TCP_KEEPIDLE) == 77, d);
    struct linger lg = { 1, 5 }, lg2 = { 0, 0 }; socklen_t ll = sizeof lg2; int sl = setsockopt(s, SOL_SOCKET, SO_LINGER, &lg, sizeof lg); getsockopt(s, SOL_SOCKET, SO_LINGER, &lg2, &ll);
    snprintf(d, sizeof d, "set %d onoff %d linger %d", sl, lg2.l_onoff, lg2.l_linger);
    res("SO_LINGER is kept", sl == 0 && lg2.l_onoff == 1 && lg2.l_linger == 5, d);
    close(s);

    int a = socket(AF_INET, SOCK_STREAM, 0), b = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(a, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one); setsockopt(b, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
    struct sockaddr_in sa = { 0 }; sa.sin_family = AF_INET; sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK); sa.sin_port = 0;
    int ba = bind(a, (struct sockaddr*)&sa, sizeof sa); socklen_t sl2 = sizeof sa; getsockname(a, (struct sockaddr*)&sa, &sl2);
    int bb = bind(b, (struct sockaddr*)&sa, sizeof sa);
    int c = socket(AF_INET, SOCK_STREAM, 0); int c2 = socket(AF_INET, SOCK_STREAM, 0); setsockopt(c, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    int bc = bind(c, (struct sockaddr*)&sa, sizeof sa); int ec = errno;
    snprintf(d, sizeof d, "port %d first %d second %d without REUSEPORT %d errno %d", ntohs(sa.sin_port), ba, bb, bc, ec);
    res("SO_REUSEPORT lets two sockets bind one port; others still get EADDRINUSE", ba == 0 && bb == 0 && bc < 0 && ec == EADDRINUSE, d);
    close(a); close(b); close(c); close(c2);

    int u = socket(AF_INET, SOCK_DGRAM, 0); struct sockaddr_in ua = { 0 }; ua.sin_family = AF_INET; ua.sin_addr.s_addr = htonl(INADDR_LOOPBACK); bind(u, (struct sockaddr*)&ua, sizeof ua);
    struct timeval tv = { 0, 200000 }, tv2 = { 0, 0 }; socklen_t tl = sizeof tv2; int st = setsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); getsockopt(u, SOL_SOCKET, SO_RCVTIMEO, &tv2, &tl);
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); char buf[8]; int rr = (int)recv(u, buf, sizeof buf, 0); int er = errno; clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    snprintf(d, sizeof d, "set %d get %ld us, recv %d errno %d after %ld ms", st, (long)tv2.tv_usec, rr, er, ms);
    res("SO_RCVTIMEO: recv gives up with EAGAIN after the timeout", st == 0 && tv2.tv_usec == 200000 && rr < 0 && (er == EAGAIN || er == EWOULDBLOCK) && ms >= 150 && ms < 1500, d);
    close(u);
}

static void nl_tests(void) {
    char d[240];
    struct ifaddrs* ifa = 0; int g = getifaddrs(&ifa); int lo4 = 0, lo_up = 0, n = 0;
    for (struct ifaddrs* p = ifa; p; p = p->ifa_next) { n++; if (!strcmp(p->ifa_name, "lo")) { if (p->ifa_flags & IFF_UP) lo_up = 1; if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && ((struct sockaddr_in*)p->ifa_addr)->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) lo4 = 1; } }
    snprintf(d, sizeof d, "getifaddrs %d entries %d lo up %d lo 127.0.0.1 %d", g, n, lo_up, lo4);
    res("getifaddrs lists lo with 127.0.0.1 (netlink)", g == 0 && lo_up && lo4, d);
    if (ifa) freeifaddrs(ifa);
    unsigned li = if_nametoindex("lo"); char nm[IF_NAMESIZE] = ""; if_indextoname(li ? li : 1, nm);
    struct if_nameindex* ix = if_nameindex(); int nix = 0; if (ix) { for (struct if_nameindex* q = ix; q->if_index; q++) nix++; if_freenameindex(ix); }
    snprintf(d, sizeof d, "if_nametoindex(lo) %u name '%s' if_nameindex entries %d", li, nm, nix);
    res("if_nametoindex / if_indextoname / if_nameindex", li >= 1 && !strcmp(nm, "lo") && nix >= 1, d);

    int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    struct sockaddr_nl sa = { 0 }; sa.nl_family = AF_NETLINK; int b = s >= 0 ? bind(s, (struct sockaddr*)&sa, sizeof sa) : -1;
    struct { struct nlmsghdr h; struct rtgenmsg g; } rq; memset(&rq, 0, sizeof rq); rq.h.nlmsg_len = sizeof rq; rq.h.nlmsg_type = RTM_GETLINK; rq.h.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP; rq.h.nlmsg_seq = 9; rq.g.rtgen_family = AF_UNSPEC;
    int sn = s >= 0 ? (int)send(s, &rq, sizeof rq, 0) : -1; int links = 0, done = 0, seqok = 1;
    char buf[16384] __attribute__((aligned(8)));
    for (int k = 0; k < 8 && !done && s >= 0; k++) { int r = (int)recv(s, buf, sizeof buf, 0); if (r <= 0) break;
        for (struct nlmsghdr* h = (struct nlmsghdr*)buf; NLMSG_OK(h, r); h = NLMSG_NEXT(h, r)) { if (h->nlmsg_seq != 9) seqok = 0; if (h->nlmsg_type == NLMSG_DONE) { done = 1; break; } if (h->nlmsg_type == RTM_NEWLINK) links++; } }
    struct sockaddr_nl me = { 0 }; socklen_t ml = sizeof me; getsockname(s, (struct sockaddr*)&me, &ml);
    snprintf(d, sizeof d, "socket %d bind %d send %d links %d done %d seq %d nl_pid %u", s, b, sn, links, done, seqok, me.nl_pid);
    res("raw netlink RTM_GETLINK dump ends with NLMSG_DONE", s >= 0 && b == 0 && sn == (int)sizeof rq && links >= 1 && done && seqok && me.nl_pid != 0, d);
    if (s >= 0) close(s);
}

static unsigned short csum(const void* p, int n) { const unsigned short* w = p; unsigned s = 0; while (n > 1) { s += *w++; n -= 2; } if (n) s += *(const unsigned char*)w; s = (s >> 16) + (s & 0xFFFF); s += s >> 16; return (unsigned short)~s; }
static void icmp_tests(void) {
    char d[200];
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (s < 0 && (errno == EACCES || errno == EPERM)) { skip("ICMP ping socket", "not allowed for this user on this system"); return; }
    struct sockaddr_in to = { 0 }; to.sin_family = AF_INET; to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    unsigned char pkt[64]; memset(pkt, 0, sizeof pkt); struct icmphdr* ih = (struct icmphdr*)pkt; ih->type = ICMP_ECHO; ih->un.echo.sequence = htons(5); memcpy(pkt + 8, "noovex-ping", 11); ih->checksum = csum(pkt, 19);
    int sn = s >= 0 ? (int)sendto(s, pkt, 19, 0, (struct sockaddr*)&to, sizeof to) : -1;
    struct pollfd p = { s, POLLIN, 0 }; int pr = s >= 0 ? poll(&p, 1, 2000) : -1;
    unsigned char rb[128]; struct sockaddr_in fr = { 0 }; socklen_t fl = sizeof fr; int rn = pr > 0 ? (int)recvfrom(s, rb, sizeof rb, 0, (struct sockaddr*)&fr, &fl) : -1;
    struct icmphdr* rh = (struct icmphdr*)rb;
    snprintf(d, sizeof d, "socket %d sendto %d poll %d recv %d type %d seq %d from %s", s, sn, pr, rn, rn > 0 ? rh->type : -1, rn > 0 ? ntohs(rh->un.echo.sequence) : -1, inet_ntoa(fr.sin_addr));
    res("ICMP ping socket: echo to 127.0.0.1 gets a reply", s >= 0 && sn == 19 && rn == 19 && rh->type == ICMP_ECHOREPLY && ntohs(rh->un.echo.sequence) == 5 && !memcmp(rb + 8, "noovex-ping", 11) && fr.sin_addr.s_addr == htonl(INADDR_LOOPBACK), d);
    if (s >= 0) close(s);
}


static void flag_tests(void) {
    char d[200];
    errno = 0; long e1 = syscall(SYS_eventfd2, 0, ~0); int ee1 = errno;
    errno = 0; long m1 = syscall(SYS_memfd_create, "", ~0); int em1 = errno;
    errno = 0; long p1 = syscall(SYS_epoll_create1, 0x7fffffff); int ep1 = errno;
    errno = 0; long t1 = syscall(SYS_timerfd_create, CLOCK_MONOTONIC, 0x1000); int et1 = errno;
    int pp[2]; errno = 0; long q1 = syscall(SYS_pipe2, pp, 0x10); int eq1 = errno;
    snprintf(d, sizeof d, "eventfd2 %ld/%d memfd %ld/%d epoll %ld/%d timerfd %ld/%d pipe2 %ld/%d", e1, ee1, m1, em1, p1, ep1, t1, et1, q1, eq1);
    res("unknown flags are refused with EINVAL (eventfd2, memfd_create, epoll_create1, timerfd_create, pipe2)", e1 < 0 && ee1 == EINVAL && m1 < 0 && em1 == EINVAL && p1 < 0 && ep1 == EINVAL && t1 < 0 && et1 == EINVAL && q1 < 0 && eq1 == EINVAL, d);
    int in = inotify_init1(IN_NONBLOCK); char dir[64]; snprintf(dir, sizeof dir, "/tmp/abi2ino%d", (int)getpid()); mkdir(dir, 0755);
    int wd = in >= 0 ? inotify_add_watch(in, dir, IN_CREATE) : -1; char f[96]; snprintf(f, sizeof f, "%s/x", dir); int fd = open(f, O_CREAT | O_WRONLY, 0644); if (fd >= 0) close(fd);
    struct pollfd p = { in, POLLIN, 0 }; int pr = poll(&p, 1, 1000); int avail = -1; ioctl(in, FIONREAD, &avail);
    char buf[512]; int rn = avail > 0 ? (int)read(in, buf, (size_t)avail) : -1; int avail2 = -1; ioctl(in, FIONREAD, &avail2);
    snprintf(d, sizeof d, "watch %d poll %d FIONREAD %d read %d then %d", wd, pr, avail, rn, avail2);
    res("FIONREAD on inotify gives the queued bytes (Chromium's file watcher)", wd >= 0 && pr == 1 && avail >= (int)sizeof(struct inotify_event) && rn == avail && avail2 == 0, d);
    unlink(f); rmdir(dir); if (in >= 0) close(in);
}

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : "";
    setvbuf(stdout, 0, _IONBF, 0);
    if (!*only || strstr(only, "rt")) { rt_tests(); thread_tests(); }
    if (!*only || strstr(only, "un")) { unix_dgram_tests(); cred_tests(); }
    if (!*only || strstr(only, "v6")) v6_tests();
    if (!*only || strstr(only, "opt")) opt_tests();
    if (!*only || strstr(only, "nl")) nl_tests();
    if (!*only || strstr(only, "icmp")) icmp_tests();
    if (!*only || strstr(only, "fl")) flag_tests();
    printf("abi2: done %d passed, %d failed, %d skipped\n", pass, total - pass, skipped);
    return pass == total ? 0 : 1;
}
