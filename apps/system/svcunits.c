#include "include/nvx/base.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/vfs.h"
#include "include/nvx/core/proc/elf.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/core/auth/accounts.h"
#include "include/nvx/apps/svcunits.h"
#include "include/nvx/fs/vfs/unixfs.h"

extern volatile u64 g_ticks;
extern void klog(const char*);
extern int disk_ok;
int ux_read_file(const char* p, char* out, int cap);
int ux_write_text(const char* path, const char* body, int len, unsigned mode);

#define SU_MAX 32
#define SU_DIR "/etc/nvx/services"
#define SU_LOGDIR "/var/log/services"
#define SU_LOGMAX 65536

enum { SU_INACTIVE, SU_STARTING, SU_RUNNING, SU_EXITED, SU_FAILED, SU_STOPPING, SU_BACKOFF };
static const char* const SU_STN[] = { "inactive", "starting", "running", "exited", "failed", "stopping", "backoff" };

typedef struct {
    char name[32], desc[80], exec[256], user[33], wd[128], env[512], after[128], why[64];
    int type, restart, rsec, tstop, defen, en, used;
    int state, ji, pid, fails, restarts, lastst, want;
    u64 since, next, kill_at;
} su_t;

static su_t U[SU_MAX];
static int su_n, su_booted;
static u64 su_first, su_rot;

static void su_cat(char* d, int cap, const char* a, const char* b, const char* c) { char* p = d; char* e = d + cap - 1; const char* s[3] = { a, b, c }; for (int k = 0; k < 3; k++) for (const char* q = s[k]; q && *q && p < e; q++) *p++ = *q; *p = 0; }
static void su_log(const char* nm, const char* msg) { char b[160]; su_cat(b, sizeof b, "service ", nm, ": "); int l = 0; while (b[l]) l++; su_cat(b + l, (int)sizeof b - l, msg, 0, 0); klog(b); }
static int su_okname(const char* n) { int l = 0; for (; n[l]; l++) { char c = n[l]; if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '@')) return 0; } return l > 0 && l < 32 && n[0] != '.'; }
static int su_exists(const char* p) { return k_find_file(p) >= 0; }
static void su_mkdirs(const char* path) {
    int save = g_uid, sg = g_gid; g_uid = 0; g_gid = 0; int d = 0; char seg[64]; int i = 1;
    while (path[i - 1] && d >= 0) { int n = 0; while (path[i] && path[i] != '/' && n < 63) seg[n++] = path[i++]; seg[n] = 0; if (n) { int c = dt_child(d, seg); d = c >= 0 ? c : dt_mkdir_at(d, seg); } if (!path[i]) break; i++; }
    g_uid = save; g_gid = sg; dt_save();
}
static void su_path(char* o, int cap, const char* dir, const char* nm, const char* ext) { su_cat(o, cap, dir, "/", nm); int l = 0; while (o[l]) l++; su_cat(o + l, cap - l, ext, 0, 0); }
static int su_dir_ok(void) { int d = dt_resolve_dir(SU_DIR); return d >= 0 && DT[d].uid == 0 && !(DT[d].mode & 022); }
static int su_file_ok(int k) { return k >= 0 && FS[k].uid == 0 && !(FS[k].mode & 022); }
static int su_marker(const char* nm, const char* ext) { char p[96]; su_path(p, sizeof p, SU_DIR, nm, ext); return su_dir_ok() && su_file_ok(k_find_file(p)); }
static int su_enabled_of(su_t* u) { if (su_marker(u->name, ".disabled")) return 0; if (su_marker(u->name, ".enabled")) return 1; return u->defen; }

static const char SU_SSHD[] = "[Unit]\nDescription=SSH server (dropbear)\nAfter=network\n\n[Service]\nType=simple\nExecStart=/usr/bin/dropbear -F -E -R -g -p 22\nUser=root\nRestart=on-failure\nRestartSec=2\nTimeoutStopSec=5\n\n[Install]\nEnabled=no\n";
static const char SU_CRON[] = "[Unit]\nDescription=Runs scheduled jobs (cron)\n\n[Service]\nType=simple\nExecStart=/usr/sbin/cron -f\nUser=root\nRestart=on-failure\nRestartSec=5\n\n[Install]\nEnabled=yes\n";
static const char SU_CRON_OFF[] = "[Unit]\nDescription=Runs scheduled jobs (cron)\n\n[Service]\nType=simple\nExecStart=/usr/sbin/cron -f\nUser=root\nRestart=on-failure\nRestartSec=5\n\n[Install]\nEnabled=no\n";
#define SU_SPOOL "/var/spool/cron/crontabs"
static void su_cron_spool(void) {
    u32 L = 0; if (!jc_find_prog("/usr/sbin/cron", &L)) return;
    su_mkdirs(SU_SPOOL);
    int save = g_uid, sg = g_gid; g_uid = 0; g_gid = 0;
    int d = dt_resolve_dir(SU_SPOOL);
    if (d >= 0 && (DT[d].uid != 0 || DT[d].gid != 0 || (DT[d].mode & 07777) != 01733)) { ux_chown(SU_SPOOL, 0, 0, 0); ux_chmod(SU_SPOOL, 01733); }
    d = dt_resolve_dir(SU_SPOOL); int sok = d >= 0 && DT[d].uid == 0 && (DT[d].mode & 07777) == 01733;
    u32 st[10]; int eok = ux_stat2("/etc/crontab", st, 0) == 0 && st[1] == 0 && (st[0] & 0170000u) == 0100000u && !(st[0] & 022);
    g_uid = save; g_gid = sg;
    klog(sok && eok ? "cron: perms ok - " SU_SPOOL " root 1733, /etc/crontab root-owned regular file, not group/other writable"
       : !sok ? "cron: perms WARNING - " SU_SPOOL " is not a root 1733 directory, user crontabs are ignored"
       : "cron: perms WARNING - /etc/crontab is missing, not root-owned or group/other writable, cron ignores it");
}
static void su_seed(void) {
    su_mkdirs(SU_DIR); su_mkdirs(SU_LOGDIR);
    int save = g_uid; g_uid = 0;
    if (!su_exists(SU_DIR "/sshd.service")) ux_write_text(SU_DIR "/sshd.service", SU_SSHD, (int)sizeof SU_SSHD - 1, 0100644u);
    else { static char t[2048]; int n = ux_read_file(SU_DIR "/sshd.service", t, sizeof t - 1);
        if (n > 0) { t[n] = 0; const char* old = "ExecStart=/usr/bin/dropbear -F -E -R -p 22\n"; int ol = 0; while (old[ol]) ol++;
            for (int i = 0; i + ol <= n; i++) { int m = 1; for (int z = 0; z < ol; z++) if (t[i + z] != old[z]) { m = 0; break; } if (m) { ux_write_text(SU_DIR "/sshd.service", SU_SSHD, (int)sizeof SU_SSHD - 1, 0100644u); break; } } } }
    if (!su_exists(SU_DIR "/cron.service")) { u32 L = 0; if (jc_find_prog("/usr/sbin/cron", &L)) ux_write_text(SU_DIR "/cron.service", SU_CRON, (int)sizeof SU_CRON - 1, 0100644u); else ux_write_text(SU_DIR "/cron.service", SU_CRON_OFF, (int)sizeof SU_CRON_OFF - 1, 0100644u); }
    g_uid = save;
}
static su_t* su_get(const char* nm) { for (int i = 0; i < su_n; i++) if (U[i].used && seq(U[i].name, nm)) return &U[i]; return 0; }
static int su_load(su_t* u) {
    static char t[4096]; char p[96]; su_path(p, sizeof p, SU_DIR, u->name, ".service");
    if (!su_dir_ok()) { su_log(u->name, SU_DIR " not root-owned or writable by others - ignored"); return -1; }
    { int k = k_find_file(p); if (k < 0) return -1; if (!su_file_ok(k)) { su_log(u->name, "unit file not owned by root or writable by others - ignored"); return -1; } }
    int save = g_uid; g_uid = 0; int n = ux_read_file(p, t, sizeof t); g_uid = save;
    if (n <= 0) return -1;
    return svcu_parse(t, n, u->desc, sizeof u->desc, u->exec, sizeof u->exec, u->user, sizeof u->user, u->wd, sizeof u->wd, u->env, sizeof u->env, u->after, sizeof u->after, &u->type, &u->restart, &u->rsec, &u->tstop, &u->defen);
}
int svcu_reload(void) {
    char nm[64]; int ty = 0;
    for (int i = 0; i < su_n; i++) U[i].want = 0;
    for (int k = 0; k < 512 && k_readdir_at(SU_DIR, k, nm, &ty) > 0; k++) {
        int l = 0; while (nm[l]) l++;
        if (l < 9 || !seq(nm + l - 8, ".service")) continue;
        nm[l - 8] = 0; if (!su_okname(nm)) continue;
        su_t* u = su_get(nm);
        if (!u) { int fr = -1; for (int z = 0; z < su_n; z++) if (!U[z].used) { fr = z; break; } if (fr < 0 && su_n >= SU_MAX) break; u = fr >= 0 ? &U[fr] : &U[su_n++]; u8* r = (u8*)u; for (u32 z = 0; z < sizeof *u; z++) r[z] = 0; int q = 0; for (; nm[q]; q++) u->name[q] = nm[q]; u->name[q] = 0; u->ji = -1; u->used = 1; }
        u->want = 1;
        if (su_load(u) < 0) { su_cat(u->why, sizeof u->why, "bad unit file (ExecStart missing or not absolute)", 0, 0); u->exec[0] = 0; }
        else if (u->why[0] == 'b') u->why[0] = 0;
        u->en = su_enabled_of(u);
    }
    int c = 0;
    for (int i = 0; i < su_n; i++) { if (!U[i].want && (U[i].state == SU_INACTIVE || U[i].state == SU_FAILED || U[i].state == SU_EXITED)) U[i].used = 0; if (U[i].used) c++; }
    return c;
}
int svcu_find(const char* name) { for (int i = 0; i < su_n; i++) if (U[i].used && seq(U[i].name, name)) return i; return -1; }
int svcu_count(void) { return su_n; }

static int su_env_build(su_t* u, acct_info* a) {
    int o = 0, n = 0;
    const char* base[5][2] = { { "PATH=", "/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin" }, { "HOME=", a->home[0] ? a->home : "/" }, { "USER=", a->name }, { "LOGNAME=", a->name }, { "SHELL=", a->shell[0] ? a->shell : "/bin/sh" } };
    for (int k = 0; k < 5; k++) { if (n >= XA_MAX - 1) return -1; g_xe_off[n++] = o; for (int z = 0; z < 2; z++) for (const char* s = base[k][z]; *s; s++) { if (o >= XA_CAP - 2) return -1; g_xe_buf[o++] = *s; } g_xe_buf[o++] = 0; }
    for (const char* e = u->env; *e;) { if (n >= XA_MAX - 1) return -1; g_xe_off[n++] = o; while (*e && *e != '\n') { if (o >= XA_CAP - 2) return -1; g_xe_buf[o++] = *e++; } g_xe_buf[o++] = 0; if (*e) e++; }
    g_xe_n = n; return 0;
}
static int su_open_into(int i, int fd, const char* path, int flags) {
    int save = g_jcur, su = g_uid; g_jcur = i; g_uid = 0; int nf = vfs_open(path, flags); g_jcur = save; g_uid = su;
    if (nf < 0) return -1;
    struct jproc* p = &g_jp[i]; if (fd != nf) { if (p->fd[fd] >= 0) vf_unref(p->fd[fd]); p->fd[fd] = p->fd[nf]; p->fd[nf] = -1; }
    return 0;
}
static void su_logline(su_t* u, const char* msg) {
    char p[96]; su_path(p, sizeof p, SU_LOGDIR, u->name, ".log");
    int k = k_find_file(p); int save = g_uid; g_uid = 0;
    char b[200]; su_cat(b, sizeof b, "[service] ", msg, "\n"); int bl = 0; while (b[bl]) bl++;
    if (k < 0) ux_write_text(p, b, bl, 0100640u);
    else { int len = FS[k].len; if (len + bl < SU_LOGMAX * 2) { static char t[SU_LOGMAX * 2]; const char* s = fsd(k); for (int z = 0; z < len; z++) t[z] = s[z]; for (int z = 0; z < bl; z++) t[len + z] = b[z]; ux_write_text(p, t, len + bl, 0); } }
    g_uid = save;
}
static int su_spawn(su_t* u) {
    if (!u->exec[0]) { su_cat(u->why, sizeof u->why, "bad unit file", 0, 0); return -2; }
    static char args[1024]; if (svcu_split_args(u->exec, args, sizeof args) < 0) { su_cat(u->why, sizeof u->why, "ExecStart cannot be parsed", 0, 0); return -2; }
    char prog[160]; int q = 0; while (args[q] && args[q] != ' ' && q < 159) { prog[q] = args[q] == 0x1F ? ' ' : args[q]; q++; } prog[q] = 0;
    u32 L = 0; if (!jc_find_prog(prog, &L)) { su_cat(u->why, sizeof u->why, "program not found: ", prog, 0); return -3; }
    acct_info a; int uid = 0, gid = 0;
    { int num = 1, v = 0; for (const char* s = u->user; *s; s++) { if (*s < '0' || *s > '9') { num = 0; break; } v = v * 10 + (*s - '0'); if (v > 65535) { num = 0; break; } }
      int r = num ? acct_get_uid(v, &a) : acct_get(u->user, &a);
      if (r < 0) { if (num && v == 0) { for (u32 z = 0; z < sizeof a; z++) ((u8*)&a)[z] = 0; scopy(a.name, "root"); scopy(a.home, "/root"); scopy(a.shell, "/bin/sh"); } else { su_cat(u->why, sizeof u->why, "no such user: ", u->user, 0); return -3; } }
      uid = a.uid; gid = a.gid; }
    int cwd = dt_resolve_dir(u->wd); if (cwd < 0) { su_cat(u->why, sizeof u->why, "no such directory: ", u->wd, 0); return -3; }
    char lp[96]; su_path(lp, sizeof lp, SU_LOGDIR, u->name, ".log");
    if (!su_exists(lp)) { su_mkdirs(SU_LOGDIR); int sv = g_uid; g_uid = 0; ux_write_text(lp, "", 0, 0100640u); g_uid = sv; }
    if (su_env_build(u, &a) < 0) { su_cat(u->why, sizeof u->why, "environment too large", 0, 0); return -2; }
    g_xe_set = 1;
    int i = jc_create(args, 0, 0);
    g_xe_set = 0;
    if (i < 0) { su_cat(u->why, sizeof u->why, i == -1 ? "process table full" : "program could not be loaded", 0, 0); return -3; }
    struct jproc* p = &g_jp[i]; p->state = S_STOP;
    p->uid = uid; p->gid = gid; p->cwd = cwd; p->umask = 022; p->sid = p->pid; p->pgid = p->pid; p->aluid = (u32)uid;
    su_open_into(i, 0, "/dev/null", O_RDONLY);
    if (su_open_into(i, 1, lp, O_WRONLY | O_APPEND) == 0) { vf_unref(p->fd[2]); p->fd[2] = p->fd[1]; vf_ref(p->fd[1]); }
    u->ji = i; u->pid = p->pid; u->since = g_ticks; u->why[0] = 0;
    u->state = u->type ? SU_STARTING : SU_RUNNING;
    p->state = S_RUN;
    return 0;
}
static int su_installed(su_t* u) {
    static char args[1024]; if (!u->exec[0] || svcu_split_args(u->exec, args, sizeof args) < 0) return 1;
    char prog[160]; int q = 0; while (args[q] && args[q] != ' ' && q < 159) { prog[q] = args[q] == 0x1F ? ' ' : args[q]; q++; } prog[q] = 0;
    u32 L = 0; if (jc_find_prog(prog, &L)) return 1;
    su_cat(u->why, sizeof u->why, "not installed: ", prog, 0); su_log(u->name, u->why); return 0;
}
static void su_begin(su_t* u) {
    int r = su_spawn(u);
    if (r == 0) { char b[48], *p = b; p = scopy(p, "started, pid "); p = udec(p, (u32)u->pid); *p = 0; su_log(u->name, b); su_logline(u, b); return; }
    u->state = SU_FAILED; u->since = g_ticks; u->ji = -1; u->pid = 0; su_log(u->name, u->why); su_logline(u, u->why);
}
static void su_bootbegin(su_t* u) { if (su_installed(u)) su_begin(u); }
int svcu_start(const char* name) {
    svcu_reload();
    su_t* u = su_get(name); if (!u) return -1;
    if (u->state == SU_RUNNING || u->state == SU_STARTING || u->state == SU_STOPPING) return 0;
    if (!u->exec[0]) return -2;
    u->fails = 0; su_begin(u);
    return u->state == SU_FAILED ? -3 : 0;
}
int svcu_stop(const char* name) {
    su_t* u = su_get(name); if (!u) return -1;
    if (u->state == SU_BACKOFF || u->state == SU_FAILED || u->state == SU_EXITED) { u->state = SU_INACTIVE; u->since = g_ticks; return 0; }
    if (u->state != SU_RUNNING && u->state != SU_STARTING) return 0;
    if (u->ji < 0) { u->state = SU_INACTIVE; return 0; }
    struct jproc* p = &g_jp[u->ji];
    if (p->pid != u->pid || p->state == S_FREE) { u->state = SU_INACTIVE; return 0; }
    u->state = SU_STOPPING; u->kill_at = g_ticks + (u64)u->tstop * 100ull;
    if (p->state != S_ZOMB) { jc_signal_proc(u->ji, 15); jc_signal_proc(u->ji, 18); }
    su_logline(u, "stopping (SIGTERM)");
    return 0;
}
int svcu_restart(const char* name) {
    su_t* u = su_get(name); if (!u) { svcu_reload(); u = su_get(name); if (!u) return -1; }
    if (u->state == SU_RUNNING || u->state == SU_STARTING || u->state == SU_STOPPING) { svcu_stop(name); u->want = 2; return 0; }
    return svcu_start(name);
}
int svcu_enable(const char* name, int on) {
    su_t* u = su_get(name); if (!u) { svcu_reload(); u = su_get(name); if (!u) return -1; }
    char pe[96], pd[96]; su_path(pe, sizeof pe, SU_DIR, name, ".enabled"); su_path(pd, sizeof pd, SU_DIR, name, ".disabled");
    int save = g_uid; g_uid = 0;
    if (on) { k_unlink(pd); ux_write_text(pe, "", 0, 0100644u); } else { k_unlink(pe); ux_write_text(pd, "", 0, 0100644u); }
    g_uid = save; u->en = on ? 1 : 0;
    return 0;
}
int svcu_state(int i, const char** name, const char** desc, const char** state, int* pid, int* enabled) {
    if (i < 0 || i >= su_n || !U[i].used) return 0;
    *name = U[i].name; *desc = U[i].desc[0] ? U[i].desc : U[i].exec; *state = SU_STN[U[i].state]; *pid = (U[i].state == SU_RUNNING || U[i].state == SU_STARTING || U[i].state == SU_STOPPING) ? U[i].pid : 0; *enabled = U[i].en;
    return 1;
}
static int su_reap(su_t* u, int* st) {
    if (u->ji < 0) { *st = -1; return 1; }
    struct jproc* p = &g_jp[u->ji];
    if (p->pid != u->pid || p->state == S_FREE) { *st = -1; return 1; }
    if (p->state == S_ZOMB) { *st = p->exitst; p->state = S_FREE; return 1; }
    return 0;
}
static void su_desc_status(char* b, int cap, int st) {
    char* p = b; char* e = b + cap - 24;
    if (st < 0) p = scopy(p, "exited (status unknown)");
    else if (st & 0x7F) { p = scopy(p, "killed by signal "); p = udec(p, (u32)(st & 0x7F)); }
    else { p = scopy(p, "exited with code "); p = udec(p, (u32)((st >> 8) & 0xFF)); }
    (void)e; *p = 0;
}
static void su_rotate(su_t* u) {
    char p[96], p1[96]; su_path(p, sizeof p, SU_LOGDIR, u->name, ".log"); su_path(p1, sizeof p1, SU_LOGDIR, u->name, ".log.1");
    int k = k_find_file(p); if (k < 0 || FS[k].len <= SU_LOGMAX) return;
    int save = g_uid; g_uid = 0;
    if (ux_write_text(p1, fsd(k), FS[k].len, 0100640u) >= 0) k_truncate(p, 0);
    g_uid = save;
}
static void su_step(su_t* u) {
    if (u->state == SU_RUNNING || u->state == SU_STARTING || u->state == SU_STOPPING) {
        int st; if (!su_reap(u, &st)) { if (u->state == SU_STOPPING && g_ticks >= u->kill_at) { jc_signal_proc(u->ji, 9); u->kill_at = g_ticks + 200; su_logline(u, "did not stop in time, SIGKILL"); } return; }
        char b[64]; su_desc_status(b, sizeof b, st); su_logline(u, b);
        int was = u->state; u->lastst = st; u->ji = -1; u->pid = 0;
        if (was == SU_STOPPING) { u->state = SU_INACTIVE; u->since = g_ticks; if (u->want == 2) { u->want = 1; u->fails = 0; su_begin(u); } return; }
        int ok = st == 0;
        if (g_ticks - u->since > 3000) u->fails = 0;
        if (u->type == 1 && ok) { u->state = SU_EXITED; u->since = g_ticks; return; }
        if (u->restart == 2 || (u->restart == 1 && !ok)) { u->next = g_ticks + svcu_backoff(u->rsec, u->fails); u->fails++; u->restarts++; u->state = SU_BACKOFF; u->since = g_ticks; return; }
        u->state = ok ? SU_INACTIVE : SU_FAILED; u->since = g_ticks; if (!ok) su_log(u->name, b);
        return;
    }
    if (u->state == SU_BACKOFF && g_ticks >= u->next) su_begin(u);
}
static int su_dep_ready(su_t* u) {
    const char* a = u->after;
    while (*a) {
        char nm[32];
        while (*a == ' ') a++;
        int n = 0; while (*a && *a != ' ' && n < 31) nm[n++] = *a++; nm[n] = 0; while (*a && *a != ' ') a++;
        if (!n) continue;
        su_t* d = su_get(nm); if (!d || !d->en || d == u) continue;
        if (d->state == SU_INACTIVE) return 0;
    }
    return 1;
}
void svcu_boot(void) {
    if (su_booted) return;
    su_booted = 1;
    su_seed(); su_cron_spool(); svcu_reload();
    int pend[SU_MAX], np = 0; for (int i = 0; i < su_n; i++) if (U[i].used && U[i].en && U[i].state == SU_INACTIVE) pend[np++] = i;
    for (int pass = 0; pass < SU_MAX && np; pass++) {
        int prog = 0;
        for (int k = 0; k < np; k++) { su_t* u = &U[pend[k]]; if (!su_dep_ready(u) && pass < SU_MAX - 1) continue; su_bootbegin(u); pend[k--] = pend[--np]; prog = 1; }
        if (!prog) { for (int k = 0; k < np; k++) su_bootbegin(&U[pend[k]]); np = 0; }
    }
}
void svcu_tick(void) {
    if (!su_first) su_first = g_ticks ? g_ticks : 1;
    if (!su_booted) { if (g_ticks - su_first < 500) return; extern int g_svcu_noboot; if (!g_svcu_noboot) svcu_boot(); else su_booted = 1; }
    for (int i = 0; i < su_n; i++) if (U[i].used) su_step(&U[i]);
    if (g_ticks - su_rot > 500) { su_rot = g_ticks; for (int i = 0; i < su_n; i++) if (U[i].used) su_rotate(&U[i]); }
}
int g_svcu_noboot;
void svcu_shutdown(int timeout_ms) {
    int any = 0;
    for (int i = 0; i < su_n; i++) { su_t* u = &U[i]; if (u->used && (u->state == SU_RUNNING || u->state == SU_STARTING)) { svcu_stop(u->name); any = 1; } }
    if (!any) return;
    u64 end = g_ticks + (u64)(timeout_ms / 10 + 1);
    for (int guard = 0; guard < 200000 && g_ticks < end; guard++) {
        jc_step(); int live = 0;
        for (int i = 0; i < su_n; i++) { su_t* u = &U[i]; int st; if (u->used && u->state == SU_STOPPING) { if (su_reap(u, &st)) { u->state = SU_INACTIVE; u->ji = -1; u->pid = 0; } else live = 1; } }
        if (!live) return;
        __builtin_ia32_pause();
    }
    for (int i = 0; i < su_n; i++) { su_t* u = &U[i]; if (u->used && u->state == SU_STOPPING && u->ji >= 0 && g_jp[u->ji].pid == u->pid) { jc_signal_proc(u->ji, 9); su_log(u->name, "killed at shutdown"); } }
}
static char* su_since(char* p, u64 since) { u64 s = (g_ticks - since) / 100ull; p = udec(p, (u32)s); return scopy(p, " s ago"); }
void svcu_list(char* out, int cap) {
    char* p = out; char* e = out + cap - 240; *p = 0;
    if (!su_n) svcu_reload();
    for (int i = 0; i < su_n && p < e; i++) {
        su_t* u = &U[i]; if (!u->used) continue;
        p = scopy(p, "  "); const char* s = SU_STN[u->state]; p = scopy(p, s); int l = 0; while (s[l]) l++; while (l++ < 9) *p++ = ' ';
        p = scopy(p, u->name); l = 0; while (u->name[l]) l++; while (l++ < 11) *p++ = ' ';
        p = scopy(p, u->en ? "enabled  " : "disabled ");
        if (u->state == SU_RUNNING || u->state == SU_STOPPING) { p = scopy(p, "pid "); p = udec(p, (u32)u->pid); p = scopy(p, " "); }
        int dl = 0; for (const char* d = u->desc[0] ? u->desc : u->exec; *d && dl < 60; d++, dl++) *p++ = *d;
        p = scopy(p, "\n");
    }
    *p = 0;
}
void svcu_status(const char* name, char* out, int cap) {
    char* p = out; *p = 0;
    if (!su_get(name)) svcu_reload();
    su_t* u = su_get(name); if (!u) { scopy(out, "service: no such service\n"); return; }
    if (cap < 600) return;
    p = scopy(p, u->name); p = scopy(p, " - "); p = scopy(p, u->desc[0] ? u->desc : "(no description)");
    p = scopy(p, "\n  Unit:     " SU_DIR "/"); p = scopy(p, u->name); p = scopy(p, ".service ("); p = scopy(p, u->en ? "enabled" : "disabled"); p = scopy(p, ")");
    p = scopy(p, "\n  State:    "); p = scopy(p, SU_STN[u->state]); if (u->since) { p = scopy(p, " since "); p = su_since(p, u->since); }
    if (u->state == SU_RUNNING || u->state == SU_STOPPING || u->state == SU_STARTING) { p = scopy(p, "\n  PID:      "); p = udec(p, (u32)u->pid); }
    p = scopy(p, "\n  Command:  "); { int l = 0; for (const char* s = u->exec; *s && l < 200; s++, l++) *p++ = *s; }
    p = scopy(p, "\n  User:     "); p = scopy(p, u->user);
    p = scopy(p, "\n  Restart:  "); p = scopy(p, u->restart == 2 ? "always" : u->restart == 1 ? "on-failure" : "no"); p = scopy(p, ", restarted "); p = udec(p, (u32)u->restarts); p = scopy(p, " times");
    if (u->lastst) { char b[64]; su_desc_status(b, sizeof b, u->lastst); p = scopy(p, "\n  Last run: "); p = scopy(p, b); }
    if (u->why[0]) { p = scopy(p, "\n  Problem:  "); p = scopy(p, u->why); }
    p = scopy(p, "\n  Log:      " SU_LOGDIR "/"); p = scopy(p, u->name); p = scopy(p, ".log\n");
    int used = (int)(p - out); svcu_logs(name, p, cap - used);
}
void svcu_logs(const char* name, char* out, int cap) {
    out[0] = 0; if (cap < 64) return;
    if (!su_okname(name)) { scopy(out, "  (no such service)\n"); return; }
    if (g_uid != 0 && !ux_is_admin(g_uid)) { scopy(out, "  (log is readable by administrators only)\n"); return; }
    char lp[96]; su_path(lp, sizeof lp, SU_LOGDIR, name, ".log");
    int k = k_find_file(lp); if (k < 0) { scopy(out, "  (no log yet)\n"); return; }
    const char* s = fsd(k); int n = FS[k].len; int st = n, lines = 0;
    while (st > 0 && lines <= 10) { st--; if (s[st] == '\n' && st != n - 1) lines++; }
    if (lines > 10) st++;
    int max = cap - 2; if (n - st > max) st = n - max;
    int o = 0; for (int z = st; z < n && o < cap - 2; z++) out[o++] = s[z]; if (o && out[o - 1] != '\n') out[o++] = '\n'; out[o] = 0;
}
