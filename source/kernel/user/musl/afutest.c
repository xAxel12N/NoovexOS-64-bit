#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <errno.h>
#include <stdint.h>

static int send_fd(int sock, int fd) {
    char b[1] = { 'X' };
    struct iovec io = { b, 1 };
    char cbuf[CMSG_SPACE(sizeof(int))];
    memset(cbuf, 0, sizeof cbuf);
    struct msghdr m;
    memset(&m, 0, sizeof m);
    m.msg_iov = &io; m.msg_iovlen = 1;
    m.msg_control = cbuf; m.msg_controllen = sizeof cbuf;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &fd, sizeof(int));
    return sendmsg(sock, &m, 0);
}
static int recv_fd(int sock) {
    char b[1];
    struct iovec io = { b, 1 };
    char cbuf[CMSG_SPACE(sizeof(int))];
    memset(cbuf, 0, sizeof cbuf);
    struct msghdr m;
    memset(&m, 0, sizeof m);
    m.msg_iov = &io; m.msg_iovlen = 1;
    m.msg_control = cbuf; m.msg_controllen = sizeof cbuf;
    if (recvmsg(sock, &m, 0) < 0) return -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    if (!c || c->cmsg_type != SCM_RIGHTS) return -1;
    int fd; memcpy(&fd, CMSG_DATA(c), sizeof(int));
    return fd;
}

int main(void) {
    int pass = 0, total = 0;
    int sp[2];

    total++;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0) {
        write(sp[0], "hello", 5);
        char b[8] = {0};
        int n = read(sp[1], b, 8);
        if (n == 5 && !memcmp(b, "hello", 5)) { printf("[ok] socketpair stream data\n"); pass++; }
        else printf("[FAIL] socketpair data n=%d\n", n);
    } else { printf("[FAIL] socketpair create\n"); printf("AFUTEST %d/%d\n", pass, 1); return 1; }

    total++;
    long mfd = syscall(SYS_memfd_create, "t", 0);
    char *p = MAP_FAILED;
    if (mfd >= 0 && ftruncate((int)mfd, 4096) == 0) {
        p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, (int)mfd, 0);
        if (p != MAP_FAILED) { strcpy(p, "shm-works"); printf("[ok] memfd_create+ftruncate+mmap\n"); pass++; }
        else printf("[FAIL] mmap memfd\n");
    } else printf("[FAIL] memfd_create rc=%ld\n", mfd);

    if (p != MAP_FAILED) {
        total++;
        if (send_fd(sp[0], (int)mfd) > 0) {
            int rfd = recv_fd(sp[1]);
            if (rfd >= 0) {
                char *q = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, rfd, 0);
                if (q != MAP_FAILED && !strcmp(q, "shm-works")) {
                    q[0] = 'S';
                    if (p[0] == 'S') { printf("[ok] SCM_RIGHTS fd-pass + shared memfd\n"); pass++; }
                    else printf("[FAIL] memfd not shared (p[0]=%c)\n", p[0]);
                } else printf("[FAIL] mmap received fd\n");
            } else printf("[FAIL] recv_fd\n");
        } else printf("[FAIL] send_fd\n");

        total++;
        int cp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, cp) == 0) {
            strcpy(p, "parent-init");
            pid_t pid = fork();
            if (pid == 0) {
                int fd2 = recv_fd(cp[1]);
                char *cpr = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
                if (cpr != MAP_FAILED) strcpy(cpr, "child-wrote");
                _exit(0);
            } else if (pid > 0) {
                send_fd(cp[0], (int)mfd);
                int st; waitpid(pid, &st, 0);
                if (!strcmp(p, "child-wrote")) { printf("[ok] cross-process shared memory via SCM_RIGHTS\n"); pass++; }
                else printf("[FAIL] cross-proc shm (p=%s)\n", p);
            } else printf("[FAIL] fork\n");
        } else printf("[FAIL] socketpair cp\n");
    }

    total++;
    {
        int e = eventfd(0, EFD_NONBLOCK);
        uint64_t v = 0;
        int ok = e >= 0;
        if (ok && read(e, &v, 8) != -1) ok = 0;
        if (ok && errno != EAGAIN) ok = 0;
        v = 5;
        if (ok && write(e, &v, 8) != 8) ok = 0;
        struct pollfd pf = { e, POLLIN, 0 };
        if (ok && (poll(&pf, 1, 0) != 1 || !(pf.revents & POLLIN))) ok = 0;
        v = 0;
        if (ok && (read(e, &v, 8) != 8 || v != 5)) ok = 0;
        if (ok) { printf("[ok] eventfd counter + poll\n"); pass++; } else printf("[FAIL] eventfd (fd=%d v=%llu errno=%d)\n", e, (unsigned long long)v, errno);
    }

    total++;
    {
        int t = timerfd_create(CLOCK_MONOTONIC, 0);
        struct itimerspec its; memset(&its, 0, sizeof its);
        its.it_value.tv_nsec = 50000000;
        uint64_t n = 0;
        int ok = t >= 0 && timerfd_settime(t, 0, &its, 0) == 0;
        if (ok && (read(t, &n, 8) != 8 || n < 1)) ok = 0;
        if (ok) { printf("[ok] timerfd one-shot fired (%llu)\n", (unsigned long long)n); pass++; } else printf("[FAIL] timerfd (fd=%d n=%llu errno=%d)\n", t, (unsigned long long)n, errno);
    }

    printf("AFUTEST %d/%d passed\n", pass, total);
    return pass == total ? 0 : 1;
}
