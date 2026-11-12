#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/sysmacros.h>
struct sockaddr_nl { unsigned short nl_family; unsigned short nl_pad; unsigned int nl_pid; unsigned int nl_groups; };
#define NETLINK_KOBJECT_UEVENT 15

static int pass, fail;
static void ck(int ok, const char* what) {
    if (ok) pass++;
    else { fail++; printf("sysfs: FAIL %s (errno %d)\n", what, errno); }
}
static int rd(const char* p, char* b, int cap) {
    int fd = open(p, O_RDONLY); if (fd < 0) return -1;
    int n = read(fd, b, cap - 1); close(fd); if (n < 0) return -1; b[n] = 0; return n;
}
static int has_entry(const char* dir, const char* name) {
    DIR* d = opendir(dir); if (!d) return 0; struct dirent* e; int f = 0;
    while ((e = readdir(d))) if (!strcmp(e->d_name, name)) f = 1;
    closedir(d); return f;
}
static int count_entries(const char* dir) {
    DIR* d = opendir(dir); if (!d) return -1; struct dirent* e; int n = 0;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d); return n;
}

int main(void) {
    char b[4096], l[256]; struct stat st;
    ck(has_entry("/sys", "class") && has_entry("/sys", "devices") && has_entry("/sys", "bus") && has_entry("/sys", "dev") && has_entry("/sys", "block"), "/sys has class, devices, bus, dev, block");
    ck(has_entry("/sys/class", "net") && has_entry("/sys/class", "mem") && has_entry("/sys/class", "tty"), "/sys/class lists net, mem and tty");
    ck(has_entry("/sys/class/net", "lo"), "/sys/class/net/lo exists");
    ssize_t n = readlink("/sys/class/net/lo", l, sizeof l - 1); if (n > 0) l[n] = 0; else l[0] = 0;
    ck(n > 0 && !strcmp(l, "../../devices/virtual/net/lo"), "/sys/class/net/lo is a link to ../../devices/virtual/net/lo");
    ck(lstat("/sys/class/net/lo", &st) == 0 && S_ISLNK(st.st_mode), "lstat sees a symbolic link");
    ck(stat("/sys/class/net/lo", &st) == 0 && S_ISDIR(st.st_mode), "stat follows it to a directory");
    ck(rd("/sys/class/net/lo/address", b, sizeof b) > 0 && !strcmp(b, "00:00:00:00:00:00\n"), "lo address through the link");
    ck(rd("/sys/class/net/lo/mtu", b, sizeof b) > 0 && atoi(b) == 65536, "lo mtu 65536");
    ck(rd("/sys/devices/virtual/mem/null/dev", b, sizeof b) > 0 && !strcmp(b, "1:3\n"), "/dev/null is 1:3 in sysfs");
    ck(rd("/sys/devices/virtual/mem/null/uevent", b, sizeof b) > 0 && strstr(b, "MAJOR=1") && strstr(b, "DEVNAME=null"), "uevent file has MAJOR and DEVNAME");
    n = readlink("/sys/dev/char/1:3", l, sizeof l - 1); if (n > 0) l[n] = 0; else l[0] = 0;
    ck(n > 0 && !strcmp(l, "../../devices/virtual/mem/null"), "/sys/dev/char/1:3 points at the null device");
    n = readlink("/sys/devices/virtual/mem/null/subsystem", l, sizeof l - 1); if (n > 0) l[n] = 0; else l[0] = 0;
    ck(n > 0 && strstr(l, "class/mem") != 0, "subsystem link points at class/mem");
    char rp[512]; ck(realpath("/sys/class/mem/zero", rp) && !strcmp(rp, "/sys/devices/virtual/mem/zero"), "realpath resolves class links");
    ck(rd("/sys/kernel/ostype", b, sizeof b) > 0 && !strcmp(b, "NoovexOS\n"), "/sys/kernel/ostype");
    ck(rd("/sys/power/state", b, sizeof b) > 0 && strstr(b, "freeze"), "/sys/power/state offers freeze");
    errno = 0; ck(open("/sys/class/nothing/here", O_RDONLY) < 0 && errno == ENOENT, "missing sysfs path gives ENOENT");
    int nb = count_entries("/sys/block"); ck(nb >= 0, "/sys/block can be listed");
    if (nb > 0) {
        DIR* d = opendir("/sys/block"); struct dirent* e; char dn[64] = "";
        while ((e = readdir(d))) if (e->d_name[0] != '.') { strncpy(dn, e->d_name, 63); break; }
        closedir(d);
        char p[256]; snprintf(p, sizeof p, "/sys/block/%s/size", dn);
        long long sz = rd(p, b, sizeof b) > 0 ? atoll(b) : -1;
        ck(sz > 0, "disk size in /sys/block");
        snprintf(p, sizeof p, "/dev/%s", dn);
        ck(stat(p, &st) == 0 && S_ISBLK(st.st_mode), "disk node in /dev is a block device");
        int fd = open(p, O_RDONLY);
        ck(fd >= 0, "disk node opens read-only");
        if (fd >= 0) {
            unsigned char s0[512]; ck(read(fd, s0, 512) == 512, "first sector reads");
            ck(lseek(fd, 0, SEEK_END) == sz * 512, "seek to end matches the size");
            close(fd);
        }
        errno = 0; ck(open(p, O_RDWR) < 0 && errno == EROFS, "disk node refuses writing (EROFS)");
        printf("sysfs: disk %s, %lld sectors\n", dn, sz);
    }
    ck(has_entry("/dev", "null") && has_entry("/dev", "ptmx"), "/dev lists null and ptmx");
    { char lp[400]; strcpy(lp, "/sys/class/net/lo/"); while (strlen(lp) < 240) strcat(lp, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/");
      errno = 0; ck(stat(lp, &st) < 0, "very long path through a link is refused without harm"); }
    { int wf = open("/sys/class/net/lo/uevent", O_RDWR); ck(wf >= 0, "uevent opens for writing as root");
      if (wf >= 0) { errno = 0; ck(lseek(wf, 0x80000000LL, SEEK_SET) < 0 && errno == EINVAL, "huge seek on a sysfs write fd gives EINVAL"); close(wf); } }
    int s = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    ck(s >= 0, "uevent netlink socket");
    if (s >= 0) {
        struct sockaddr_nl a; memset(&a, 0, sizeof a); a.nl_family = AF_NETLINK; a.nl_groups = 1;
        ck(bind(s, (struct sockaddr*)&a, sizeof a) == 0, "bind to the kernel uevent group");
        int fd = open("/sys/class/net/lo/uevent", O_WRONLY);
        ck(fd >= 0 && write(fd, "change", 6) == 6, "writing change to a uevent file");
        if (fd >= 0) close(fd);
        struct pollfd pf = { s, POLLIN, 0 }; int pr = poll(&pf, 1, 3000);
        ck(pr == 1, "uevent arrives");
        if (pr == 1) {
            struct sockaddr_nl from; socklen_t fl = sizeof from;
            int m = recvfrom(s, b, sizeof b - 1, 0, (struct sockaddr*)&from, &fl);
            b[m > 0 ? m : 0] = 0;
            ck(m > 0 && !strcmp(b, "change@/devices/virtual/net/lo"), "message starts with change@/devices/virtual/net/lo");
            int act = 0, sub = 0; for (int i = 0; i < m; i += strlen(b + i) + 1) { if (!strcmp(b + i, "ACTION=change")) act = 1; if (!strcmp(b + i, "SUBSYSTEM=net")) sub = 1; }
            ck(act && sub, "message has ACTION and SUBSYSTEM");
            ck(from.nl_pid == 0 && from.nl_groups == 1, "sender is the kernel (pid 0, group 1)");
        }
        close(s);
    }
    printf("sysfs: done %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
