#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <utime.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>

#define SPOOL "/var/spool/cron/crontabs"
#define SYSTAB "/etc/crontab"
#define SYSDIR "/etc/cron.d"
#define LOGDIR "/var/log/cron"
#define REBOOTMARK "/run/cron.reboot"
#define MAXJOBS 512
#define MAXENV 1024
#define MAXRUN 96
#define USERRUN 16
#define USERJOBS 64
#define USERENV 64
#define LOGMAX 65536
#define CATCHUP (7*24*60)

enum { F_DOMSTAR=1, F_DOWSTAR=2, F_REBOOT=4, F_WILD=8 };
typedef struct { unsigned long long min; unsigned hour, dom; unsigned short mon; unsigned char dow, flags; } sched_t;
typedef struct { sched_t s; uid_t uid; gid_t gid; char user[32], home[128]; char* cmd; int env0, envn, fired; } job_t;

static job_t jobs[MAXJOBS]; static int njobs;
static char* envs[MAXENV]; static int nenv;
static pid_t running[MAXRUN]; static uid_t runuid[MAXRUN]; static int nrunning;
static volatile sig_atomic_t g_hup, g_term;
static const char* mons[] = {"jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec",0};
static const char* dows[] = {"sun","mon","tue","wed","thu","fri","sat",0};

static void say(const char* fmt, ...) __attribute__((format(printf,1,2)));
static void say(const char* fmt, ...)
{
    char b[64]; time_t t = time(0); struct tm tm; localtime_r(&t, &tm); strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tm);
    va_list ap; va_start(ap, fmt); printf("%s cron: ", b); vprintf(fmt, ap); va_end(ap); putchar('\n'); fflush(stdout);
}

static int num(const char** p, int lo, int hi, const char* const* names, int* out)
{
    const char* s = *p;
    if (isdigit((unsigned char)*s)) { long v = 0; while (isdigit((unsigned char)*s)) { v = v*10 + (*s++ - '0'); if (v > 1000) return -1; } if (v < lo || v > hi) return -1; *out = (int)v; *p = s; return 0; }
    if (names) for (int i = 0; names[i]; i++) if (!strncasecmp(s, names[i], 3) && !isalpha((unsigned char)s[3])) { *out = i + lo; *p = s + 3; return 0; }
    return -1;
}

int cron_field(const char* s, int lo, int hi, const char* const* names, unsigned long long* bits)
{
    *bits = 0;
    if (!*s) return -1;
    for (;;) {
        int a, b, step = 1, star = 0;
        if (*s == '*') { a = lo; b = hi; s++; star = 1; }
        else { if (num(&s, lo, hi, names, &a)) return -1; b = a; if (*s == '-') { s++; if (num(&s, lo, hi, names, &b) || b < a) return -1; } }
        if (*s == '/') { s++; if (!isdigit((unsigned char)*s)) return -1; long v = 0; while (isdigit((unsigned char)*s)) { v = v*10 + (*s++ - '0'); if (v > 1000) return -1; } if (v < 1) return -1; step = (int)v; if (!star && b == a) b = hi; }
        for (int i = a; i <= b; i += step) *bits |= 1ULL << i;
        if (*s == ',') { s++; continue; }
        return *s ? -1 : 0;
    }
}

static char* tok(char** p)
{
    char* s = *p; while (*s == ' ' || *s == '\t') s++;
    if (!*s) { *p = s; return 0; }
    char* t = s; while (*s && *s != ' ' && *s != '\t') s++;
    if (*s) *s++ = 0;
    *p = s; return t;
}

int cron_sched(char** line, sched_t* sc)
{
    memset(sc, 0, sizeof *sc);
    char* f[5]; unsigned long long b;
    char* p = *line; while (*p == ' ' || *p == '\t') p++;
    if (*p == '@') {
        char* k = tok(&p); static const struct { const char* n; const char* e; } at[] = {
            {"@yearly","0 0 1 1 *"},{"@annually","0 0 1 1 *"},{"@monthly","0 0 1 * *"},{"@weekly","0 0 * * 0"},
            {"@daily","0 0 * * *"},{"@midnight","0 0 * * *"},{"@hourly","0 * * * *"},{0,0}};
        if (!strcmp(k, "@reboot")) { sc->flags = F_REBOOT; *line = p; return 0; }
        for (int i = 0; at[i].n; i++) if (!strcmp(k, at[i].n)) { char tmp[32]; snprintf(tmp, sizeof tmp, "%s", at[i].e); char* q = tmp; if (cron_sched(&q, sc)) return -1; *line = p; return 0; }
        return -1;
    }
    for (int i = 0; i < 5; i++) if (!(f[i] = tok(&p))) return -1;
    if (cron_field(f[0], 0, 59, 0, &sc->min)) return -1;
    if (cron_field(f[1], 0, 23, 0, &b)) return -1;
    sc->hour = (unsigned)b;
    if (cron_field(f[2], 1, 31, 0, &b)) return -1;
    sc->dom = (unsigned)b;
    if (cron_field(f[3], 1, 12, mons, &b)) return -1;
    sc->mon = (unsigned short)b;
    if (cron_field(f[4], 0, 7, dows, &b)) return -1;
    sc->dow = (unsigned char)((b | (b >> 7 & 1)) & 0x7f);
    if (f[2][0] == '*') sc->flags |= F_DOMSTAR;
    if (f[4][0] == '*') sc->flags |= F_DOWSTAR;
    if (f[0][0] == '*' || f[1][0] == '*') sc->flags |= F_WILD;
    *line = p; return 0;
}

int cron_match(const sched_t* s, const struct tm* t)
{
    if (s->flags & F_REBOOT) return 0;
    if (!(s->min >> t->tm_min & 1) || !(s->hour >> t->tm_hour & 1) || !(s->mon >> (t->tm_mon + 1) & 1)) return 0;
    int d = s->dom >> t->tm_mday & 1, w = s->dow >> t->tm_wday & 1;
    if ((s->flags & (F_DOMSTAR|F_DOWSTAR)) == 0) return d || w;
    return d && w;
}

static int envline(char* p, char** name, char** val)
{
    while (*p == ' ' || *p == '\t') p++;
    if (!isalpha((unsigned char)*p) && *p != '_') return 0;
    char* n = p; while (isalnum((unsigned char)*p) || *p == '_') p++;
    char* e = p; while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return 0;
    *e = 0; p++; while (*p == ' ' || *p == '\t') p++;
    size_t l = strlen(p); while (l && (p[l-1] == ' ' || p[l-1] == '\t')) p[--l] = 0;
    if (l >= 2 && (p[0] == '"' || p[0] == '\'') && p[l-1] == p[0]) { p[l-1] = 0; p++; }
    *name = n; *val = p; return 1;
}

typedef int (*job_cb)(void* ctx, const sched_t* s, const char* user, const char* cmd, int env0, int envn);

int cron_parse(FILE* f, int sys, job_cb cb, void* ctx, int* badline)
{
    char line[4096]; int ln = 0, env0 = nenv, errs = 0, added = 0;
    while (fgets(line, sizeof line, f)) {
        ln++;
        size_t l = strlen(line); if (l && line[l-1] != '\n' && !feof(f)) { int c; while ((c = fgetc(f)) != EOF && c != '\n'); if (!errs++ && badline) *badline = ln; continue; }
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
        char* p = line; while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        char *n, *v;
        if (*p != '@' && *p != '*' && !isdigit((unsigned char)*p) && envline(p, &n, &v)) {
            if (nenv < MAXENV && (sys || nenv - env0 < USERENV)) { char* e = malloc(strlen(n) + strlen(v) + 2); if (e) { sprintf(e, "%s=%s", n, v); envs[nenv++] = e; } }
            continue;
        }
        sched_t sc; const char* user = 0;
        if (cron_sched(&p, &sc) || (sys && !(user = tok(&p)))) { if (!errs++ && badline) *badline = ln; continue; }
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) { if (!errs++ && badline) *badline = ln; continue; }
        if (!sys && added >= USERJOBS) { if (!errs++ && badline) *badline = ln; continue; }
        if (cb && cb(ctx, &sc, user, p, env0, nenv - env0)) errs++; else added++;
    }
    return errs;
}

static void env_reset(void) { for (int i = 0; i < nenv; i++) free(envs[i]); nenv = 0; }
static void jobs_reset(void) { for (int i = 0; i < njobs; i++) free(jobs[i].cmd); njobs = 0; env_reset(); }

static int add_job(void* ctx, const sched_t* s, const char* user, const char* cmd, int env0, int envn)
{
    const char* owner = ctx;
    if (njobs >= MAXJOBS) return 1;
    struct passwd* pw = getpwnam(user ? user : owner);
    if (!pw) { say("unknown user %s, job skipped", user ? user : owner); return 1; }
    job_t* j = &jobs[njobs];
    memset(j, 0, sizeof *j); j->s = *s; j->uid = pw->pw_uid; j->gid = pw->pw_gid;
    snprintf(j->user, sizeof j->user, "%s", pw->pw_name); snprintf(j->home, sizeof j->home, "%s", pw->pw_dir && *pw->pw_dir ? pw->pw_dir : "/");
    if (!(j->cmd = strdup(cmd))) return 1;
    j->env0 = env0; j->envn = envn; njobs++; return 0;
}

static int safe_file(const char* path, uid_t want, struct stat* st)
{
    if (lstat(path, st) || !S_ISREG(st->st_mode)) return 0;
    if (st->st_uid != want) { say("%s: wrong owner (uid %d), ignored", path, (int)st->st_uid); return 0; }
    if (st->st_mode & 022) { say("%s: group/other writable, ignored", path); return 0; }
    if (st->st_nlink != 1) { say("%s: has hard links, ignored", path); return 0; }
    return 1;
}

static int okname(const char* n)
{
    if (!*n || *n == '.' || strlen(n) > 31) return 0;
    for (; *n; n++) if (!isalnum((unsigned char)*n) && *n != '_' && *n != '-' && *n != '.') return 0;
    return 1;
}

static void load_file(const char* path, int sys, const char* owner, uid_t want)
{
    struct stat st; if (!safe_file(path, want, &st)) return;
    int fd = open(path, O_RDONLY|O_NOFOLLOW|O_CLOEXEC); if (fd < 0) return;
    struct stat s2; if (fstat(fd, &s2) || s2.st_ino != st.st_ino || s2.st_dev != st.st_dev || s2.st_uid != want || (s2.st_mode & 022) || s2.st_nlink != 1 || !S_ISREG(s2.st_mode)) { say("%s: changed while opening, ignored", path); close(fd); return; }
    FILE* f = fdopen(fd, "r"); if (!f) { close(fd); return; }
    int bad = 0, e = cron_parse(f, sys, add_job, (void*)owner, &bad); fclose(f);
    if (e) say("%s: %d error(s), first at line %d", path, e, bad);
}

static unsigned long long sig_dir(const char* d, unsigned long long h)
{
    struct stat st; if (stat(d, &st)) return h * 31;
    h = h * 1000003 ^ (unsigned long long)st.st_mtime ^ (unsigned long long)st.st_ino << 20;
    DIR* dp = opendir(d); if (!dp) return h;
    struct dirent* e; char p[512];
    while ((e = readdir(dp))) { if (e->d_name[0] == '.') continue; snprintf(p, sizeof p, "%s/%s", d, e->d_name); if (!lstat(p, &st)) h = h * 1000003 ^ (unsigned long long)st.st_mtime ^ (unsigned long long)st.st_size << 32 ^ st.st_uid ^ (unsigned long long)st.st_mode << 40; }
    closedir(dp); return h;
}

static unsigned long long signature(void)
{
    struct stat st; unsigned long long h = 1469598103934665603ULL;
    if (!lstat(SYSTAB, &st)) h ^= (unsigned long long)st.st_mtime ^ (unsigned long long)st.st_size << 32;
    return sig_dir(SPOOL, sig_dir(SYSDIR, h));
}

static void load_all(void)
{
    jobs_reset();
    load_file(SYSTAB, 1, "root", 0);
    DIR* d; struct dirent* e; char p[512];
    if ((d = opendir(SYSDIR))) { while ((e = readdir(d))) if (okname(e->d_name) && !strchr(e->d_name, '~')) { snprintf(p, sizeof p, "%s/%s", SYSDIR, e->d_name); load_file(p, 1, "root", 0); } closedir(d); }
    struct stat sp; int spok = !lstat(SPOOL, &sp) && S_ISDIR(sp.st_mode) && sp.st_uid == 0 && (sp.st_mode & 07777) == 01733;
    if (!spok) say("%s is not a root-owned 1733 directory, user crontabs ignored", SPOOL);
    if (spok && (d = opendir(SPOOL))) {
        while ((e = readdir(d))) {
            if (!okname(e->d_name)) continue;
            struct passwd* pw = getpwnam(e->d_name); if (!pw) { say("crontab for unknown user %s ignored", e->d_name); continue; }
            snprintf(p, sizeof p, "%s/%s", SPOOL, e->d_name); load_file(p, 0, e->d_name, pw->pw_uid);
        }
        closedir(d);
    }
    say("loaded %d job(s)", njobs);
}

static int open_log(const job_t* j)
{
    char p[256], o[260]; struct stat st;
    snprintf(p, sizeof p, LOGDIR "/%s.log", j->user);
    if (!lstat(p, &st) && S_ISREG(st.st_mode) && st.st_size > LOGMAX) { snprintf(o, sizeof o, "%s.1", p); rename(p, o); }
    int fd = open(p, O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_NOCTTY, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); return -1; }
    if (fchown(fd, j->uid, j->gid) && j->uid) { close(fd); return -1; }
    return fd;
}

static void child(const job_t* j)
{
    for (int s = 1; s < 32; s++) signal(s, SIG_DFL);
    sigset_t m; sigemptyset(&m); sigprocmask(SIG_SETMASK, &m, 0);
    setsid();
    int lf = open_log(j); if (lf < 0) lf = open("/dev/null", O_WRONLY);
    char hdr[256]; time_t t = time(0); struct tm tm; localtime_r(&t, &tm); strftime(hdr, sizeof hdr, "--- %Y-%m-%d %H:%M ", &tm);
    if (lf >= 0) dprintf(lf, "%s%.200s\n", hdr, j->cmd);
    int nul = open("/dev/null", O_RDONLY);
    if (nul >= 0 && nul != 0) { dup2(nul, 0); close(nul); }
    if (lf >= 0) { dup2(lf, 1); dup2(lf, 2); if (lf > 2) close(lf); }
    if (j->uid) {
        if (setgroups(0, 0) && errno != ENOSYS) _exit(126);
        initgroups(j->user, j->gid);
        if (setgid(j->gid) || setuid(j->uid)) { fprintf(stderr, "cron: cannot switch to %s\n", j->user); _exit(126); }
        if (getuid() != j->uid || geteuid() != j->uid || getegid() != j->gid || !setuid(0)) { fprintf(stderr, "cron: privilege drop failed\n"); _exit(126); }
    }
    if (chdir(j->home) && chdir("/")) _exit(126);
    char* ev[MAXENV + 8]; int c = 0; static char h[160], u[48], l[48], sh[160], pa[160];
    snprintf(h, sizeof h, "HOME=%s", j->home); snprintf(u, sizeof u, "USER=%s", j->user); snprintf(l, sizeof l, "LOGNAME=%s", j->user);
    snprintf(sh, sizeof sh, "SHELL=/bin/sh"); snprintf(pa, sizeof pa, "PATH=%s", j->uid ? "/usr/bin:/bin" : "/usr/bin:/bin:/usr/sbin:/sbin");
    ev[c++] = h; ev[c++] = u; ev[c++] = l; ev[c++] = sh; ev[c++] = pa;
    for (int i = j->env0; i < j->env0 + j->envn && i < nenv; i++) {
        if (!strncmp(envs[i], "LOGNAME=", 8) || !strncmp(envs[i], "USER=", 5) || !strncmp(envs[i], "MAILTO=", 7)) continue;
        size_t k = strcspn(envs[i], "=") + 1; int r = -1;
        for (int q = 0; q < c; q++) if (!strncmp(ev[q], envs[i], k)) r = q;
        if (r >= 0) ev[r] = envs[i]; else if (c < MAXENV + 6) ev[c++] = envs[i];
    }
    ev[c] = 0;
    const char* shell = "/bin/sh";
    for (int q = 0; q < c; q++) if (!strncmp(ev[q], "SHELL=", 6) && ev[q][6] == '/') shell = ev[q] + 6;
    execle(shell, "sh", "-c", j->cmd, (char*)0, ev);
    fprintf(stderr, "cron: exec %s: %s\n", shell, strerror(errno)); _exit(127);
}

static void reap(void)
{
    int st; pid_t p;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
        for (int i = 0; i < nrunning; i++) if (running[i] == p) { running[i] = running[--nrunning]; runuid[i] = runuid[nrunning]; break; }
        if (WIFEXITED(st) && WEXITSTATUS(st)) say("pid %d exited with status %d", (int)p, WEXITSTATUS(st));
        else if (WIFSIGNALED(st)) say("pid %d killed by signal %d", (int)p, WTERMSIG(st));
    }
}

static void run_job(job_t* j)
{
    reap();
    if (nrunning >= MAXRUN) { say("too many running jobs, skipped (%s) %.80s", j->user, j->cmd); return; }
    if (j->uid) { int c = 0; for (int i = 0; i < nrunning; i++) if (runuid[i] == j->uid) c++; if (c >= USERRUN || nrunning >= MAXRUN - USERRUN) { say("(%s) has too many jobs running, skipped %.80s", j->user, j->cmd); return; } }
    pid_t p = fork();
    if (p < 0) { say("fork failed: %s", strerror(errno)); return; }
    if (!p) child(j);
    running[nrunning] = p; runuid[nrunning++] = j->uid;
    say("(%s) pid %d CMD %.200s", j->user, (int)p, j->cmd);
}

static void on_sig(int s) { if (s == SIGHUP) g_hup = 1; else g_term = 1; }

static time_t boot_time(void)
{
    struct sysinfo si; if (sysinfo(&si)) return -1;
    return time(0) - si.uptime;
}

static int first_since_boot(void)
{
    time_t bt = boot_time(); if (bt < 0) return 1;
    long prev = 0; FILE* f = fopen(REBOOTMARK, "r"); if (f) { if (fscanf(f, "%ld", &prev) != 1) prev = 0; fclose(f); }
    if (prev && labs(prev - (long)bt) < 120) return 0;
    { int fd = open(REBOOTMARK, O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC, 0644); if (fd >= 0) { dprintf(fd, "%ld\n", (long)bt); close(fd); } }
    return 1;
}

static void dirs(void)
{
    mkdir("/var/spool", 0755); mkdir("/var/spool/cron", 0755); mkdir(SPOOL, 01733); chown(SPOOL, 0, 0); chmod(SPOOL, 01733);
    mkdir("/var/log", 0755); mkdir(LOGDIR, 0755); chmod(LOGDIR, 0755); mkdir("/run", 0755); mkdir(SYSDIR, 0755);
}

int cron_catchup(time_t last, time_t now, time_t noreplay, void (*fire)(job_t*))
{
    int ran = 0;
    if (now - last > (time_t)CATCHUP * 60) last = now - (time_t)CATCHUP * 60;
    for (int i = 0; i < njobs; i++) jobs[i].fired = 0;
    for (time_t t = last + 60; t <= now; t += 60) {
        struct tm tm; localtime_r(&t, &tm);
        for (int i = 0; i < njobs; i++) {
            job_t* j = &jobs[i];
            if (j->fired || !cron_match(&j->s, &tm)) continue;
            if (t <= noreplay && !(j->s.flags & F_WILD)) continue;
            j->fired = 1; fire(j); ran++;
        }
    }
    return ran;
}

static int daemon_main(int fg)
{
    if (getuid()) { fprintf(stderr, "cron: must run as root\n"); return 1; }
    if (!fg) { pid_t p = fork(); if (p < 0) return 1; if (p) return 0; setsid(); }
    umask(022); dirs();
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_handler = on_sig; sigaction(SIGHUP, &sa, 0); sigaction(SIGTERM, &sa, 0); sigaction(SIGINT, &sa, 0);
    signal(SIGPIPE, SIG_IGN);
    say("starting (pid %d)", (int)getpid());
    load_all(); unsigned long long sig = signature();
    if (first_since_boot()) for (int i = 0; i < njobs; i++) if (jobs[i].s.flags & F_REBOOT) run_job(&jobs[i]);
    time_t last = time(0) / 60 * 60, noreplay = 0;
    while (!g_term) {
        time_t now = time(0); int left = 60 - (int)(now % 60); if (left < 1 || left > 60) left = 60;
        struct timespec ts = { left, 50000000 };
        while (nanosleep(&ts, &ts) && errno == EINTR && !g_term && !g_hup) reap();
        reap();
        if (g_term) break;
        unsigned long long s2 = signature();
        if (g_hup || s2 != sig) { g_hup = 0; sig = s2; load_all(); }
        now = time(0) / 60 * 60;
        if (now < last) { say("clock went back %ld s", (long)(last - now)); if (last - now < 3 * 3600) noreplay = last; last = now - 60; }
        if (now > last) {
            if (now - last > 120) say("woke after %ld minute(s), running missed jobs once", (long)((now - last) / 60));
            cron_catchup(last, now, noreplay, run_job); last = now;
        }
    }
    say("stopping"); return 0;
}

static int spool_path(char* out, size_t n, const char* user) { if (!okname(user)) return -1; snprintf(out, n, SPOOL "/%s", user); return 0; }

static int check_tab(const char* path, int* badline)
{
    FILE* f = fopen(path, "r"); if (!f) return -1;
    int e = cron_parse(f, 0, 0, 0, badline); fclose(f); env_reset(); return e;
}

static int install(const char* src, const struct passwd* pw)
{
    char dst[256], tmp[300]; if (spool_path(dst, sizeof dst, pw->pw_name)) return -1;
    int bad = 0, e = check_tab(src, &bad);
    if (e < 0) { fprintf(stderr, "crontab: %s: %s\n", src, strerror(errno)); return -1; }
    if (e) { fprintf(stderr, "crontab: %s: error on line %d, not installed\n", src, bad); return -1; }
    snprintf(tmp, sizeof tmp, SPOOL "/.tmp.%s.%d", pw->pw_name, (int)getpid());
    int in = open(src, O_RDONLY); if (in < 0) { perror("crontab"); return -1; }
    int out = open(tmp, O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, 0600);
    if (out < 0) { fprintf(stderr, "crontab: %s: %s\n", SPOOL, strerror(errno)); close(in); return -1; }
    char b[4096]; ssize_t r; int ok = 1;
    while ((r = read(in, b, sizeof b)) > 0) if (write(out, b, (size_t)r) != r) { ok = 0; break; }
    if (r < 0) ok = 0;
    close(in);
    if (ok && !getuid() && fchown(out, pw->pw_uid, pw->pw_gid)) ok = 0;
    if (ok && fchmod(out, 0600)) ok = 0;
    if (close(out)) ok = 0;
    if (!ok || rename(tmp, dst)) { fprintf(stderr, "crontab: install failed: %s\n", strerror(errno)); unlink(tmp); return -1; }
    utime(SPOOL, 0);
    return 0;
}

static int copy_to_fd(const char* path, int fd)
{
    int in = open(path, O_RDONLY|O_NOFOLLOW); if (in < 0) return -1;
    char b[4096]; ssize_t r; while ((r = read(in, b, sizeof b)) > 0) if (write(fd, b, (size_t)r) != r) { close(in); return -1; }
    close(in); return r < 0 ? -1 : 0;
}

static int edit(const struct passwd* pw)
{
    char dst[256], tmp[] = "/tmp/crontab.XXXXXX"; if (spool_path(dst, sizeof dst, pw->pw_name)) return 1;
    int fd = mkstemp(tmp); if (fd < 0) { perror("crontab: mkstemp"); return 1; }
    if (copy_to_fd(dst, fd) && errno != ENOENT) { perror("crontab"); close(fd); unlink(tmp); return 1; }
    struct stat st0; fstat(fd, &st0); close(fd);
    const char* ed = getenv("VISUAL"); if (!ed || !*ed) ed = getenv("EDITOR"); if (!ed || !*ed) ed = "vi";
    for (int tries = 0; tries < 10; tries++) {
        pid_t p = fork();
        if (p < 0) { perror("crontab: fork"); break; }
        if (!p) { execl("/bin/sh", "sh", "-c", "exec $0 \"$1\"", ed, tmp, (char*)0); _exit(127); }
        int s; while (waitpid(p, &s, 0) < 0 && errno == EINTR);
        if (!WIFEXITED(s) || WEXITSTATUS(s)) { fprintf(stderr, "crontab: editor failed, no changes made\n"); break; }
        struct stat st; if (!stat(tmp, &st) && st.st_mtime == st0.st_mtime && st.st_size == st0.st_size) { fprintf(stderr, "crontab: no changes made\n"); unlink(tmp); return 0; }
        int bad = 0, e = check_tab(tmp, &bad);
        if (!e) { int r = install(tmp, pw); unlink(tmp); if (!r) fprintf(stderr, "crontab: installing new crontab\n"); return r ? 1 : 0; }
        fprintf(stderr, "crontab: error on line %d. Edit again? (y/n) ", bad);
        char a[16]; if (!fgets(a, sizeof a, stdin) || (a[0] != 'y' && a[0] != 'Y')) break;
    }
    fprintf(stderr, "crontab: edits left in %s\n", tmp); return 1;
}

static int crontab_main(int argc, char** argv)
{
    const char* user = 0; char op = 0; const char* file = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-u") && i + 1 < argc) user = argv[++i];
        else if (!strcmp(argv[i], "-l") || !strcmp(argv[i], "-r") || !strcmp(argv[i], "-e")) { if (op) goto usage; op = argv[i][1]; }
        else if (!strcmp(argv[i], "-") || argv[i][0] != '-') { if (file) goto usage; file = argv[i]; }
        else goto usage;
    }
    if (!op && !file) goto usage;
    if (op && file) goto usage;
    struct passwd* pw;
    if (user) { if (getuid()) { fprintf(stderr, "crontab: only root may use -u\n"); return 1; } pw = getpwnam(user); }
    else pw = getpwuid(getuid());
    if (!pw) { fprintf(stderr, "crontab: unknown user\n"); return 1; }
    char dst[256]; if (spool_path(dst, sizeof dst, pw->pw_name)) { fprintf(stderr, "crontab: bad user name\n"); return 1; }
    if (op == 'l') { if (copy_to_fd(dst, 1)) { if (errno == ENOENT) fprintf(stderr, "no crontab for %s\n", pw->pw_name); else perror("crontab"); return 1; } return 0; }
    if (op == 'r') { if (unlink(dst)) { if (errno == ENOENT) fprintf(stderr, "no crontab for %s\n", pw->pw_name); else perror("crontab"); return 1; } utime(SPOOL, 0); return 0; }
    if (op == 'e') return edit(pw);
    if (!strcmp(file, "-")) {
        char tmp[] = "/tmp/crontab.XXXXXX"; int fd = mkstemp(tmp); if (fd < 0) { perror("crontab"); return 1; }
        char b[4096]; ssize_t r; while ((r = read(0, b, sizeof b)) > 0) if (write(fd, b, (size_t)r) != r) { r = -1; break; }
        close(fd); int rc = r < 0 ? -1 : install(tmp, pw); unlink(tmp); return rc ? 1 : 0;
    }
    return install(file, pw) ? 1 : 0;
usage:
    fprintf(stderr, "usage: crontab [-u user] file|-\n       crontab [-u user] -l|-r|-e\n"); return 2;
}

#ifndef CRON_NO_MAIN
int main(int argc, char** argv)
{
    const char* b = strrchr(argv[0], '/'); b = b ? b + 1 : argv[0];
    if (!strcmp(b, "crontab")) return crontab_main(argc, argv);
    int fg = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f")) fg = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) { int bad = 0, e = check_tab(argv[++i], &bad); if (e) { fprintf(stderr, "%s: %s at line %d\n", argv[i], e < 0 ? strerror(errno) : "error", bad); return 1; } puts("ok"); return 0; }
        else { fprintf(stderr, "usage: cron [-f] | cron -t FILE\n"); return 2; }
    }
    return daemon_main(fg);
}
#endif
