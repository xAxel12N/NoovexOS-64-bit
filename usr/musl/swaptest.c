#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/syscall.h>

static int pass, fail;
static void ck(int ok, const char* what) {
    if (ok) pass++;
    else { fail++; printf("swap: FAIL %s (errno %d)\n", what, errno); }
}
static long mi(const char* key) {
    char b[4096]; int fd = open("/proc/meminfo", O_RDONLY); if (fd < 0) return -1;
    int n = read(fd, b, sizeof b - 1); close(fd); if (n <= 0) return -1; b[n] = 0;
    char* p = strstr(b, key); if (!p) return -1; p += strlen(key);
    while (*p == ' ' || *p == ':') p++;
    return atol(p);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static unsigned rnd(unsigned* s) { *s = *s * 1103515245u + 12345u; return *s >> 8; }
static void fill(unsigned char* p, long i, int gen) {
    unsigned long* w = (unsigned long*)p; int k = (int)(i % 4);
    if (k == 0) { for (int j = 0; j < 512; j++) w[j] = 0x0101010101010101UL * (unsigned long)((i + gen) & 0xFF); return; }
    if (k == 2) { unsigned s = (unsigned)(i * 7919 + gen); for (int j = 0; j < 4096; j++) p[j] = (unsigned char)rnd(&s); return; }
    if (k == 1) { const char* t = "NoovexOS swap test page, compressible text "; int L = (int)strlen(t); for (int j = 0; j < 4096; j++) p[j] = (unsigned char)t[j % L]; }
    else { for (int j = 0; j < 4096; j++) p[j] = (unsigned char)((j / 16 + gen) & 0x0F); }
    w[0] = (unsigned long)i; w[1] = (unsigned long)gen;
}
static long check(unsigned char* base, long pages, int gen) {
    unsigned char t[4096]; long bad = 0;
    for (long i = 0; i < pages; i++) { fill(t, i, gen); if (memcmp(t, base + i * 4096, 4096)) bad++; }
    return bad;
}

int main(void) {
    long st = mi("SwapTotal"), sf0 = mi("SwapFree");
    ck(st > 0, "SwapTotal is above 0");
    { char b[512]; int fd = open("/proc/swaps", O_RDONLY); int n = fd >= 0 ? read(fd, b, sizeof b - 1) : -1; if (fd >= 0) close(fd); b[n > 0 ? n : 0] = 0; ck(strstr(b, "/dev/zram0") != 0, "/proc/swaps lists /dev/zram0"); }
    long pages = 8192; size_t len = (size_t)pages * 4096;
    unsigned char* m = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ck(m != MAP_FAILED, "mmap 32 MB");
    if (m == MAP_FAILED) { printf("swap: done %d passed, %d failed\n", pass, fail); return 1; }
    for (long i = 0; i < pages; i++) fill(m + i * 4096, i, 1);
    double t0 = now();
    ck(madvise(m, len, 21) == 0, "madvise MADV_PAGEOUT");
    double t1 = now();
    long sf1 = mi("SwapFree"), zs = mi("Zswap"), zd = mi("Zswapped");
    long out_kb = sf0 - sf1;
    printf("swap: paged out %ld kB of 32768 kB in %.2f s, stored in %ld kB (compressed pages %ld kB)\n", out_kb, t1 - t0, zs, zd);
    ck(out_kb >= 20000, "at least 20 MB went to swap (the random quarter stays in memory)");
    ck(zs > 0 && zs * 4 < zd, "compressed store is less than a quarter of the swapped size");
    ck(check(m, pages, 1) == 0, "every page reads back correctly after swap-in");
    ck(mi("SwapFree") >= sf0 - 64, "swap-in gives the swap space back");

    madvise(m, len, 21);
    pid_t c = fork();
    if (c == 0) { long bad = check(m, pages, 1); for (long i = 0; i < pages; i += 3) fill(m + i * 4096, i, 2); _exit(bad ? 3 : 0); }
    int ws = 0; waitpid(c, &ws, 0);
    ck(WIFEXITED(ws) && WEXITSTATUS(ws) == 0, "a child after fork reads the swapped pages");
    ck(check(m, pages, 1) == 0, "the child's writes do not reach the parent");

    madvise(m, len, 21);
    ck(mprotect(m, len, PROT_READ) == 0 && check(m, pages, 1) == 0, "swapped pages read back under PROT_READ");
    mprotect(m, len, PROT_READ | PROT_WRITE);

    madvise(m, len, 21);
    ck(madvise(m, 4096 * 16, MADV_DONTNEED) == 0, "MADV_DONTNEED on swapped pages");
    { int z = 1; for (int i = 0; i < 4096 * 16; i++) if (m[i]) { z = 0; break; } ck(z, "they read back as zero"); }
    long sf2 = mi("SwapFree");
    ck(munmap(m, len) == 0, "munmap of a swapped range");
    ck(mi("SwapFree") > sf2 && mi("SwapFree") >= sf0 - 64, "munmap frees the swap space");

    { int fd = open("/proc/sys/vm/swappiness", O_RDONLY); char b[16] = ""; int n = fd >= 0 ? read(fd, b, 15) : -1; if (fd >= 0) close(fd); if (n > 0) b[n] = 0; ck(n > 0 && atoi(b) == 60, "swappiness reads 60"); }
    { long c0 = mi("Cached"); int fd = open("/proc/sys/vm/drop_caches", O_WRONLY); ck(fd >= 0 && write(fd, "3\n", 2) == 2, "drop_caches accepts 3"); if (fd >= 0) close(fd); ck(mi("Cached") <= c0, "Cached does not grow after drop_caches"); }
    errno = 0; ck(syscall(SYS_swapon, "/dev/sda", 0) == -1 && errno == EINVAL, "swapon on a disk is refused (only zram)");

    long avail = mi("MemAvailable");
    if (avail > 0 && avail < 1400 * 1024) {
        long pp = (avail + 160 * 1024) / 4; size_t pl = (size_t)pp * 4096;
        unsigned char* q = mmap(0, pl, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ck(q != MAP_FAILED, "mmap more than the free memory");
        if (q != MAP_FAILED) {
            double a0 = now();
            for (long i = 0; i < pp; i++) { unsigned char* c = q + i * 4096; const char* t = "pressure page "; for (int j = 0; j < 4096; j++) c[j] = (unsigned char)t[j % 14]; unsigned long* w = (unsigned long*)c; w[0] = (unsigned long)i; w[511] = ~(unsigned long)i; }
            long bad = 0; for (long i = 0; i < pp; i++) { unsigned long* w = (unsigned long*)(q + i * 4096); if (w[0] != (unsigned long)i || w[511] != ~(unsigned long)i || q[i * 4096 + 100] != (unsigned char)"pressure page "[100 % 14]) bad++; }
            printf("swap: wrote %ld MB with %ld MB available in %.1f s, swap used %ld MB\n", pp / 256, avail / 1024, now() - a0, (st - mi("SwapFree")) / 1024);
            ck(bad == 0, "all pages correct under memory pressure");
            munmap(q, pl);
        }
    } else printf("swap: pressure step skipped (%ld kB available)\n", avail);
    printf("swap: done %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
