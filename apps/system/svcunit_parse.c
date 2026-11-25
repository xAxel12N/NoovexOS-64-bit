#include "include/nvx/apps/svcunits.h"

static int sp_eq(const char* a, int n, const char* b) { int i = 0; for (; i < n && b[i]; i++) if (a[i] != b[i]) return 0; return i == n && !b[i]; }
static int sp_num(const char* v, int n, int dflt, int lo, int hi) { int r = 0, k = 0; for (int i = 0; i < n; i++) { if (v[i] < '0' || v[i] > '9') return dflt; r = r * 10 + (v[i] - '0'); if (r > 100000) r = 100000; k++; } if (!k) return dflt; return r < lo ? lo : r > hi ? hi : r; }
static void sp_put(char* o, int cap, const char* v, int n) { int k = 0; for (; k < n && k < cap - 1; k++) o[k] = v[k]; o[k] = 0; }
static void sp_env(char* env, int cap, const char* v, int n) {
    int o = 0; while (env[o]) o++;
    int i = 0;
    while (i < n) {
        while (i < n && (v[i] == ' ' || v[i] == '\t')) i++;
        if (i >= n) break;
        int st = o, q = 0;
        while (i < n && (q || (v[i] != ' ' && v[i] != '\t'))) { if (v[i] == '"') { q = !q; i++; continue; } if (o < cap - 2) env[o++] = v[i]; i++; }
        int eq = 0; for (int k = st; k < o; k++) if (env[k] == '=') { eq = k > st; break; }
        if (!eq || o >= cap - 2) { o = st; continue; }
        env[o++] = '\n';
    }
    env[o] = 0;
}
int svcu_parse(const char* txt, int n, char* desc, int dcap, char* exec, int ecap, char* user, int ucap, char* wd, int wcap, char* env, int envcap, char* after, int acap, int* type, int* restart, int* rsec, int* tstop, int* defen) {
    desc[0] = exec[0] = after[0] = env[0] = 0; sp_put(user, ucap, "root", 4); sp_put(wd, wcap, "/", 1);
    *type = 0; *restart = 0; *rsec = 1; *tstop = 5; *defen = 0;
    int i = 0;
    while (i < n && txt[i]) {
        int ls = i; while (i < n && txt[i] && txt[i] != '\n') i++;
        int le = i; if (i < n && txt[i]) i++;
        while (ls < le && (txt[ls] == ' ' || txt[ls] == '\t' || txt[ls] == '\r')) ls++;
        while (le > ls && (txt[le - 1] == ' ' || txt[le - 1] == '\t' || txt[le - 1] == '\r')) le--;
        if (ls >= le || txt[ls] == '#' || txt[ls] == ';' || txt[ls] == '[') continue;
        int eq = ls; while (eq < le && txt[eq] != '=') eq++;
        if (eq >= le) continue;
        int ke = eq; while (ke > ls && (txt[ke - 1] == ' ' || txt[ke - 1] == '\t')) ke--;
        int vs = eq + 1; while (vs < le && (txt[vs] == ' ' || txt[vs] == '\t')) vs++;
        int ve = le, q = 0;
        for (int k = vs; k < le; k++) { if (txt[k] == '"') q = !q; else if (!q && txt[k] == '#' && k > vs && (txt[k - 1] == ' ' || txt[k - 1] == '\t')) { ve = k; break; } }
        while (ve > vs && (txt[ve - 1] == ' ' || txt[ve - 1] == '\t')) ve--;
        const char* k = txt + ls; int kn = ke - ls; const char* v = txt + vs; int vn = ve - vs;
        if (sp_eq(k, kn, "Description")) sp_put(desc, dcap, v, vn);
        else if (sp_eq(k, kn, "ExecStart")) sp_put(exec, ecap, v, vn);
        else if (sp_eq(k, kn, "User")) { if (vn) sp_put(user, ucap, v, vn); }
        else if (sp_eq(k, kn, "WorkingDirectory")) { if (vn) sp_put(wd, wcap, v, vn); }
        else if (sp_eq(k, kn, "Environment")) sp_env(env, envcap, v, vn);
        else if (sp_eq(k, kn, "After")) { int o = 0; while (after[o]) o++; if (o && o < acap - 1) after[o++] = ' '; for (int z = 0; z < vn && o < acap - 1; z++) after[o++] = v[z]; after[o] = 0; }
        else if (sp_eq(k, kn, "Type")) *type = sp_eq(v, vn, "oneshot") ? 1 : 0;
        else if (sp_eq(k, kn, "Restart")) *restart = sp_eq(v, vn, "always") ? 2 : (sp_eq(v, vn, "on-failure") || sp_eq(v, vn, "on-abnormal")) ? 1 : 0;
        else if (sp_eq(k, kn, "RestartSec")) *rsec = sp_num(v, vn, 1, 1, 60);
        else if (sp_eq(k, kn, "TimeoutStopSec")) *tstop = sp_num(v, vn, 5, 1, 60);
        else if (sp_eq(k, kn, "Enabled") || sp_eq(k, kn, "WantedBy")) *defen = (sp_eq(v, vn, "no") || sp_eq(v, vn, "false") || sp_eq(v, vn, "0")) ? 0 : 1;
    }
    if (exec[0] != '/') return -1;
    return 0;
}
int svcu_split_args(const char* exec, char* out, int cap) {
    int o = 0, argc = 0, i = 0;
    while (exec[i]) {
        while (exec[i] == ' ' || exec[i] == '\t') i++;
        if (!exec[i]) break;
        if (argc && o < cap - 1) out[o++] = ' ';
        int q = 0, st = o;
        while (exec[i] && (q || (exec[i] != ' ' && exec[i] != '\t'))) { char c = exec[i++]; if (c == '"') { q = !q; continue; } if (o >= cap - 1) return -1; out[o++] = c == ' ' ? (char)0x1F : c; }
        if (q) return -1;
        if (o == st) { if (o >= cap - 1) return -1; out[o++] = (char)0x1F; }
        argc++;
    }
    out[o] = 0;
    return argc ? argc : -1;
}
u32 svcu_backoff(int rsec, int fails) { if (rsec < 1) rsec = 1; if (fails < 0) fails = 0; if (fails > 6) fails = 6; u32 s = (u32)rsec << fails; if (s > 60) s = 60; return s * 100u; }
