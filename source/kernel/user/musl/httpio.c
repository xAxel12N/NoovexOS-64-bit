#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "bearssl.h"
#include "nvxtls.h"
#include "httpio.h"

#define ROOTS "/etc/ssl/nvx-anchors.pem"
#define LOCAL_CA "/etc/ssl/local-ca.pem"

struct hurl { int tls; char host[256]; int port; char path[3072]; };
struct nocheck { const br_x509_class* vtable; br_x509_decoder_context dc; int depth, have; };
struct hio {
    struct hurl u;
    int fd, tls, chunked, eof;
    long long pos, size, body_left, chunk_left;
    int ranges;
    br_ssl_client_context sc; br_x509_minimal_context xc; br_sslio_context io; struct nocheck noc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
    unsigned char rb[16384]; int rpos, rlen;
    unsigned char* ring; long long rstart, lpos;
};
#define RING (8 << 20)
static nvx_anchors g_ta;
static int g_ta_loaded;

static int parse_url(const char* s, struct hurl* u, const struct hurl* base) {
    memset(u, 0, sizeof *u);
    if (base && s[0] == '/' && s[1] != '/') { *u = *base; snprintf(u->path, sizeof u->path, "%s", s); return 0; }
    const char* p = s;
    if (base && s[0] == '/' && s[1] == '/') { u->tls = base->tls; p = s + 2; }
    else if (!strncasecmp(p, "https://", 8)) { u->tls = 1; p += 8; }
    else if (!strncasecmp(p, "http://", 7)) p += 7;
    else return -1;
    u->port = u->tls ? 443 : 80;
    size_t hl = strcspn(p, ":/?#");
    if (!hl || hl >= sizeof u->host) return -1;
    memcpy(u->host, p, hl);
    u->host[hl] = 0;
    p += hl;
    if (*p == ':') { char* e; long v = strtol(p + 1, &e, 10); if (e == p + 1 || v <= 0 || v > 65535) return -1; u->port = (int)v; p = e; }
    if (*p == '#' || !*p) snprintf(u->path, sizeof u->path, "/");
    else if (*p == '?') snprintf(u->path, sizeof u->path, "/%s", p);
    else snprintf(u->path, sizeof u->path, "%s", p);
    char* h = strchr(u->path, '#');
    if (h) *h = 0;
    return 0;
}

static int sock_read(void* ctx, unsigned char* buf, size_t len) { int fd = *(int*)ctx; for (;;) { ssize_t r = read(fd, buf, len); if (r < 0 && errno == EINTR) continue; return r <= 0 ? -1 : (int)r; } }
static int sock_write(void* ctx, const unsigned char* buf, size_t len) { int fd = *(int*)ctx; for (;;) { ssize_t r = write(fd, buf, len); if (r < 0 && errno == EINTR) continue; return r <= 0 ? -1 : (int)r; } }

static void nc_start_chain(const br_x509_class** ctx, const char* name) { struct nocheck* c = (struct nocheck*)ctx; (void)name; c->depth = 0; c->have = 0; }
static void nc_start_cert(const br_x509_class** ctx, uint32_t len) { struct nocheck* c = (struct nocheck*)ctx; (void)len; if (!c->depth) br_x509_decoder_init(&c->dc, NULL, NULL); }
static void nc_append(const br_x509_class** ctx, const unsigned char* b, size_t len) { struct nocheck* c = (struct nocheck*)ctx; if (!c->depth) br_x509_decoder_push(&c->dc, b, len); }
static void nc_end_cert(const br_x509_class** ctx) { struct nocheck* c = (struct nocheck*)ctx; if (!c->depth) c->have = br_x509_decoder_get_pkey(&c->dc) != NULL; c->depth++; }
static unsigned nc_end_chain(const br_x509_class** ctx) { struct nocheck* c = (struct nocheck*)ctx; return c->have ? 0 : BR_ERR_X509_UNSUPPORTED; }
static const br_x509_pkey* nc_get_pkey(const br_x509_class* const* ctx, unsigned* usages) { struct nocheck* c = (struct nocheck*)ctx; if (usages) *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN; return c->have ? br_x509_decoder_get_pkey(&c->dc) : NULL; }
static const br_x509_class nocheck_vt = { sizeof(struct nocheck), nc_start_chain, nc_start_cert, nc_append, nc_end_cert, nc_end_chain, nc_get_pkey };

static int dial(const struct hurl* u) {
    char port[8];
    snprintf(port, sizeof port, "%d", u->port);
    struct addrinfo hints, *ai = NULL, *a;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(u->host, port, &hints, &ai)) return -1;
    int fd = -1;
    for (a = ai; a; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(ai);
    return fd;
}

static int raw_read(struct hio* h, unsigned char* b, size_t n) {
    if (!h->tls) { for (;;) { ssize_t r = read(h->fd, b, n); if (r < 0 && errno == EINTR) continue; return r < 0 ? -1 : (int)r; } }
    int r = br_sslio_read(&h->io, b, n);
    if (r < 0) { int e = br_ssl_engine_last_error(&h->sc.eng); return (e == BR_ERR_OK || e == BR_ERR_IO) ? 0 : -1; }
    return r;
}
static int buf_fill(struct hio* h) {
    if (h->rpos < h->rlen) return h->rlen - h->rpos;
    int r = raw_read(h, h->rb, sizeof h->rb);
    if (r <= 0) return r;
    h->rpos = 0;
    h->rlen = r;
    return r;
}
static int buf_getc(struct hio* h) { if (buf_fill(h) <= 0) return -1; return h->rb[h->rpos++]; }
static int read_line(struct hio* h, char* out, int cap) {
    int n = 0;
    for (;;) {
        int c = buf_getc(h);
        if (c < 0) return n ? n : -1;
        if (c == '\n') break;
        if (c != '\r' && n < cap - 1) out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}
static int conn_write(struct hio* h, const void* b, size_t n) {
    if (!h->tls) { size_t o = 0; while (o < n) { ssize_t r = write(h->fd, (const char*)b + o, n - o); if (r < 0 && errno == EINTR) continue; if (r <= 0) return -1; o += (size_t)r; } return 0; }
    if (br_sslio_write_all(&h->io, b, n) < 0) return -1;
    return br_sslio_flush(&h->io);
}
static void disconnect(struct hio* h) {
    if (h->fd >= 0) { if (h->tls) br_sslio_close(&h->io); close(h->fd); }
    h->fd = -1;
    h->rpos = h->rlen = 0;
}
static int tls_start(struct hio* h) {
    if (!g_ta_loaded) { nvx_anchors_load(&g_ta, ROOTS, LOCAL_CA); g_ta_loaded = 1; }
    struct in_addr ia;
    int is_ip = inet_aton(h->u.host, &ia) != 0;
    br_ssl_client_init_full(&h->sc, &h->xc, g_ta.v, g_ta.n);
    if (is_ip || getenv("NVX_INSECURE")) { h->noc.vtable = &nocheck_vt; br_ssl_engine_set_x509(&h->sc.eng, &h->noc.vtable); }
    br_ssl_engine_set_buffer(&h->sc.eng, h->iobuf, sizeof h->iobuf, 1);
    if (!br_ssl_client_reset(&h->sc, is_ip ? NULL : h->u.host, 0)) return -1;
    br_sslio_init(&h->io, &h->sc.eng, sock_read, &h->fd, sock_write, &h->fd);
    if (br_sslio_flush(&h->io) < 0 || br_ssl_engine_current_state(&h->sc.eng) == BR_SSL_CLOSED) return -1;
    return 0;
}

static int net_read(struct hio* h, unsigned char* b, int n);
static int request(struct hio* h, long long from) {
    for (int hop = 0; hop < 8; hop++) {
        disconnect(h);
        h->fd = dial(&h->u);
        if (h->fd < 0) return -1;
        h->tls = h->u.tls;
        if (h->tls && tls_start(h)) { disconnect(h); return -1; }
        char req[4096];
        int rl;
        char hostport[300];
        if ((h->u.tls && h->u.port == 443) || (!h->u.tls && h->u.port == 80)) snprintf(hostport, sizeof hostport, "%s", h->u.host);
        else snprintf(hostport, sizeof hostport, "%s:%d", h->u.host, h->u.port);
        rl = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (X11; NoovexOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36\r\nAccept: */*\r\nRange: bytes=%lld-\r\nConnection: close\r\n\r\n", h->u.path, hostport, from);
        if (rl >= (int)sizeof req || conn_write(h, req, (size_t)rl)) { disconnect(h); return -1; }
        char line[4096];
        if (read_line(h, line, sizeof line) <= 0) { disconnect(h); return -1; }
        int code = 0;
        if (sscanf(line, "HTTP/%*d.%*d %d", &code) != 1) { disconnect(h); return -1; }
        long long clen = -1, total = -1;
        char loc[3072] = "";
        h->chunked = 0;
        int accept_ranges = 0;
        for (;;) {
            int n = read_line(h, line, sizeof line);
            if (n < 0) { disconnect(h); return -1; }
            if (n == 0) break;
            if (!strncasecmp(line, "Content-Length:", 15)) clen = atoll(line + 15);
            else if (!strncasecmp(line, "Content-Range:", 14)) { char* sl = strchr(line, '/'); if (sl && sl[1] != '*') total = atoll(sl + 1); }
            else if (!strncasecmp(line, "Transfer-Encoding:", 18) && strcasestr(line + 18, "chunked")) h->chunked = 1;
            else if (!strncasecmp(line, "Accept-Ranges:", 14) && strcasestr(line + 14, "bytes")) accept_ranges = 1;
            else if (!strncasecmp(line, "Location:", 9)) { const char* v = line + 9; while (*v == ' ') v++; snprintf(loc, sizeof loc, "%s", v); }
        }
        if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && loc[0]) {
            struct hurl nu;
            if (parse_url(loc, &nu, &h->u)) { disconnect(h); return -1; }
            h->u = nu;
            continue;
        }
        h->rstart = h->lpos = from;
        if (code == 416) { h->pos = from; h->body_left = 0; h->eof = 1; return 0; }
        if (code != 200 && code != 206) { disconnect(h); return -code; }
        if (code == 206) { h->ranges = 1; h->pos = from; if (total >= 0) h->size = total; else if (clen >= 0) h->size = from + clen; }
        else { h->ranges = accept_ranges; h->pos = 0; h->rstart = h->lpos = 0; if (clen >= 0) h->size = clen; }
        h->body_left = h->chunked ? -1 : clen;
        h->chunk_left = 0;
        h->eof = 0;
        if (code == 200 && from > 0) { unsigned char tmp[8192]; while (h->pos < from) { long long want = from - h->pos; int r = net_read(h, tmp, want > (long long)sizeof tmp ? (int)sizeof tmp : (int)want); if (r <= 0) break; } h->lpos = h->pos; }
        return 0;
    }
    return -1;
}

hio* hio_open(const char* url) {
    hio* h = calloc(1, sizeof *h);
    if (!h) return 0;
    h->ring = malloc(RING);
    if (!h->ring) { free(h); return 0; }
    h->fd = -1;
    h->size = -1;
    if (parse_url(url, &h->u, 0) || request(h, 0) < 0) { hio_close(h); return 0; }
    return h;
}

static int net_read(struct hio* h, unsigned char* b, int n) {
    if (h->eof || n <= 0) return 0;
    if (h->chunked) {
        if (h->chunk_left == 0) {
            char line[64];
            int k = read_line(h, line, sizeof line);
            if (k == 0) k = read_line(h, line, sizeof line);
            if (k < 0) { h->eof = 1; return 0; }
            h->chunk_left = strtoll(line, 0, 16);
            if (h->chunk_left <= 0) { h->eof = 1; return 0; }
        }
        if (n > h->chunk_left) n = (int)h->chunk_left;
    } else if (h->body_left >= 0) {
        if (h->body_left == 0) { h->eof = 1; return 0; }
        if (n > h->body_left) n = (int)h->body_left;
    }
    int got;
    if (h->rpos < h->rlen) { got = h->rlen - h->rpos; if (got > n) got = n; memcpy(b, h->rb + h->rpos, (size_t)got); h->rpos += got; }
    else { got = raw_read(h, b, (size_t)n); if (got <= 0) { h->eof = 1; return 0; } }
    for (int i = 0; i < got; ) {
        long long ap = h->pos + i;
        int ri = (int)(ap % RING), k = got - i;
        if (k > RING - ri) k = RING - ri;
        memcpy(h->ring + ri, b + i, (size_t)k);
        i += k;
    }
    h->pos += got;
    if (h->pos - h->rstart > RING) h->rstart = h->pos - RING;
    if (h->chunked) h->chunk_left -= got;
    else if (h->body_left > 0) h->body_left -= got;
    return got;
}

int hio_read(hio* h, unsigned char* b, int n) {
    if (n <= 0) return 0;
    if (h->lpos < h->rstart) h->lpos = h->rstart;
    if (h->lpos < h->pos) {
        long long av = h->pos - h->lpos;
        int k = av < n ? (int)av : n;
        int ri = (int)(h->lpos % RING);
        if (k > RING - ri) k = RING - ri;
        memcpy(b, h->ring + ri, (size_t)k);
        h->lpos += k;
        return k;
    }
    int r = net_read(h, b, n);
    if (r > 0) h->lpos = h->pos;
    return r;
}

long long hio_seek(hio* h, long long pos) {
    if (pos >= h->rstart && pos <= h->pos && !(pos == h->pos && h->eof && h->size > pos)) { h->lpos = pos; return pos; }
    if (pos > h->pos && pos - h->pos < 1024 * 1024 && !h->eof) {
        unsigned char tmp[16384];
        while (h->pos < pos) { long long want = pos - h->pos; int r = net_read(h, tmp, want > (long long)sizeof tmp ? (int)sizeof tmp : (int)want); if (r <= 0) break; }
        if (h->pos == pos) { h->lpos = pos; return pos; }
    }
    if (request(h, pos) < 0) return -1;
    return h->lpos;
}

long long hio_size(hio* h) { return h->size; }
long long hio_tell(hio* h) { return h->lpos; }
int hio_seekable(hio* h) { return h->ranges; }
void hio_close(hio* h) { if (!h) return; disconnect(h); free(h->ring); free(h); }
