#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "bearssl.h"
#include "nvxtls.h"

#define ROOTS "/etc/ssl/nvx-anchors.pem"
#define LOCAL_CA "/etc/ssl/local-ca.pem"

struct url { int tls; char host[256]; int port; char path[2048]; };
static int verbose, insecure, quiet, want_sha;
static nvx_anchors g_ta; static int g_ta_loaded;

static void msg(const char* f, const char* a, const char* b){ if(!quiet){ fprintf(stderr, f, a, b); } }

static int parse_url(const char* s, struct url* u, const struct url* base){
    memset(u, 0, sizeof *u);
    if(base && s[0] == '/'){ *u = *base; snprintf(u->path, sizeof u->path, "%s", s); return 0; }
    const char* p = s;
    if(!strncasecmp(p, "https://", 8)){ u->tls = 1; p += 8; }
    else if(!strncasecmp(p, "http://", 7)){ p += 7; }
    else if(strstr(p, "://"))return -1;
    else u->tls = 1;
    u->port = u->tls ? 443 : 80;
    size_t hl = strcspn(p, ":/?#");
    if(!hl || hl >= sizeof u->host)return -1;
    memcpy(u->host, p, hl); u->host[hl] = 0; p += hl;
    if(*p == ':'){ char* e; long v = strtol(p + 1, &e, 10); if(e == p + 1 || v <= 0 || v > 65535)return -1; u->port = (int)v; p = e; }
    if(*p == '#' || !*p)snprintf(u->path, sizeof u->path, "/");
    else if(*p == '?')snprintf(u->path, sizeof u->path, "/%s", p);
    else snprintf(u->path, sizeof u->path, "%s", p);
    char* h = strchr(u->path, '#'); if(h)*h = 0;
    return 0;
}

static int dial(const struct url* u){
    char port[8]; snprintf(port, sizeof port, "%d", u->port);
    struct addrinfo hints, *ai = NULL, *a;
    memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    int g = getaddrinfo(u->host, port, &hints, &ai);
    if(g){ msg("fetch: cannot resolve %s: %s\n", u->host, gai_strerror(g)); return -1; }
    int fd = -1;
    for(a = ai; a; a = a->ai_next){
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol); if(fd < 0)continue;
        if(connect(fd, a->ai_addr, a->ai_addrlen) == 0)break;
        close(fd); fd = -1;
    }
    if(fd < 0)msg("fetch: cannot connect to %s: %s\n", u->host, strerror(errno));
    freeaddrinfo(ai);
    return fd;
}

static int sock_read(void* ctx, unsigned char* buf, size_t len){
    int fd = *(int*)ctx;
    for(;;){ ssize_t r = read(fd, buf, len); if(r < 0 && errno == EINTR)continue; return r <= 0 ? -1 : (int)r; }
}
static int sock_write(void* ctx, const unsigned char* buf, size_t len){
    int fd = *(int*)ctx;
    for(;;){ ssize_t r = write(fd, buf, len); if(r < 0 && errno == EINTR)continue; return r <= 0 ? -1 : (int)r; }
}

struct dbuf2 { unsigned char* p; size_t n, cap; };
static void d2_add(struct dbuf2* b, const void* d, size_t n){
    if(b->n + n > b->cap){ size_t c = b->cap ? b->cap * 2 : 4096; while(c < b->n + n)c *= 2; unsigned char* q = realloc(b->p, c); if(!q)return; b->p = q; b->cap = c; }
    memcpy(b->p + b->n, d, n); b->n += n;
}

static int der_next(const unsigned char* p, size_t len, size_t* off, int* tag, const unsigned char** v, size_t* vl){
    if(*off + 2 > len)return -1;
    int t = p[*off]; size_t l = p[*off + 1], h = 2;
    if(l & 0x80){ int n = (int)(l & 0x7f); if(n < 1 || n > 4 || *off + 2 + (size_t)n > len)return -1; l = 0; for(int i = 0; i < n; i++)l = (l << 8) | p[*off + 2 + i]; h = 2 + (size_t)n; }
    if(*off + h + l > len)return -1;
    *tag = t; *v = p + *off + h; *vl = l; *off += h + l; return 0;
}
static int san_has_ip(const unsigned char* c, size_t n, const unsigned char ip[4]){
    size_t o = 0, o2 = 0; int t; const unsigned char *cert, *tbs, *v; size_t cl, tl, vl;
    if(der_next(c, n, &o, &t, &cert, &cl) || t != 0x30)return 0;
    if(der_next(cert, cl, &o2, &t, &tbs, &tl) || t != 0x30)return 0;
    size_t o3 = 0;
    while(o3 < tl){
        if(der_next(tbs, tl, &o3, &t, &v, &vl))return 0;
        if(t != 0xA3)continue;
        size_t o4 = 0; const unsigned char* exts; size_t el;
        if(der_next(v, vl, &o4, &t, &exts, &el) || t != 0x30)return 0;
        size_t o5 = 0;
        while(o5 < el){
            const unsigned char* ext; size_t xl; if(der_next(exts, el, &o5, &t, &ext, &xl) || t != 0x30)return 0;
            size_t o6 = 0; const unsigned char* oid; size_t ol; if(der_next(ext, xl, &o6, &t, &oid, &ol) || t != 0x06)return 0;
            if(ol != 3 || oid[0] != 0x55 || oid[1] != 0x1D || oid[2] != 0x11)continue;
            const unsigned char* os; size_t osl;
            if(der_next(ext, xl, &o6, &t, &os, &osl))return 0;
            if(t == 0x01 && der_next(ext, xl, &o6, &t, &os, &osl))return 0;
            if(t != 0x04)return 0;
            size_t o7 = 0; const unsigned char* gn; size_t gl;
            if(der_next(os, osl, &o7, &t, &gn, &gl) || t != 0x30)return 0;
            size_t o8 = 0;
            while(o8 < gl){ const unsigned char* nm; size_t nl; if(der_next(gn, gl, &o8, &t, &nm, &nl))return 0; if(t == 0x87 && nl == 4 && !memcmp(nm, ip, 4))return 1; }
            return 0;
        }
    }
    return 0;
}

struct ipcheck { const br_x509_class* vtable; br_x509_minimal_context* inner; unsigned char ip[4]; int depth; struct dbuf2 leaf; };
static void ip_start_chain(const br_x509_class** ctx, const char* name){ struct ipcheck* c = (struct ipcheck*)ctx; (void)name; c->depth = 0; c->leaf.n = 0; c->inner->vtable->start_chain(&c->inner->vtable, NULL); }
static void ip_start_cert(const br_x509_class** ctx, uint32_t len){ struct ipcheck* c = (struct ipcheck*)ctx; c->inner->vtable->start_cert(&c->inner->vtable, len); }
static void ip_append(const br_x509_class** ctx, const unsigned char* b, size_t len){ struct ipcheck* c = (struct ipcheck*)ctx; c->inner->vtable->append(&c->inner->vtable, b, len); if(!c->depth)d2_add(&c->leaf, b, len); }
static void ip_end_cert(const br_x509_class** ctx){ struct ipcheck* c = (struct ipcheck*)ctx; c->inner->vtable->end_cert(&c->inner->vtable); c->depth++; }
static unsigned ip_end_chain(const br_x509_class** ctx){ struct ipcheck* c = (struct ipcheck*)ctx; unsigned e = c->inner->vtable->end_chain(&c->inner->vtable); if(e)return e; return san_has_ip(c->leaf.p, c->leaf.n, c->ip) ? 0 : BR_ERR_X509_BAD_SERVER_NAME; }
static const br_x509_pkey* ip_get_pkey(const br_x509_class* const* ctx, unsigned* usages){ const struct ipcheck* c = (const struct ipcheck*)ctx; return c->inner->vtable->get_pkey((const br_x509_class* const*)&c->inner->vtable, usages); }
static const br_x509_class ipcheck_vt = { sizeof(struct ipcheck), ip_start_chain, ip_start_cert, ip_append, ip_end_cert, ip_end_chain, ip_get_pkey };

struct nocheck { const br_x509_class* vtable; br_x509_decoder_context dc; int depth, have; };
static void nc_start_chain(const br_x509_class** ctx, const char* name){ struct nocheck* c = (struct nocheck*)ctx; (void)name; c->depth = 0; c->have = 0; }
static void nc_start_cert(const br_x509_class** ctx, uint32_t len){ struct nocheck* c = (struct nocheck*)ctx; (void)len; if(!c->depth)br_x509_decoder_init(&c->dc, NULL, NULL); }
static void nc_append(const br_x509_class** ctx, const unsigned char* b, size_t len){ struct nocheck* c = (struct nocheck*)ctx; if(!c->depth)br_x509_decoder_push(&c->dc, b, len); }
static void nc_end_cert(const br_x509_class** ctx){ struct nocheck* c = (struct nocheck*)ctx; if(!c->depth)c->have = br_x509_decoder_get_pkey(&c->dc) != NULL; c->depth++; }
static unsigned nc_end_chain(const br_x509_class** ctx){ struct nocheck* c = (struct nocheck*)ctx; return c->have ? 0 : BR_ERR_X509_UNSUPPORTED; }
static const br_x509_pkey* nc_get_pkey(const br_x509_class* const* ctx, unsigned* usages){ struct nocheck* c = (struct nocheck*)ctx; if(usages)*usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN; return c->have ? br_x509_decoder_get_pkey(&c->dc) : NULL; }
static const br_x509_class nocheck_vt = { sizeof(struct nocheck), nc_start_chain, nc_start_cert, nc_append, nc_end_cert, nc_end_chain, nc_get_pkey };

struct conn {
    int fd, tls;
    br_ssl_client_context sc; br_x509_minimal_context xc; br_sslio_context io;
    struct ipcheck ipc; struct nocheck noc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
};
static int conn_read(struct conn* c, unsigned char* b, size_t n){
    if(!c->tls){ for(;;){ ssize_t r = read(c->fd, b, n); if(r < 0 && errno == EINTR)continue; return r < 0 ? -1 : (int)r; } }
    int r = br_sslio_read(&c->io, b, n);
    if(r < 0){ int e = br_ssl_engine_last_error(&c->sc.eng); return (e == BR_ERR_OK || e == BR_ERR_IO) ? 0 : -1; }
    return r;
}
static int conn_write(struct conn* c, const void* b, size_t n){
    if(!c->tls){ size_t o = 0; while(o < n){ ssize_t r = write(c->fd, (const char*)b + o, n - o); if(r < 0 && errno == EINTR)continue; if(r <= 0)return -1; o += (size_t)r; } return 0; }
    if(br_sslio_write_all(&c->io, b, n) < 0)return -1;
    return br_sslio_flush(&c->io);
}
static const char* suite_name(unsigned s){
    switch(s){
    case 0xC02B: return "ECDHE-ECDSA-AES128-GCM-SHA256"; case 0xC02C: return "ECDHE-ECDSA-AES256-GCM-SHA384";
    case 0xC02F: return "ECDHE-RSA-AES128-GCM-SHA256"; case 0xC030: return "ECDHE-RSA-AES256-GCM-SHA384";
    case 0xCCA9: return "ECDHE-ECDSA-CHACHA20-POLY1305"; case 0xCCA8: return "ECDHE-RSA-CHACHA20-POLY1305";
    case 0xC023: return "ECDHE-ECDSA-AES128-SHA256"; case 0xC027: return "ECDHE-RSA-AES128-SHA256";
    }
    return "other";
}
static int tls_start(struct conn* c, const struct url* u){
    if(!g_ta_loaded){ const char* er = getenv("NVX_ROOTS"); const char* el = getenv("NVX_LOCAL_CA"); int n = nvx_anchors_load(&g_ta, er ? er : ROOTS, el ? el : LOCAL_CA); g_ta_loaded = 1; if(verbose)fprintf(stderr, "fetch: %d trust anchors loaded\n", n < 0 ? 0 : n); }
    struct in_addr ia; int is_ip = inet_aton(u->host, &ia) != 0;
    br_ssl_client_init_full(&c->sc, &c->xc, g_ta.v, g_ta.n);
    if(insecure){ c->noc.vtable = &nocheck_vt; br_ssl_engine_set_x509(&c->sc.eng, &c->noc.vtable); }
    else if(is_ip){ c->ipc.vtable = &ipcheck_vt; c->ipc.inner = &c->xc; memcpy(c->ipc.ip, &ia.s_addr, 4); br_ssl_engine_set_x509(&c->sc.eng, &c->ipc.vtable); }
    br_ssl_engine_set_buffer(&c->sc.eng, c->iobuf, sizeof c->iobuf, 1);
    if(!br_ssl_client_reset(&c->sc, is_ip ? NULL : u->host, 0))return -1;
    br_sslio_init(&c->io, &c->sc.eng, sock_read, &c->fd, sock_write, &c->fd);
    if(br_sslio_flush(&c->io) < 0 || br_ssl_engine_current_state(&c->sc.eng) == BR_SSL_CLOSED){
        int e = br_ssl_engine_last_error(&c->sc.eng);
        if(!quiet)fprintf(stderr, "fetch: TLS with %s failed: %s (error %d)\n", u->host, nvx_tls_error(e), e);
        return -1;
    }
    if(verbose || !quiet){
        br_ssl_session_parameters sp; br_ssl_engine_get_session_parameters(&c->sc.eng, &sp);
        unsigned v = br_ssl_engine_get_version(&c->sc.eng);
        fprintf(stderr, "fetch: TLS 1.%u %s, %s\n", (v & 0xFF) - 1, suite_name(sp.cipher_suite), insecure ? "certificate NOT checked (-k)" : "certificate verified");
    }
    return 0;
}

static int hex_val(int ch){ if(ch >= '0' && ch <= '9')return ch - '0'; if(ch >= 'a' && ch <= 'f')return ch - 'a' + 10; if(ch >= 'A' && ch <= 'F')return ch - 'A' + 10; return -1; }

struct out { FILE* f; long long total; br_sha256_context sha; };
static int emit(struct out* o, const unsigned char* b, size_t n){
    if(!n)return 0;
    if(fwrite(b, 1, n, o->f) != n)return -1;
    o->total += (long long)n; br_sha256_update(&o->sha, b, n); return 0;
}

static void conn_free(struct conn* c){ free(c->ipc.leaf.p); free(c); }
static int do_get(const struct url* u, struct out* o, char* loc, size_t locsz, int* status){
    struct conn* c = calloc(1, sizeof *c); if(!c)return 2;
    c->fd = dial(u); if(c->fd < 0){ conn_free(c); return 2; }
    c->tls = u->tls;
    if(c->tls && tls_start(c, u)){ close(c->fd); conn_free(c); return 3; }
    char req[4096]; int rl;
    if((u->tls && u->port == 443) || (!u->tls && u->port == 80))rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: fetch/NoovexOS\r\nAccept: */*\r\nConnection: close\r\n\r\n", u->path, u->host);
    else rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUser-Agent: fetch/NoovexOS\r\nAccept: */*\r\nConnection: close\r\n\r\n", u->path, u->host, u->port);
    if(conn_write(c, req, (size_t)rl)){ msg("fetch: sending the request to %s failed%s\n", u->host, ""); close(c->fd); conn_free(c); return 2; }
    static unsigned char buf[16384]; struct dbuf2 hdr = {0}; size_t hend = 0;
    for(;;){
        int r = conn_read(c, buf, sizeof buf);
        if(r <= 0)break;
        d2_add(&hdr, buf, (size_t)r);
        for(size_t i = 3; i < hdr.n; i++) if(hdr.p[i-3] == '\r' && hdr.p[i-2] == '\n' && hdr.p[i-1] == '\r' && hdr.p[i] == '\n'){ hend = i + 1; break; }
        if(hend || hdr.n > 65536)break;
    }
    if(!hend){ msg("fetch: no HTTP response from %s%s\n", u->host, ""); free(hdr.p); close(c->fd); conn_free(c); return 2; }
    char* h = (char*)hdr.p; h[hend - 1] = 0;
    int code = 0; if(sscanf(h, "HTTP/%*d.%*d %d", &code) != 1){ msg("fetch: bad status line from %s%s\n", u->host, ""); free(hdr.p); close(c->fd); conn_free(c); return 2; }
    *status = code;
    long long clen = -1; int chunked = 0; loc[0] = 0;
    for(char* line = strstr(h, "\r\n"); line; line = strstr(line, "\r\n")){
        line += 2; char* end = strstr(line, "\r\n"); size_t L = end ? (size_t)(end - line) : strlen(line);
        if(!strncasecmp(line, "Content-Length:", 15))clen = atoll(line + 15);
        else if(!strncasecmp(line, "Transfer-Encoding:", 18)){ char t[64]; size_t k = L - 18 < 63 ? L - 18 : 63; memcpy(t, line + 18, k); t[k] = 0; if(strcasestr(t, "chunked"))chunked = 1; }
        else if(!strncasecmp(line, "Location:", 9)){ const char* v = line + 9; while(*v == ' ')v++; size_t k = L - (size_t)(v - line); if(k >= locsz)k = locsz - 1; memcpy(loc, v, k); loc[k] = 0; }
        if(verbose){ fprintf(stderr, "< %.*s\n", (int)L, line); }
        if(!end)break;
    }
    if(verbose)fprintf(stderr, "< %.*s\n", (int)(strstr(h, "\r\n") ? strstr(h, "\r\n") - h : 0), h);
    int redirect = (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && loc[0];
    struct out sink = *o; FILE* devnull = NULL;
    if(redirect){ devnull = fopen("/dev/null", "w"); sink.f = devnull; }
    unsigned char* rest = hdr.p + hend; size_t rn = hdr.n - hend; int rc = 0;
    if(!chunked){
        long long got = 0;
        if(rn){ size_t k = rn; if(clen >= 0 && (long long)k > clen)k = (size_t)clen; if(!redirect && emit(&sink, rest, k))rc = 5; else got += (long long)k; }
        while(!rc && (clen < 0 || got < clen)){
            int r = conn_read(c, buf, sizeof buf); if(r < 0){ rc = 3; break; } if(r == 0)break;
            size_t k = (size_t)r; if(clen >= 0 && got + (long long)k > clen)k = (size_t)(clen - got);
            if(!redirect && emit(&sink, buf, k))rc = 5;
            got += (long long)k;
        }
        if(!rc && clen >= 0 && got < clen){ msg("fetch: connection closed early%s%s\n", "", ""); rc = 2; }
    } else {
        struct dbuf2 in = {0}; d2_add(&in, rest, rn); size_t pos = 0; int done = 0;
        while(!done && !rc){
            size_t nl = 0; int found = 0; for(size_t i = pos; i + 1 < in.n; i++) if(in.p[i] == '\r' && in.p[i+1] == '\n'){ nl = i; found = 1; break; }
            if(!found){ int r = conn_read(c, buf, sizeof buf); if(r <= 0){ rc = 2; break; } d2_add(&in, buf, (size_t)r); continue; }
            long long sz = 0; int any = 0; for(size_t q = pos; q < nl; q++){ int hv = hex_val(in.p[q]); if(hv < 0)break; sz = sz * 16 + hv; any = 1; }
            if(!any || sz > (1LL << 40)){ rc = 2; break; }
            size_t need = nl + 2 + (size_t)sz + 2;
            while(in.n < need){ int r = conn_read(c, buf, sizeof buf); if(r <= 0){ rc = 2; break; } d2_add(&in, buf, (size_t)r); }
            if(rc)break;
            if(sz == 0){ done = 1; break; }
            if(!redirect && emit(&sink, in.p + nl + 2, (size_t)sz))rc = 5;
            pos = need;
            if(pos > 1 << 20){ memmove(in.p, in.p + pos, in.n - pos); in.n -= pos; pos = 0; }
        }
        free(in.p);
    }
    if(devnull)fclose(devnull);
    if(!redirect){ o->total = sink.total; o->sha = sink.sha; }
    if(c->tls)br_sslio_close(&c->io);
    close(c->fd); free(hdr.p); conn_free(c);
    if(rc)return rc;
    return redirect ? -1 : 0;
}

static void usage(void){
    fprintf(stderr, "usage: fetch [-k] [-q] [-v] [-s] [-o FILE] URL\n"
        "  Downloads URL (http:// or https://, https if no scheme is given) to standard output or FILE.\n"
        "  HTTPS checks the server certificate against the roots in %s and %s.\n"
        "  -k  do not check the certificate   -s  print the SHA-256 of the body   -v  show headers   -q  quiet\n", ROOTS, LOCAL_CA);
}
int main(int argc, char** argv){
    const char* outp = NULL; const char* target = NULL;
    for(int i = 1; i < argc; i++){
        if(!strcmp(argv[i], "-k"))insecure = 1;
        else if(!strcmp(argv[i], "-v"))verbose = 1;
        else if(!strcmp(argv[i], "-q"))quiet = 1;
        else if(!strcmp(argv[i], "-s"))want_sha = 1;
        else if(!strcmp(argv[i], "-o") && i + 1 < argc)outp = argv[++i];
        else if(argv[i][0] == '-'){ usage(); return 1; }
        else target = argv[i];
    }
    if(!target){ usage(); return 1; }
    struct url u, nu;
    if(parse_url(target, &u, NULL)){ fprintf(stderr, "fetch: bad URL: %s\n", target); return 1; }
    struct out o; o.total = 0; br_sha256_init(&o.sha);
    o.f = outp ? fopen(outp, "wb") : stdout;
    if(!o.f){ fprintf(stderr, "fetch: cannot write %s: %s\n", outp, strerror(errno)); return 5; }
    char loc[2048]; int status = 0, rc = 0;
    for(int hop = 0; ; hop++){
        rc = do_get(&u, &o, loc, sizeof loc, &status);
        if(rc != -1)break;
        if(hop >= 5){ fprintf(stderr, "fetch: too many redirects\n"); rc = 4; break; }
        if(parse_url(loc, &nu, &u)){ fprintf(stderr, "fetch: bad redirect: %s\n", loc); rc = 4; break; }
        if(!quiet)fprintf(stderr, "fetch: %d redirect to %s://%s:%d%s\n", status, nu.tls ? "https" : "http", nu.host, nu.port, nu.path);
        u = nu;
    }
    fflush(o.f);
    if(outp)fclose(o.f);
    if(!rc && status >= 400)rc = 4;
    if(!quiet && status)fprintf(stderr, "fetch: %s://%s:%d%s -> HTTP %d, %lld bytes\n", u.tls ? "https" : "http", u.host, u.port, u.path, status, o.total);
    if(want_sha && !rc){ unsigned char d[32]; char hx[65]; br_sha256_out(&o.sha, d); for(int i = 0; i < 32; i++)snprintf(hx + 2 * i, 3, "%02x", d[i]); fprintf(stderr, "fetch: sha256 %s\n", hx); }
    if(g_ta_loaded)nvx_anchors_free(&g_ta);
    return rc;
}
