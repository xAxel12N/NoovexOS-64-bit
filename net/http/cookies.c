#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/sys/native/syscall.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/net/http/cookies.h"

typedef struct {
    char name[256];
    char* value;
    int vcap;
    char domain[128];
    char path[128];
    u64 expires;
    u64 created;
    u8 secure, httponly, hostonly, samesite, used;
} cookie;

#define CK_MAX 3000
#define CK_PER_SITE 180
#define CK_VMAX 4096
static cookie* g_ck;
static int g_ck_loaded, g_ck_dirty;
static u64 g_ck_lastsave;

typedef struct { char origin[128]; char key[160]; char* val; int vlen; u8 session, used; } wsent;
#define WS_MAX 600
static wsent* g_ws;
static int g_ws_loaded, g_ws_dirty;

static int lcc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int ieqn_(const char* a, const char* b, int n) { for (int i = 0; i < n; i++) { if (lcc((u8)a[i]) != lcc((u8)b[i])) return 0; if (!a[i]) return 1; } return 1; }
static int slen_(const char* s) { int n = 0; while (s[n]) n++; return n; }
static void cpyn(char* d, const char* s, int n, int cap) { if (n > cap - 1) n = cap - 1; for (int i = 0; i < n; i++) d[i] = s[i]; d[n] = 0; }
static int streq(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

static void url_parts(const char* url, int* secure, char* host, int hcap, char* path, int pcap) {
    *secure = ieqn_(url, "https:", 6);
    const char* p = url;
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == '/') p++;
    int h = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#' && *p != ':' && h < hcap - 1) host[h++] = (char)lcc((u8)*p++);
    host[h] = 0;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    int k = 0;
    if (*p != '/') path[k++] = '/';
    while (*p && *p != '?' && *p != '#' && k < pcap - 1) path[k++] = *p++;
    path[k] = 0;
}

static u64 now_ms(void) { return k_realtime_ms(); }

static int ck_val(cookie* c, const char* s, int n) {
    if (n < 0) n = 0;
    if (n > CK_VMAX) n = CK_VMAX;
    if (!c->value || c->vcap < n + 1) {
        int cap = n + 1 < 64 ? 64 : n + 1;
        char* v = kmalloc((unsigned long)cap);
        if (!v) return 0;
        if (c->value) kfree(c->value);
        c->value = v; c->vcap = cap;
    }
    for (int i = 0; i < n; i++) c->value[i] = s[i];
    c->value[n] = 0;
    return 1;
}
static const char* ck_site(const char* d) { int n = slen_(d), dots = 0; for (int i = n - 1; i >= 0; i--) if (d[i] == '.') { dots++; if (dots == 2) return d + i + 1; } return d; }

static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}
static u64 parse_date(const char* s, int n) {
    static const char* mons = "janfebmaraprmayjunjulaugsepoctnovdec";
    int day = -1, mon = -1, year = -1, hh = 0, mm = 0, ss = 0, havetime = 0;
    int i = 0;
    while (i < n) {
        while (i < n && !((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) i++;
        int st = i;
        while (i < n && ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || s[i] == ':')) i++;
        int l = i - st;
        if (l <= 0) break;
        const char* t = s + st;
        if (!havetime && l >= 5 && t[2] == ':') { hh = (t[0] - '0') * 10 + t[1] - '0'; mm = (t[3] - '0') * 10 + t[4] - '0'; if (l >= 8) ss = (t[6] - '0') * 10 + t[7] - '0'; havetime = 1; continue; }
        if (t[0] >= '0' && t[0] <= '9') {
            int v = 0;
            for (int k = 0; k < l && t[k] >= '0' && t[k] <= '9'; k++) v = v * 10 + t[k] - '0';
            if (day < 0 && l <= 2) day = v;
            else if (year < 0) year = v < 70 ? v + 2000 : v < 100 ? v + 1900 : v;
            continue;
        }
        if (mon < 0 && l >= 3) for (int k = 0; k < 12; k++) if (lcc((u8)t[0]) == mons[k * 3] && lcc((u8)t[1]) == mons[k * 3 + 1] && lcc((u8)t[2]) == mons[k * 3 + 2]) { mon = k + 1; break; }
    }
    if (day < 1 || mon < 1 || year < 1601) return 0;
    long long d = days_from_civil(year, mon, day);
    long long secs = d * 86400 + hh * 3600 + mm * 60 + ss;
    if (secs <= 0) return 1;
    return (u64)secs * 1000ULL;
}

void acct_profile_name(const char* base,char* out,int cap);
static const char* ck_fname(void){ static char n[64]; acct_profile_name("cookies.txt",n,64); return n; }
static const char* ws_fname(void){ static char n[64]; acct_profile_name("webstorage.txt",n,64); return n; }
static void ck_save(void);
static void ck_load(void) {
    if (g_ck_loaded) return;
    g_ck_loaded = 1;
    if (!g_ck) { g_ck = kmalloc(sizeof(cookie) * CK_MAX); if (!g_ck) return; for (unsigned long i = 0; i < sizeof(cookie) * CK_MAX; i++) ((u8*)g_ck)[i] = 0; }
    int slot = -1;
    for (int i = 0; i < FS_N; i++) if (FS[i].used && streq(FS[i].name, ck_fname())) { slot = i; break; }
    if (slot < 0) return;
    const char* d = fsd(slot);
    int n = (int)FS[slot].len;
    int i = 0;
    u64 now = now_ms();
    while (i < n) {
        int ls = i;
        while (i < n && d[i] != '\n') i++;
        int le = i;
        if (i < n) i++;
        const char* f[9];
        int fl[9];
        int nf = 0;
        int p = ls;
        while (p <= le && nf < 9) { int s = p; while (p < le && d[p] != '\t') p++; f[nf] = d + s; fl[nf] = p - s; nf++; p++; }
        if (nf < 8) continue;
        int k = -1;
        for (int q = 0; q < CK_MAX; q++) if (!g_ck[q].used) { k = q; break; }
        if (k < 0) break;
        cookie* c = &g_ck[k];
        cpyn(c->domain, f[0], fl[0], sizeof(c->domain));
        c->hostonly = f[1][0] == '1';
        cpyn(c->path, f[2], fl[2], sizeof(c->path));
        c->secure = f[3][0] == '1';
        c->httponly = f[4][0] == '1';
        u64 e = 0;
        for (int q = 0; q < fl[5]; q++) if (f[5][q] >= '0' && f[5][q] <= '9') e = e * 10 + (u64)(f[5][q] - '0');
        c->expires = e;
        cpyn(c->name, f[6], fl[6], sizeof(c->name));
        if (!ck_val(c, f[7], fl[7])) continue;
        c->samesite = nf >= 9 && fl[8] > 0 && f[8][0] >= '0' && f[8][0] <= '3' ? (u8)(f[8][0] - '0') : 4;
        c->created = now - (u64)(CK_MAX - k);
        c->used = (e == 0 || e > now) ? 1 : 0;
    }
}

static void ck_save(void) {
    if (!g_ck) return;
    int cap = 64;
    for (int i = 0; i < CK_MAX; i++) if (g_ck[i].used) cap += slen_(g_ck[i].domain) + slen_(g_ck[i].path) + slen_(g_ck[i].name) + (g_ck[i].value ? slen_(g_ck[i].value) : 0) + 48;
    char* b = kmalloc((unsigned long)cap);
    if (!b) return;
    int o = 0;
    u64 now = now_ms();
    for (int i = 0; i < CK_MAX; i++) {
        cookie* c = &g_ck[i];
        if (!c->used || c->expires == 0 || c->expires <= now) continue;
        if (!c->value) continue;
        for (const char* s = c->domain; *s; s++) b[o++] = *s;
        b[o++] = '\t'; b[o++] = c->hostonly ? '1' : '0'; b[o++] = '\t';
        for (const char* s = c->path; *s; s++) b[o++] = *s;
        b[o++] = '\t'; b[o++] = c->secure ? '1' : '0'; b[o++] = '\t'; b[o++] = c->httponly ? '1' : '0'; b[o++] = '\t';
        char t[24]; int tn = 0; u64 e = c->expires; if (!e) t[tn++] = '0'; while (e) { t[tn++] = (char)('0' + e % 10); e /= 10; } while (tn) b[o++] = t[--tn];
        b[o++] = '\t';
        for (const char* s = c->name; *s; s++) b[o++] = *s;
        b[o++] = '\t';
        for (const char* s = c->value; *s; s++) if (*s != '\n' && *s != '\t') b[o++] = *s;
        if (c->samesite <= 3) { b[o++] = '\t'; b[o++] = (char)('0' + c->samesite); }
        b[o++] = '\n';
    }
    fs_save_bin(ck_fname(), (const u8*)b, o, 0);
    kfree(b);
    g_ck_dirty = 0;
    g_ck_lastsave = now;
}

static int domain_match(const char* host, const char* dom) {
    int hl = slen_(host), dl = slen_(dom);
    if (hl == dl) return ieqn_(host, dom, hl);
    if (hl > dl && host[hl - dl - 1] == '.' && ieqn_(host + hl - dl, dom, dl)) return 1;
    return 0;
}
static int path_match(const char* rp, const char* cp) {
    int cl = slen_(cp);
    for (int i = 0; i < cl; i++) if (rp[i] != cp[i]) return 0;
    if (rp[cl] == 0) return 1;
    if (cl > 0 && cp[cl - 1] == '/') return 1;
    return rp[cl] == '/';
}

void cookie_set(const char* url, const char* line, int fromjs) {
    ck_load();
    if (!g_ck) return;
    int secure;
    char host[128], path[512];
    url_parts(url, &secure, host, sizeof(host), path, sizeof(path));
    if (!host[0]) return;
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    const char* ns = p;
    while (*p && *p != '=' && *p != ';') p++;
    const char* ne = p;
    const char* vs = p;
    const char* ve = p;
    if (*p == '=') { p++; vs = p; while (*p && *p != ';') p++; ve = p; }
    else { vs = ne = ns; ve = p; }
    while (ne > ns && (ne[-1] == ' ' || ne[-1] == '\t')) ne--;
    while (vs < ve && (*vs == ' ' || *vs == '\t')) vs++;
    while (ve > vs && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) ve--;
    if (ve - vs >= 2 && *vs == '"' && ve[-1] == '"') { vs++; ve--; }
    cookie c;
    for (unsigned i = 0; i < sizeof(c); i++) ((u8*)&c)[i] = 0;
    if ((ne - ns) + (ve - vs) > CK_VMAX || (ne - ns) >= (int)sizeof(c.name)) return;
    cpyn(c.name, ns, (int)(ne - ns), sizeof(c.name));
    c.hostonly = 1;
    cpyn(c.domain, host, slen_(host), sizeof(c.domain));
    int lastslash = 0;
    for (int i = 0; path[i]; i++) if (path[i] == '/') lastslash = i;
    if (lastslash == 0) { c.path[0] = '/'; c.path[1] = 0; } else cpyn(c.path, path, lastslash, sizeof(c.path));
    u64 now = now_ms();
    long long maxage = -1;
    int has_maxage = 0;
    u64 expires = 0;
    while (*p == ';') {
        p++;
        while (*p == ' ') p++;
        const char* as = p;
        while (*p && *p != '=' && *p != ';') p++;
        int al = (int)(p - as);
        while (al > 0 && as[al - 1] == ' ') al--;
        const char* avs = p;
        int avl = 0;
        if (*p == '=') { p++; avs = p; while (*p && *p != ';') p++; avl = (int)(p - avs); while (avl > 0 && (avs[avl - 1] == ' ' || avs[avl - 1] == '\r')) avl--; while (avl > 0 && *avs == ' ') { avs++; avl--; } }
        if (al == 6 && ieqn_(as, "domain", 6) && avl > 0) {
            if (*avs == '.') { avs++; avl--; }
            char d[128];
            cpyn(d, avs, avl, sizeof(d));
            for (int i = 0; d[i]; i++) d[i] = (char)lcc((u8)d[i]);
            if (!domain_match(host, d)) return;
            int dots = 0;
            for (int i = 0; d[i]; i++) if (d[i] == '.') dots++;
            if (!dots && !streq(d, "localhost")) return;
            cpyn(c.domain, d, slen_(d), sizeof(c.domain));
            c.hostonly = 0;
        } else if (al == 4 && ieqn_(as, "path", 4) && avl > 0 && *avs == '/') cpyn(c.path, avs, avl, sizeof(c.path));
        else if (al == 7 && ieqn_(as, "expires", 7)) expires = parse_date(avs, avl);
        else if (al == 7 && ieqn_(as, "max-age", 7)) { long long v = 0; int neg = 0; int i = 0; if (i < avl && avs[i] == '-') { neg = 1; i++; } for (; i < avl && avs[i] >= '0' && avs[i] <= '9'; i++) v = v * 10 + (avs[i] - '0'); maxage = neg ? -v : v; has_maxage = 1; }
        else if (al == 6 && ieqn_(as, "secure", 6)) c.secure = 1;
        else if (al == 8 && ieqn_(as, "httponly", 8)) c.httponly = 1;
        else if (al == 8 && ieqn_(as, "samesite", 8)) c.samesite = (avl >= 6 && ieqn_(avs, "strict", 6)) ? 2 : (avl >= 4 && ieqn_(avs, "none", 4)) ? 3 : 1;
    }
    if (fromjs && c.httponly) return;
    if (c.secure && !secure) return;
    if (has_maxage) c.expires = maxage <= 0 ? 1 : now + (u64)maxage * 1000ULL;
    else c.expires = expires;
    int found = -1;
    for (int i = 0; i < CK_MAX; i++) {
        cookie* e = &g_ck[i];
        if (!e->used) continue;
        if (streq(e->name, c.name) && streq(e->domain, c.domain) && streq(e->path, c.path)) { found = i; break; }
    }
    if (found >= 0 && fromjs && g_ck[found].httponly) return;
    if (c.expires && c.expires <= now) {
        if (found >= 0) { g_ck[found].used = 0; g_ck_dirty = 1; }
        return;
    }
    if (found < 0) {
        const char* site = ck_site(c.domain);
        int ns_ = 0, so = -1; u64 sold = (u64)-1;
        for (int i = 0; i < CK_MAX; i++) if (g_ck[i].used && streq(ck_site(g_ck[i].domain), site)) { ns_++; if (g_ck[i].created < sold) { sold = g_ck[i].created; so = i; } }
        if (ns_ >= CK_PER_SITE && so >= 0) { if (g_ck[so].expires) g_ck_dirty = 1; g_ck[so].used = 0; found = so; }
        if (found < 0) for (int i = 0; i < CK_MAX; i++) if (!g_ck[i].used) { found = i; break; }
        if (found < 0) {
            u64 oldest = (u64)-1;
            for (int i = 0; i < CK_MAX; i++) if (g_ck[i].created < oldest) { oldest = g_ck[i].created; found = i; }
            if (g_ck[found].expires) g_ck_dirty = 1;
        }
        c.created = now;
    } else c.created = g_ck[found].created;
    c.used = 1;
    c.value = g_ck[found].value; c.vcap = g_ck[found].vcap;
    if (!ck_val(&c, vs, (int)(ve - vs))) { g_ck[found].value = c.value; g_ck[found].vcap = c.vcap; return; }
    if (g_ck[found].used && g_ck[found].expires) g_ck_dirty = 1;
    g_ck[found] = c;
    if (c.expires) g_ck_dirty = 1;
}

int g_ck_samesite = 1;
int cookie_header2(const char* url, int forjs, char* out, int cap, int cross, int topnav, int safe);
int cookie_header(const char* url, int forjs, char* out, int cap) { return cookie_header2(url, forjs, out, cap, 0, 1, 1); }
int cookie_header2(const char* url, int forjs, char* out, int cap, int cross, int topnav, int safe) {
    ck_load();
    out[0] = 0;
    if (!g_ck) return 0;
    int secure;
    char host[128], path[512];
    url_parts(url, &secure, host, sizeof(host), path, sizeof(path));
    u64 now = now_ms();
    int* idx = kmalloc(sizeof(int) * CK_MAX);
    if (!idx) return 0;
    int n = 0;
    for (int i = 0; i < CK_MAX; i++) {
        cookie* c = &g_ck[i];
        if (!c->used) continue;
        if (c->expires && c->expires <= now) { c->used = 0; g_ck_dirty = 1; continue; }
        if (c->hostonly ? !streq(host, c->domain) : !domain_match(host, c->domain)) continue;
        if (!path_match(path, c->path)) continue;
        if (c->secure && !secure) continue;
        if (forjs && c->httponly) continue;
        if (!c->value) continue;
        if (cross && g_ck_samesite && c->samesite != 3 && c->samesite != 4) {
            if (c->samesite == 2) continue;
            int lax = topnav && safe;
            if (!lax && c->samesite == 0 && topnav && now - c->created < 120000ull) lax = 1;
            if (!lax) continue;
        }
        idx[n++] = i;
    }
    for (int a = 1; a < n; a++) { int t = idx[a]; int b = a - 1; while (b >= 0 && (slen_(g_ck[idx[b]].path) < slen_(g_ck[t].path) || (slen_(g_ck[idx[b]].path) == slen_(g_ck[t].path) && g_ck[idx[b]].created > g_ck[t].created))) { idx[b + 1] = idx[b]; b--; } idx[b + 1] = t; }
    int o = 0;
    for (int k = 0; k < n; k++) {
        cookie* c = &g_ck[idx[k]];
        int need = slen_(c->name) + slen_(c->value) + 3;
        if (o + need >= cap) continue;
        if (o) { out[o++] = ';'; out[o++] = ' '; }
        for (const char* s = c->name; *s; s++) out[o++] = *s;
        if (c->name[0]) out[o++] = '=';
        for (const char* s = c->value; *s; s++) out[o++] = *s;
    }
    out[o] = 0;
    kfree(idx);
    return o;
}

void cookie_clear(void) {
    ck_load();
    if (!g_ck) return;
    for (int i = 0; i < CK_MAX; i++) { g_ck[i].used = 0; if (g_ck[i].value) kfree(g_ck[i].value); g_ck[i].value = 0; g_ck[i].vcap = 0; }
    g_ck_dirty = 0;
    fs_save_bin(ck_fname(), (const u8*)"", 0, 0);
}
int cookie_count(void) { ck_load(); int n = 0; if (g_ck) for (int i = 0; i < CK_MAX; i++) if (g_ck[i].used) n++; return n; }
int cookie_list(int idx, char* domain, int dcap, char* name, int ncap, char* value, int vcap, u64* expires, int* flags) {
    ck_load();
    if (!g_ck) return -1;
    int k = 0;
    for (int i = 0; i < CK_MAX; i++) {
        if (!g_ck[i].used) continue;
        if (k++ != idx) continue;
        cookie* c = &g_ck[i];
        cpyn(domain, c->domain, slen_(c->domain), dcap);
        cpyn(name, c->name, slen_(c->name), ncap);
        cpyn(value, c->value ? c->value : "", c->value ? slen_(c->value) : 0, vcap);
        *expires = c->expires;
        *flags = (c->secure ? 1 : 0) | (c->httponly ? 2 : 0) | (c->hostonly ? 4 : 0);
        return 0;
    }
    return -1;
}
void cookie_delete_domain(const char* dom) {
    ck_load();
    if (!g_ck) return;
    for (int i = 0; i < CK_MAX; i++) if (g_ck[i].used && streq(g_ck[i].domain, dom)) { g_ck[i].used = 0; g_ck_dirty = 1; }
}

static void ws_save(void);
static void ws_load(void) {
    if (g_ws_loaded) return;
    g_ws_loaded = 1;
    if (!g_ws) { g_ws = kmalloc(sizeof(wsent) * WS_MAX); if (!g_ws) return; for (int i = 0; i < WS_MAX; i++) g_ws[i].used = 0; }
    int slot = -1;
    for (int i = 0; i < FS_N; i++) if (FS[i].used && streq(FS[i].name, ws_fname())) { slot = i; break; }
    if (slot < 0) return;
    const char* d = fsd(slot);
    int n = (int)FS[slot].len;
    int i = 0;
    while (i < n) {
        int ls = i;
        while (i < n && d[i] != '\n') i++;
        int le = i;
        if (i < n) i++;
        int t1 = ls;
        while (t1 < le && d[t1] != '\t') t1++;
        int t2 = t1 + 1;
        while (t2 < le && d[t2] != '\t') t2++;
        if (t2 >= le) continue;
        int k = -1;
        for (int q = 0; q < WS_MAX; q++) if (!g_ws[q].used) { k = q; break; }
        if (k < 0) break;
        wsent* e = &g_ws[k];
        cpyn(e->origin, d + ls, t1 - ls, sizeof(e->origin));
        cpyn(e->key, d + t1 + 1, t2 - t1 - 1, sizeof(e->key));
        int vl = le - t2 - 1;
        e->val = kmalloc((unsigned long)vl + 1);
        if (!e->val) continue;
        int o = 0;
        for (int q = t2 + 1; q < le; q++) {
            if (d[q] == '\\' && q + 1 < le) { q++; e->val[o++] = d[q] == 'n' ? '\n' : d[q] == 't' ? '\t' : d[q]; }
            else e->val[o++] = d[q];
        }
        e->val[o] = 0;
        e->vlen = o;
        e->session = 0;
        e->used = 1;
    }
}
static void ws_save(void) {
    if (!g_ws) return;
    int total = 64;
    for (int i = 0; i < WS_MAX; i++) if (g_ws[i].used && !g_ws[i].session) total += slen_(g_ws[i].origin) + slen_(g_ws[i].key) + g_ws[i].vlen * 2 + 4;
    char* b = kmalloc((unsigned long)total);
    if (!b) return;
    int o = 0;
    for (int i = 0; i < WS_MAX; i++) {
        wsent* e = &g_ws[i];
        if (!e->used || e->session) continue;
        for (const char* s = e->origin; *s; s++) b[o++] = *s;
        b[o++] = '\t';
        for (const char* s = e->key; *s; s++) b[o++] = *s == '\t' || *s == '\n' ? ' ' : *s;
        b[o++] = '\t';
        for (int k = 0; k < e->vlen; k++) { char c = e->val[k]; if (c == '\n') { b[o++] = '\\'; b[o++] = 'n'; } else if (c == '\t') { b[o++] = '\\'; b[o++] = 't'; } else if (c == '\\') { b[o++] = '\\'; b[o++] = '\\'; } else b[o++] = c; }
        b[o++] = '\n';
    }
    fs_save_bin(ws_fname(), (const u8*)b, o, 0);
    kfree(b);
    g_ws_dirty = 0;
}
static wsent* ws_find(const char* origin, int session, const char* key) {
    ws_load();
    if (!g_ws) return 0;
    for (int i = 0; i < WS_MAX; i++) if (g_ws[i].used && g_ws[i].session == session && streq(g_ws[i].origin, origin) && streq(g_ws[i].key, key)) return &g_ws[i];
    return 0;
}
int webstore_get(const char* origin, int session, const char* key, char* out, int cap) {
    wsent* e = ws_find(origin, session, key);
    if (!e) return -1;
    cpyn(out, e->val, e->vlen, cap);
    return e->vlen;
}
void webstore_set(const char* origin, int session, const char* key, const char* val) {
    ws_load();
    if (!g_ws) return;
    wsent* e = ws_find(origin, session, key);
    if (!val) { if (e) { if (e->val) kfree(e->val); e->used = 0; if (!session) g_ws_dirty = 1; } return; }
    int vl = slen_(val);
    if (vl > 262144) vl = 262144;
    if (!e) {
        for (int i = 0; i < WS_MAX; i++) if (!g_ws[i].used) { e = &g_ws[i]; break; }
        if (!e) return;
        cpyn(e->origin, origin, slen_(origin), sizeof(e->origin));
        cpyn(e->key, key, slen_(key), sizeof(e->key));
        e->session = (u8)session;
        e->val = 0;
        e->used = 1;
    }
    if (e->val) kfree(e->val);
    e->val = kmalloc((unsigned long)vl + 1);
    if (!e->val) { e->used = 0; return; }
    for (int i = 0; i < vl; i++) e->val[i] = val[i];
    e->val[vl] = 0;
    e->vlen = vl;
    if (!session) g_ws_dirty = 1;
}
void webstore_clear(const char* origin, int session) {
    ws_load();
    if (!g_ws) return;
    for (int i = 0; i < WS_MAX; i++) if (g_ws[i].used && g_ws[i].session == session && (!origin || streq(g_ws[i].origin, origin))) { if (g_ws[i].val) kfree(g_ws[i].val); g_ws[i].used = 0; if (!session) g_ws_dirty = 1; }
}
int webstore_key(const char* origin, int session, int idx, char* out, int cap) {
    ws_load();
    if (!g_ws) return -1;
    int k = 0;
    for (int i = 0; i < WS_MAX; i++) if (g_ws[i].used && g_ws[i].session == session && streq(g_ws[i].origin, origin)) { if (k++ == idx) { cpyn(out, g_ws[i].key, slen_(g_ws[i].key), cap); return 0; } }
    return -1;
}
int webstore_len(const char* origin, int session) {
    ws_load();
    if (!g_ws) return 0;
    int k = 0;
    for (int i = 0; i < WS_MAX; i++) if (g_ws[i].used && g_ws[i].session == session && streq(g_ws[i].origin, origin)) k++;
    return k;
}
void webstore_session_reset(void) { if (g_ws) for (int i = 0; i < WS_MAX; i++) if (g_ws[i].used && g_ws[i].session) { if (g_ws[i].val) kfree(g_ws[i].val); g_ws[i].used = 0; } }

void cookies_flush(int force) {
    u64 now = now_ms();
    if (g_ck_dirty && (force || now - g_ck_lastsave > 3000)) ck_save();
    if (g_ws_dirty && force) ws_save();
    else if (g_ws_dirty) { static u64 last; if (now - last > 3000) { last = now; ws_save(); } }
}
void ck_profile_switch(void) {
    if (g_ck_loaded && g_ck_dirty) ck_save();
    if (g_ws_loaded && g_ws_dirty) ws_save();
    if (g_ck) for (int i = 0; i < CK_MAX; i++) { if (g_ck[i].value) kfree(g_ck[i].value); g_ck[i].value = 0; g_ck[i].vcap = 0; g_ck[i].used = 0; }
    if (g_ws) for (int i = 0; i < WS_MAX; i++) { if (g_ws[i].used && g_ws[i].val) kfree(g_ws[i].val); g_ws[i].val = 0; g_ws[i].used = 0; }
    g_ck_loaded = 0; g_ck_dirty = 0; g_ws_loaded = 0; g_ws_dirty = 0;
}
