#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "include/nvx/apps/nwe_api.h"

void nwe_arena_init(void) {}
void nwe_arena_reset(void) {}
size_t nwe_arena_used(void) { return 0; }
size_t nwe_arena_size(void) { return 512u << 20; }
void* nwk_alloc_arena(unsigned long n) { return malloc(n); }

static char* readfile(const char* p, int* len) {
    FILE* f = fopen(p, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 0; }
    b[n] = 0;
    fclose(f);
    *len = (int)n;
    return b;
}

int nwk_fetch(const char* url, const char* method, const char* ctype, const char* body, int blen, const char* referer, nwk_resp* r) {
    (void)ctype; (void)body; (void)blen; (void)referer;
    memset(r, 0, sizeof(*r));
    snprintf(r->final_url, sizeof(r->final_url), "%s", url);
    fprintf(stderr, "[fetch] %s %s\n", method, url);
    if (strncmp(url, "file://", 7)) { r->status = 0; return -1; }
    int len;
    char* d = readfile(url + 7, &len);
    if (!d) { r->status = 404; return 0; }
    r->body = d;
    r->len = len;
    r->status = 200;
    snprintf(r->ctype, sizeof(r->ctype), "%s", strstr(url, ".css") ? "text/css" : strstr(url, ".js") ? "application/javascript" : "text/html");
    return 0;
}
void nwk_free(void* p) { free(p); }
int nwk_image(const unsigned char* data, int len, unsigned int** argb, int* w, int* h) {
    (void)data; (void)len;
    *w = 64; *h = 48;
    unsigned* p = malloc(64 * 48 * 4);
    for (int i = 0; i < 64 * 48; i++) p[i] = 0xFF000000u | (unsigned)((i % 64) * 4 << 16) | (unsigned)((i / 64) * 5 << 8) | 0x80;
    *argb = p;
    return 0;
}
static char cookies[4096];
int nwk_cookie_get(const char* url, int forjs, char* out, int cap) { (void)url; (void)forjs; snprintf(out, (size_t)cap, "%s", cookies); return 0; }
void nwk_cookie_set(const char* url, const char* line, int fromjs) { (void)url; (void)fromjs; const char* semi = strchr(line, ';'); int l = semi ? (int)(semi - line) : (int)strlen(line); if (cookies[0]) strcat(cookies, "; "); strncat(cookies, line, (size_t)l); }
static char skeys[64][128], svals[64][1024];
static int ns;
int nwk_store_get(const char* origin, int session, const char* key, char* out, int cap) { (void)origin; (void)session; for (int i = 0; i < ns; i++) if (!strcmp(skeys[i], key)) { snprintf(out, (size_t)cap, "%s", svals[i]); return 0; } return -1; }
void nwk_store_set(const char* origin, int session, const char* key, const char* val) { (void)origin; (void)session; for (int i = 0; i < ns; i++) if (!strcmp(skeys[i], key)) { if (!val) { skeys[i][0] = 0; } else snprintf(svals[i], 1024, "%s", val); return; } if (val && ns < 64) { snprintf(skeys[ns], 128, "%s", key); snprintf(svals[ns], 1024, "%s", val); ns++; } }
void nwk_store_clear(const char* origin, int session) { (void)origin; (void)session; ns = 0; }
int nwk_store_key(const char* origin, int session, int idx, char* out, int cap) { (void)origin; (void)session; if (idx < 0 || idx >= ns) return -1; snprintf(out, (size_t)cap, "%s", skeys[idx]); return 0; }
int nwk_store_len(const char* origin, int session) { (void)origin; (void)session; return ns; }
void nwk_log(const char* s) { fprintf(stderr, "[log] %s\n", s); }
void nwk_status(const char* s) { (void)s; }
void nwk_alert(const char* s) { fprintf(stderr, "[alert] %s\n", s); }
int nwk_busy(void) { return 0; }
unsigned long nwk_ms(void) { struct timeval tv; gettimeofday(&tv, 0); return (unsigned long)(tv.tv_sec * 1000 + tv.tv_usec / 1000); }
long long nwk_epoch_ms(void) { struct timeval tv; gettimeofday(&tv, 0); return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
int nwk_tz_min(void) { return 0; }
void nwk_pump(void) {}
void* nwe_stack_top(void) { return 0; }
#include <zlib.h>
int nwk_inflate(const unsigned char* src, int slen, unsigned char* dst, int dcap) { uLongf dl = (uLongf)dcap; if (uncompress(dst, &dl, src, (uLong)slen) != Z_OK) return -1; return (int)dl; }
static struct { int used, playing; long long cur, base; unsigned long t0; long long app[2]; int ended[2]; unsigned fr[160 * 90]; unsigned long long seq; } HM[8];
int nwk_media_new(const char* url, int split, int w, int h, long long start_ms, int paused) {
    for (int i = 0; i < 8; i++) if (!HM[i].used) { memset(&HM[i], 0, sizeof HM[i]); HM[i].used = 1; HM[i].playing = !paused; HM[i].base = start_ms; HM[i].t0 = nwk_ms(); for (int k = 0; k < 160 * 90; k++) HM[i].fr[k] = 0xFF2060C0u + (unsigned)(k % 160); HM[i].seq = 1; fprintf(stderr, "[media] new %d url='%s' split=%d %dx%d start=%lld paused=%d\n", i, url, split, w, h, start_ms, paused); return i; }
    return -1;
}
static long long hm_cur(int id) { return HM[id].playing ? HM[id].base + (long long)(nwk_ms() - HM[id].t0) : HM[id].base; }
void nwk_media_cmd(int id, int cmd, long long a, long long b) { fprintf(stderr, "[media] cmd %d %d %lld %lld\n", id, cmd, a, b); if (cmd == 1 && !HM[id].playing) { HM[id].t0 = nwk_ms(); HM[id].playing = 1; } if (cmd == 2 && HM[id].playing) { HM[id].base = hm_cur(id); HM[id].playing = 0; } if (cmd == 3) { HM[id].base = a; HM[id].t0 = nwk_ms(); } }
int nwk_media_status(int id, long long* v) { if (id < 0 || id >= 8 || !HM[id].used) return -1; long long c = hm_cur(id); unsigned fl = 128 | 16 | 32; if (c >= 10000) { c = 10000; fl |= 4; } else fl |= HM[id].playing ? 1 : 2; v[0] = c; v[1] = 10000; v[2] = fl; v[3] = 160; v[4] = 90; v[5] = 50; HM[id].seq++; return 0; }
int nwk_media_append(int id, int stream, const unsigned char* d, int len) { HM[id].app[stream] += len; fprintf(stderr, "[media] append %d s%d %d bytes (total %lld) first=%02x%02x%02x%02x%02x%02x%02x%02x\n", id, stream, len, HM[id].app[stream], d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]); return 0; }
void nwk_media_end(int id, int stream) { fprintf(stderr, "[media] end %d s%d\n", id, stream); }
int nwk_media_restart(int id, long long start_ms, int keep) { fprintf(stderr, "[media] restart %d at %lld keep=%d\n", id, start_ms, keep); HM[id].base = start_ms; HM[id].t0 = nwk_ms(); return 0; }
void nwk_media_free(int id) { if (id >= 0 && id < 8) { HM[id].used = 0; fprintf(stderr, "[media] free %d\n", id); } }
long long nwk_media_len(int id, int stream) { return HM[id].app[stream]; }
const unsigned* nwk_media_frame(int id, int* w, int* h, unsigned long long* seq) { if (id < 0 || id >= 8 || !HM[id].used) return 0; *w = 160; *h = 90; if (seq) *seq = HM[id].seq; return HM[id].fr; }

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: webhost in.html out.ppm [w] [h] [maxh] [script]\n"); return 1; }
    int w = argc > 3 ? atoi(argv[3]) : 1000, h = argc > 4 ? atoi(argv[4]) : 700, maxh = argc > 5 ? atoi(argv[5]) : 2400;
    int len;
    char* html = readfile(argv[1], &len);
    if (!html) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    char url[2048];
    char abs[4096];
    if (!realpath(argv[1], abs)) snprintf(abs, sizeof(abs), "%s", argv[1]);
    snprintf(url, sizeof(url), "file://%s", abs);
    static nwe_req r;
    memset(&r, 0, sizeof(r));
    nwe_entry(NWE_OP_INIT, &r);
    unsigned long t0 = nwk_ms();
    memset(&r, 0, sizeof(r));
    r.url = url; r.data = html; r.len = len; r.w = w; r.h = h;
    nwe_entry(NWE_OP_LOAD, &r);
    fprintf(stderr, "load %lums title='%s' doch=%d nav='%s'\n", nwk_ms() - t0, r.title, r.doch, r.nav);
    for (int i = 0; i < 20; i++) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); }
    if (getenv("WH_MS")) { unsigned long t0 = nwk_ms(), lim = (unsigned long)atol(getenv("WH_MS")); while (nwk_ms() - t0 < lim) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); struct timespec ts = { 0, 10000000 }; nanosleep(&ts, 0); } }
    if (getenv("WH_ACT")) { char* a = strdup(getenv("WH_ACT")); for (char* t = strtok(a, ";"); t; t = strtok(0, ";")) {
            if (t[0] == 'c') { int x = 0, y = 0; sscanf(t + 1, "%d %d", &x, &y); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; nwe_entry(NWE_OP_MOVE, &r); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; nwe_entry(NWE_OP_DOWN, &r); memset(&r, 0, sizeof(r)); r.x = x; r.y = y; r.btn = 1; nwe_entry(NWE_OP_UP, &r); }
            else if (t[0] == 't') { for (char* q = t + 2; *q; q++) { memset(&r, 0, sizeof(r)); r.key = (unsigned char)*q; nwe_entry(NWE_OP_KEY, &r); } }
            else if (t[0] == 'k') { memset(&r, 0, sizeof(r)); r.key = atoi(t + 1); nwe_entry(NWE_OP_KEY, &r); }
            for (int i = 0; i < 3; i++) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); } } }
    if (argc > 6) {
        memset(&r, 0, sizeof(r));
        r.data = argv[6];
        nwe_entry(NWE_OP_EVAL, &r);
        fprintf(stderr, "eval: %s\n", r.out);
        for (int i = 0; i < 5; i++) { memset(&r, 0, sizeof(r)); nwe_entry(NWE_OP_TICK, &r); }
    }
    memset(&r, 0, sizeof(r));
    r.w = w; r.h = h;
    nwe_entry(NWE_OP_LAYOUT, &r);
    int doch = r.doch;
    int H = doch < maxh ? doch : maxh;
    if (H < h) H = h;
    unsigned* fb = calloc((size_t)w * (size_t)H, 4);
    t0 = nwk_ms();
    for (int y = 0; y < H; y += h) {
        memset(&r, 0, sizeof(r));
        r.y = y;
        nwe_entry(NWE_OP_SCROLL, &r);
        int sc = r.scroll;
        unsigned* tmp = calloc((size_t)w * (size_t)h, 4);
        memset(&r, 0, sizeof(r));
        r.fb = tmp; r.stride = w; r.fbw = w; r.fbh = h; r.x = 0; r.y = 0; r.w = w; r.h = h;
        nwe_entry(NWE_OP_PAINT, &r);
        for (int yy = 0; yy < h; yy++) { int dy = sc + yy; if (dy >= y && dy < H) memcpy(fb + (size_t)dy * w, tmp + (size_t)yy * w, (size_t)w * 4); }
        free(tmp);
        memset(&r, 0, sizeof(r));
        r.y = -100000;
        nwe_entry(NWE_OP_SCROLL, &r);
    }
    fprintf(stderr, "paint %lums size %dx%d\n", nwk_ms() - t0, w, H);
    FILE* f = fopen(argv[2], "wb");
    fprintf(f, "P6 %d %d 255\n", w, H);
    for (size_t i = 0; i < (size_t)w * H; i++) { unsigned c = fb[i]; fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f); }
    fclose(f);
    return 0;
}
