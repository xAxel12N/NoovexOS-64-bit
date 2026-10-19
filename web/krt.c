#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "include/nvx/apps/nwe_api.h"
#include "third_party/quickjs/dtoa.h"

void nwe_fail(const char* why);

typedef struct hdr { size_t sz; size_t cls; } hdr;
typedef struct fnode { struct fnode* next; } fnode;
typedef struct lnode { struct lnode* next; size_t sz; } lnode;

static unsigned char* A;
static size_t AS, AT, peak;
#define NCLS 64
static size_t csz[NCLS];
static int ncls;
static fnode* fl[NCLS];
static lnode* lfree;

void nwe_arena_init(void) {
    if (A) return;
    unsigned long want[] = { 768ul << 20, 512ul << 20, 384ul << 20, 256ul << 20, 160ul << 20, 96ul << 20, 64ul << 20, 32ul << 20 };
    for (int i = 0; i < 8 && !A; i++) { A = nwk_alloc_arena(want[i]); if (A) AS = want[i]; }
    size_t s = 16;
    ncls = 0;
    while (s <= 65536 && ncls < NCLS) { csz[ncls++] = s; s += s < 512 ? 16 : ((s / 4 + 15) & ~(size_t)15); }
}
void nwe_arena_reset(void) {
    AT = 0;
    for (int i = 0; i < NCLS; i++) fl[i] = 0;
    lfree = 0;
}
size_t nwe_arena_used(void) { return AT; }
size_t nwe_arena_size(void) { return AS; }

static int cls_of(size_t n) {
    if (n <= 512) return (int)((n + 15) / 16) - 1;
    int lo = 31, hi = ncls - 1;
    while (lo < hi) { int m = (lo + hi) / 2; if (csz[m] >= n) hi = m; else lo = m + 1; }
    return lo;
}
static void* bump(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!A || AT + n > AS) return 0;
    void* p = A + AT;
    AT += n;
    if (AT > peak) peak = AT;
    return p;
}
void* malloc(size_t n) {
    if (n == 0) n = 1;
    if (ncls && n <= csz[ncls - 1]) {
        int c = cls_of(n);
        if (c < 0 || c >= ncls || csz[c] < n) return 0;
        if (fl[c]) { fnode* f = fl[c]; fl[c] = f->next; return f; }
        hdr* h = bump(sizeof(hdr) + csz[c]);
        if (!h) return 0;
        h->sz = csz[c];
        h->cls = (size_t)c;
        return h + 1;
    }
    size_t need = (n + 4095) & ~(size_t)4095;
    lnode** pp = &lfree;
    while (*pp) {
        lnode* l = *pp;
        if (l->sz >= need && l->sz <= need * 2) {
            *pp = l->next;
            hdr* h = (hdr*)l - 1;
            return h + 1;
        }
        pp = &l->next;
    }
    hdr* h = bump(sizeof(hdr) + need);
    if (!h) return 0;
    h->sz = need;
    h->cls = 0xFFFF;
    return h + 1;
}
void free(void* p) {
    if (!p) return;
    unsigned char* u = p;
    if (u < A || u >= A + AT) return;
    hdr* h = (hdr*)p - 1;
    if (h->cls == 0xFFFF) {
        lnode* l = (lnode*)p;
        l->sz = h->sz;
        l->next = lfree;
        lfree = l;
        return;
    }
    if (h->cls >= (size_t)ncls) return;
    fnode* f = p;
    f->next = fl[h->cls];
    fl[h->cls] = f;
}
size_t malloc_usable_size(void* p) { return p ? ((hdr*)p - 1)->sz : 0; }
void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    if (n == 0) { free(p); return 0; }
    size_t old = ((hdr*)p - 1)->sz;
    if (n <= old) return p;
    void* q = malloc(n);
    if (!q) return 0;
    memcpy(q, p, old);
    free(p);
    return q;
}
void* calloc(size_t a, size_t b) {
    size_t n = a * b;
    void* p = malloc(n);
    if (p) memset(p, 0, n);
    return p;
}

int abs(int v) { return v < 0 ? -v : v; }
void abort(void) { nwe_fail("abort"); for (;;) {} }

typedef struct { char* o; size_t n, cap; } fout;
static void fput(fout* f, char c) { if (f->n + 1 < f->cap) f->o[f->n] = c; f->n++; }
static void fputs_(fout* f, const char* s, int len, int width, int left, char pad) {
    int fillc = width > len ? width - len : 0;
    if (!left) while (fillc-- > 0) fput(f, pad);
    for (int i = 0; i < len; i++) fput(f, s[i]);
    if (left) while (fillc-- > 0) fput(f, ' ');
}
static int fmt_u(char* b, unsigned long long v, int base, int upper) {
    char t[32];
    int n = 0;
    const char* d = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do { t[n++] = d[v % (unsigned)base]; v /= (unsigned)base; } while (v);
    for (int i = 0; i < n; i++) b[i] = t[n - 1 - i];
    return n;
}
static int fmt_f(char* b, double v, int prec, int expo, int gstyle) {
    int n = 0;
    if (v != v) { memcpy(b, "nan", 3); return 3; }
    if (v < 0) { b[n++] = '-'; v = -v; }
    if (v > 1e300) { memcpy(b + n, "inf", 3); return n + 3; }
    if (prec < 0) prec = 6;
    if (prec > 17) prec = 17;
    int e = 0;
    if (gstyle) {
        if (prec == 0) prec = 1;
        double t = v;
        if (t != 0) { while (t >= 10) { t /= 10; e++; } while (t < 1) { t *= 10; e--; } }
        if (e < -4 || e >= prec) { expo = 1; prec = prec - 1; }
        else prec = prec - 1 - e;
        if (prec < 0) prec = 0;
    }
    if (expo) {
        e = 0;
        if (v != 0) { while (v >= 10) { v /= 10; e++; } while (v < 1) { v *= 10; e--; } }
    }
    double scale = 1;
    for (int i = 0; i < prec; i++) scale *= 10;
    double r = v * scale + 0.5;
    if (r >= 1.8e19) { memcpy(b + n, "big", 3); return n + 3; }
    unsigned long long iv = (unsigned long long)r;
    unsigned long long ip = iv / (unsigned long long)scale, fp = iv % (unsigned long long)scale;
    if (expo && ip >= 10) { ip /= 10; e++; }
    n += fmt_u(b + n, ip, 10, 0);
    char fr[24];
    int fn = 0;
    if (prec > 0) {
        unsigned long long d = (unsigned long long)scale / 10;
        for (int i = 0; i < prec; i++) { fr[fn++] = (char)('0' + (fp / (d ? d : 1)) % 10); if (d) d /= 10; }
        if (gstyle) while (fn > 0 && fr[fn - 1] == '0') fn--;
        if (fn) { b[n++] = '.'; memcpy(b + n, fr, (size_t)fn); n += fn; }
    }
    if (expo) {
        b[n++] = 'e';
        b[n++] = e < 0 ? '-' : '+';
        if (e < 0) e = -e;
        if (e < 10) b[n++] = '0';
        n += fmt_u(b + n, (unsigned)e, 10, 0);
    }
    return n;
}
int vsnprintf(char* out, size_t cap, const char* fmt, va_list ap) {
    fout f = { out, 0, cap };
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') { fput(&f, *p); continue; }
        p++;
        int left = 0, zero = 0, plus = 0, space = 0, width = 0, prec = -1, lng = 0;
        for (;; p++) {
            if (*p == '-') left = 1; else if (*p == '0') zero = 1; else if (*p == '+') plus = 1; else if (*p == ' ') space = 1; else if (*p == '#') {} else break;
        }
        if (*p == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } p++; }
        else while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') { prec = va_arg(ap, int); p++; }
            else while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0');
        }
        for (;; p++) {
            if (*p == 'l') lng++; else if (*p == 'z' || *p == 'j' || *p == 't') lng = 2; else if (*p == 'h' || *p == 'L' || *p == 'q') {} else break;
        }
        char b[96];
        int n = 0;
        char pad = (zero && !left) ? '0' : ' ';
        switch (*p) {
        case 'd': case 'i': {
            long long v = lng >= 2 ? va_arg(ap, long long) : lng == 1 ? va_arg(ap, long) : va_arg(ap, int);
            unsigned long long u = v < 0 ? (unsigned long long)(-(v + 1)) + 1 : (unsigned long long)v;
            if (v < 0) b[n++] = '-'; else if (plus) b[n++] = '+'; else if (space) b[n++] = ' ';
            int s = n;
            n += fmt_u(b + n, u, 10, 0);
            if (prec > n - s) { int add = prec - (n - s); memmove(b + s + add, b + s, (size_t)(n - s)); for (int i = 0; i < add; i++) b[s + i] = '0'; n += add; }
            if (pad == '0' && n > 0 && (b[0] == '-' || b[0] == '+') && width > n) { fput(&f, b[0]); fputs_(&f, b + 1, n - 1, width - 1, left, pad); }
            else fputs_(&f, b, n, width, left, pad);
            break; }
        case 'u': case 'x': case 'X': case 'o': {
            unsigned long long v = lng >= 2 ? va_arg(ap, unsigned long long) : lng == 1 ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
            n = fmt_u(b, v, *p == 'u' ? 10 : *p == 'o' ? 8 : 16, *p == 'X');
            if (prec > n) { int add = prec - n; memmove(b + add, b, (size_t)n); for (int i = 0; i < add; i++) b[i] = '0'; n += add; }
            fputs_(&f, b, n, width, left, pad);
            break; }
        case 'p': {
            unsigned long long v = (unsigned long long)(uintptr_t)va_arg(ap, void*);
            b[0] = '0'; b[1] = 'x';
            n = 2 + fmt_u(b + 2, v, 16, 0);
            fputs_(&f, b, n, width, left, ' ');
            break; }
        case 'c': b[0] = (char)va_arg(ap, int); fputs_(&f, b, 1, width, left, ' '); break;
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int l = 0;
            while (s[l] && (prec < 0 || l < prec)) l++;
            fputs_(&f, s, l, width, left, ' ');
            break; }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            double v = va_arg(ap, double);
            if (plus && v >= 0) b[n++] = '+';
            n += fmt_f(b + n, v, prec, *p == 'e' || *p == 'E', *p == 'g' || *p == 'G');
            fputs_(&f, b, n, width, left, pad);
            break; }
        case '%': fput(&f, '%'); break;
        case 0: p--; break;
        default: fput(&f, '%'); fput(&f, *p); break;
        }
    }
    if (cap) out[f.n < cap ? f.n : cap - 1] = 0;
    return (int)f.n;
}
int snprintf(char* out, size_t cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, cap, fmt, ap);
    va_end(ap);
    return r;
}
int sprintf(char* out, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(out, 1 << 20, fmt, ap);
    va_end(ap);
    return r;
}
static char logbuf[1024];
int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(logbuf, sizeof(logbuf), fmt, ap);
    va_end(ap);
    nwk_log(logbuf);
    return r;
}
int fprintf(void* fp, const char* fmt, ...) {
    (void)fp;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(logbuf, sizeof(logbuf), fmt, ap);
    va_end(ap);
    nwk_log(logbuf);
    return r;
}
int vfprintf(void* fp, const char* fmt, va_list ap) { (void)fp; int r = vsnprintf(logbuf, sizeof(logbuf), fmt, ap); nwk_log(logbuf); return r; }
int fputs(const char* s, void* fp) { (void)fp; nwk_log(s); return 0; }
int putchar(int c) { return c; }
int fflush(void* fp) { (void)fp; return 0; }
size_t fwrite(const void* p, size_t a, size_t b, void* fp) { (void)p; (void)fp; return a * b; }
int fputc(int c, void* fp) { (void)fp; return c; }
int putc(int c, void* fp) { (void)fp; return c; }
void* stdout;
void* stderr;

double strtod(const char* s, char** end) {
    JSATODTempMem tm;
    const char* e = s;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    double v = js_atod(s, &e, 10, 0, &tm);
    if (end) *end = (char*)e;
    return v;
}

int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    long long ms = nwk_epoch_ms();
    tv->tv_sec = (time_t)(ms / 1000);
    tv->tv_usec = (suseconds_t)((ms % 1000) * 1000);
    return 0;
}
int clock_gettime(clockid_t id, struct timespec* ts) {
    if (id == CLOCK_REALTIME) {
        long long ms = nwk_epoch_ms();
        ts->tv_sec = (time_t)(ms / 1000);
        ts->tv_nsec = (long)((ms % 1000) * 1000000);
    } else {
        unsigned long ms = nwk_ms();
        ts->tv_sec = (time_t)(ms / 1000);
        ts->tv_nsec = (long)((ms % 1000) * 1000000);
    }
    return 0;
}
time_t time(time_t* t) { time_t v = (time_t)(nwk_epoch_ms() / 1000); if (t) *t = v; return v; }
struct tm* localtime_r(const time_t* t, struct tm* r) {
    long long s = (long long)*t + (long long)nwk_tz_min() * 60;
    long long days = s / 86400, rem = s % 86400;
    if (rem < 0) { rem += 86400; days--; }
    r->tm_hour = (int)(rem / 3600);
    r->tm_min = (int)(rem % 3600 / 60);
    r->tm_sec = (int)(rem % 60);
    r->tm_wday = (int)((days + 4) % 7);
    if (r->tm_wday < 0) r->tm_wday += 7;
    long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    long long d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    r->tm_year = (int)(y - 1900);
    r->tm_mon = (int)(m - 1);
    r->tm_mday = (int)d;
    static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    r->tm_yday = cum[r->tm_mon] + r->tm_mday - 1 + (leap && r->tm_mon > 1);
    r->tm_isdst = 0;
    r->tm_gmtoff = nwk_tz_min() * 60;
    r->tm_zone = "LOCAL";
    return r;
}

int pthread_mutex_init(void* m, const void* a) { (void)m; (void)a; return 0; }
int pthread_mutex_lock(void* m) { (void)m; return 0; }
int pthread_mutex_unlock(void* m) { (void)m; return 0; }
int pthread_mutex_destroy(void* m) { (void)m; return 0; }
int pthread_cond_init(void* c, const void* a) { (void)c; (void)a; return 0; }
int pthread_cond_destroy(void* c) { (void)c; return 0; }
int pthread_cond_signal(void* c) { (void)c; return 0; }
int pthread_cond_broadcast(void* c) { (void)c; return 0; }
int pthread_cond_wait(void* c, void* m) { (void)c; (void)m; return 0; }
int pthread_cond_timedwait(void* c, void* m, const void* t) { (void)c; (void)m; (void)t; return 110; }
int pthread_condattr_init(void* a) { (void)a; return 0; }
int pthread_condattr_destroy(void* a) { (void)a; return 0; }
int pthread_condattr_setclock(void* a, int c) { (void)a; (void)c; return 0; }
int pthread_once(int* o, void (*fn)(void)) { if (!*o) { *o = 1; fn(); } return 0; }

double atof(const char* s) { return strtod(s, 0); }
long strtol(const char* s, char** end, int base) {
    const char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
    else if (base == 0 && p[0] == '0') base = 8;
    else if (base == 0) base = 10;
    long v = 0;
    const char* st = p;
    for (;;) {
        int d = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'z') ? *p - 'a' + 10 : (*p >= 'A' && *p <= 'Z') ? *p - 'A' + 10 : 99;
        if (d >= base) break;
        v = v * base + d;
        p++;
    }
    if (end) *end = (char*)(p == st ? s : p);
    return neg ? -v : v;
}
unsigned long strtoul(const char* s, char** end, int base) { return (unsigned long)strtol(s, end, base); }
int atoi(const char* s) { return (int)strtol(s, 0, 10); }
long atol(const char* s) { return strtol(s, 0, 10); }
char* strcat(char* d, const char* s) { char* r = d; while (*d) d++; while ((*d++ = *s++)) {} return r; }
