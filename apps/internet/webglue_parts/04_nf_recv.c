static int nf_recv(nreq* q, pconn* c, int head, const char* cur, int* redirect, char* loc) {
    int he = -1, chunked = 0, clen = -1, until_close = 0, keep = 1, cpos = 0, fin = 0, got = 0;
    q->rawn = 0; q->bodn = 0; q->ctrail = 0;
    for (;;) {
        if (!nq_grow(&q->raw, &q->rawcap, q->rawn + 32768)) { pc_close(c); return got ? -1 : 0; }
        int k = pc_read(c, q->raw + q->rawn, q->rawcap - q->rawn - 1);
        if (k <= 0 || q->cancel) {
            if (!q->cancel && he >= 0 && until_close) { fin = 1; keep = 0; break; }
            pc_close(c); return got ? -1 : 0;
        }
        got = 1; g_nf_prog++;
        q->rawn += k;
        if (he < 0) {
            he = find_hdr_end(q->raw, q->rawn);
            if (he < 0) { if (q->rawn > 262144) { pc_close(c); return -1; } continue; }
            int bo = q->raw[he] == '\n' ? he + 1 : he + 2;
            int status = 0; { int i = 0; while (i < q->rawn && q->raw[i] != ' ') i++; i++; while (i < q->rawn && q->raw[i] >= '0' && q->raw[i] <= '9') status = status * 10 + (q->raw[i++] - '0'); }
            if (status == 100) { int left = q->rawn - bo; for (int i = 0; i < left; i++) q->raw[i] = q->raw[bo + i]; q->rawn = left; he = -1; continue; }
            char v[160];
            for (int i = 0; i < he; i++) {
                if (i && q->raw[i - 1] != '\n') continue;
                if (i + 11 < he && ieqn((const char*)q->raw + i, "set-cookie:", 11)) {
                    int j = i + 11; while (j < he && q->raw[j] == ' ') j++;
                    char* line = kmalloc(4096); if (!line) continue;
                    int n = 0; while (j < he && q->raw[j] != '\r' && q->raw[j] != '\n' && n < 4095) line[n++] = (char)q->raw[j++];
                    line[n] = 0; cookie_set(cur, line, 0); kfree(line);
                }
            }
            if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) && hdr_get(q->raw, he, "location", loc, 4096) && loc[0]) {
                q->status = status; *redirect = 1; pc_close(c); return 1;
            }
            if (hdr_get(q->raw, he, "connection", v, sizeof(v)) && ieqn(v, "close", 5)) keep = 0;
            if (q->rawn < 9 || q->raw[5] != '1' || q->raw[7] == '0') { if (!(hdr_get(q->raw, he, "connection", v, sizeof(v)) && ieqn(v, "keep-alive", 10))) keep = 0; }
            q->status = status;
            hdr_get(q->raw, he, "content-type", q->ctyp, sizeof(q->ctyp));
            q->gz = 0;
            if (hdr_get(q->raw, he, "content-encoding", v, sizeof(v))) { if (ieqn(v, "gzip", 4) || ieqn(v, "x-gzip", 6)) q->gz = 1; else if (ieqn(v, "deflate", 7)) q->gz = 2; }
            { int hs = 0; while (hs < he && q->raw[hs] != '\n') hs++; if (hs < he) hs++; int hl = he - hs; if (hl < 0) hl = 0; if (hl > 32768) hl = 32768; q->hdrs = nq_dup((const char*)q->raw + hs, hl); q->hlen = q->hdrs ? hl : 0; }
            if (q->final_url) kfree(q->final_url);
            q->final_url = nq_dup(cur, -1);
            if (hdr_get(q->raw, he, "transfer-encoding", v, sizeof(v))) { for (int i = 0; v[i]; i++) if (ieqn(v + i, "chunked", 7)) chunked = 1; }
            if (!chunked && hdr_get(q->raw, he, "content-length", v, sizeof(v))) { long L = 0; int i = 0; while (v[i] == ' ') i++; while (v[i] >= '0' && v[i] <= '9') { L = L * 10 + (v[i] - '0'); i++; if (L > NQ_MAXBODY) break; } clen = L > NQ_MAXBODY ? NQ_MAXBODY : (int)L; }
            if (!chunked && clen < 0) { until_close = 1; keep = 0; }
            q->hdr_ready = 1;
            int left = q->rawn - bo; for (int i = 0; i < left; i++) q->raw[i] = q->raw[bo + i]; q->rawn = left;
            if (head || status == 204 || status == 304 || (status >= 100 && status < 200) || clen == 0) { q->rawn = 0; fin = 1; break; }
        }
        if (nf_body(q, &cpos, chunked, clen, &fin) < 0) { pc_close(c); return -1; }
        if (fin) break;
    }
    if (keep && fin) { c->last = g_ticks; pc_put(c); } else pc_close(c);
    return 1;
}

static void nf_run(nreq* q) {
    purl* u = kmalloc(sizeof(purl));
    char* cur = kmalloc(4096); char* loc = kmalloc(4096); char* nu = kmalloc(4096);
    char meth[16];
    if (!u || !cur || !loc || !nu) { q->err = -1; goto out; }
    { int k = 0; for (; q->url[k] && k < 4095; k++) cur[k] = q->url[k]; cur[k] = 0; for (k = 0; q->meth[k] && k < 15; k++) meth[k] = q->meth[k]; meth[k] = 0; }
    const char* b = q->body; int bl = q->blen, hops = 0;
    for (;;) {
        if (q->cancel) { q->err = -7; goto out; }
        if (parse_url(cur, u) < 0) { q->err = -1; goto out; }
        int reqcap = 65536 + bl;
        char* req = kmalloc((unsigned long)reqcap);
        if (!req) { q->err = -1; goto out; }
        g_nwk_kind = 2; g_nwk_xhdr = q->xhdr;
        char* p = req_head(req, u, cur, meth, q->referer);
        if (meth[0] == 'P' || meth[0] == 'D') {
            int hb = b && bl > 0;
            if (q->ctype && q->ctype[0]) { p = put(p, "Content-Type: "); p = put(p, q->ctype); p = put(p, "\r\n"); }
            p = put(p, "Content-Length: "); p = putu(p, (unsigned)(hb ? bl : 0)); p = put(p, "\r\n");
        }
        p = put(p, "\r\n");
        if (b && bl > 0 && (meth[0] == 'P' || meth[0] == 'D')) p = putn(p, b, bl);
        int rl = (int)(p - req), redirect = 0, rc = -3;
        for (int attempt = 0; attempt < 2; attempt++) {
            int err = 0;
            pconn* c = pc_get(u, attempt > 0, &err);
            if (!c) { rc = err ? err : -2; break; }
            int reused = c->reused;
            if (!http_send(c, req, rl)) { pc_close(c); rc = -3; continue; }
            int r2 = nf_recv(q, c, meth[0] == 'H', cur, &redirect, loc);
            if (r2 == 0 && reused && attempt == 0 && !q->cancel) continue;
            rc = r2 == 1 ? 0 : r2 == 0 ? -3 : -4;
            break;
        }
        kfree(req);
        if (rc < 0) { q->err = rc; goto out; }
        if (redirect) {
            if (++hops > 40) { q->err = -6; goto out; }
            resolve(cur, loc, nu, 4096);
            int i = 0; for (; nu[i] && i < 4095; i++) cur[i] = nu[i]; cur[i] = 0;
            if (q->status == 303 || ((q->status == 301 || q->status == 302) && meth[0] == 'P')) { meth[0] = 'G'; meth[1] = 'E'; meth[2] = 'T'; meth[3] = 0; b = 0; bl = 0; }
            q->status = 0;
            continue;
        }
        q->ok = 1;
        break;
    }
out:
    if (!q->final_url && cur) q->final_url = nq_dup(cur, -1);
    if (u) kfree(u);
    if (cur) kfree(cur);
    if (loc) kfree(loc);
    if (nu) kfree(nu);
    if (q->raw) { kfree(q->raw); q->raw = 0; q->rawcap = 0; q->rawn = 0; }
    cookies_flush(0);
    q->state = 2;
}

static void nf_entry(void) {
    for (;;) {
        struct nfib* f = g_nf_cur;
        if (f->q) nf_run(f->q);
        f->q = 0; f->live = 0;
        nf_yield();
    }
}
static int nf_init(struct nfib* f) {
    if (f->stk) return 1;
    f->stk = kmalloc(NF_STK);
    f->scr = kmalloc(tls_scratch_size());
    { extern int mm_guard_range_bottom(void*, unsigned long); if (f->stk) mm_guard_range_bottom(f->stk, NF_STK); }
    if (!f->stk || !f->scr) { if (f->stk) kfree(f->stk); if (f->scr) kfree(f->scr); f->stk = 0; f->scr = 0; return 0; }
    u64 top = ((u64)(f->stk + NF_STK)) & ~15ull;
    u64* sp = (u64*)top;
    *--sp = 0;
    *--sp = (u64)nf_entry;
    for (int i = 0; i < 6; i++) *--sp = 0;
    f->rsp = (u64)sp;
    return 1;
}
static void nf_resume(struct nfib* f) {
    int kind = g_nwk_kind; const char* xh = g_nwk_xhdr; u64 tmo = g_pc_tmo; int ab = g_pc_abort; struct tls_scratch* scr = g_tls_scr;
    const char* su = g_stream_url; void (*st)(const char*, int, const char*) = g_nwk_stream;
    g_nwk_stream = 0; g_stream_url = 0; g_tls_scr = f->scr; g_pc_tmo = 2000; g_pc_abort = f->q ? f->q->cancel : 0;
    g_nf_cur = f;
    nf_swap(&g_nf_main_rsp, f->rsp);
    g_nf_cur = 0;
    g_nwk_kind = kind; g_nwk_xhdr = xh; g_pc_tmo = tmo; g_pc_abort = ab; g_tls_scr = scr; g_stream_url = su; g_nwk_stream = st;
}
static int nq_host_running(const char* h) { int n = 0; for (int i = 0; i < NQ_N; i++) if (g_nq[i].used && g_nq[i].state == 1 && pc_streq(g_nq[i].host, h)) n++; return n; }
static void nf_assign(void) {
    for (;;) {
        nreq* best = 0;
        for (int i = 0; i < NQ_N; i++) { nreq* q = &g_nq[i]; if (!q->used || q->state != 0) continue; if (q->cancel) { q->state = 2; q->err = -7; continue; } if (nq_host_running(q->host) >= NQ_PER_HOST) continue; if (!best || q->id < best->id) best = q; }
        if (!best) return;
        struct nfib* f = 0;
        for (int i = 0; i < NF_N; i++) if (!g_nf[i].live) { f = &g_nf[i]; break; }
        if (!f || !nf_init(f)) return;
        f->q = best; f->live = 1; best->state = 1;
    }
}
static void nf_round(void) {
    if (g_nf_stepping || g_nf_cur) return;
    g_nf_stepping = 1;
    nf_assign();
    int live = 0;
    for (int i = 0; i < NF_N; i++) if (g_nf[i].live) { nf_resume(&g_nf[i]); if (!g_nf[i].live) g_nf_prog++; }
    for (int i = 0; i < NQ_N; i++) if (g_nq[i].used && g_nq[i].state == 2 && g_nq[i].orphan) nq_free(&g_nq[i]);
    nf_assign();
    for (int i = 0; i < NF_N; i++) if (g_nf[i].live) live++;
    for (int i = 0; i < NQ_N; i++) if (g_nq[i].used && g_nq[i].state == 0) live++;
    g_nf_live = live;
    g_nf_stepping = 0;
}
void nwk_net_step(void) {
    if (!g_nf_live) return;
    unsigned long long t0 = nus();
    for (int r = 0; r < 64; r++) { unsigned p0 = g_nf_prog; nf_round(); if (!g_nf_live || g_nf_prog == p0 || nus() - t0 > 4000) break; }
}
static void nf_step_wait(void) { if (g_nf_live) nf_round(); }

int g_web_netbg = 1, g_web_csp = 1;
int nwk_net_start(long owner, const char* url, const char* method, const char* ctype, const char* body, int blen, const char* xhdr, const char* referer) {
    if (g_nwk_offline || !url || !g_web_netbg) return 0;
    nreq* q = 0;
    for (int i = 0; i < NQ_N; i++) if (!g_nq[i].used) { q = &g_nq[i]; break; }
    if (!q) return 0;
    static purl pu;
    if (parse_url(url, &pu) < 0) return 0;
    for (unsigned i = 0; i < sizeof(*q); i++) ((u8*)q)[i] = 0;
    q->used = 1; q->id = g_nq_next++; if (g_nq_next > 0x3FFFFFFF) g_nq_next = 1;
    q->owner = owner;
    { int k = 0; for (; pu.host[k] && k < 255; k++) q->host[k] = pu.host[k]; q->host[k] = 0; }
    q->url = nq_dup(url, -1); q->meth = nq_dup(method && method[0] ? method : "GET", -1);
    q->ctype = ctype ? nq_dup(ctype, -1) : 0; q->xhdr = xhdr ? nq_dup(xhdr, -1) : 0; q->referer = referer ? nq_dup(referer, -1) : 0;
    if (body && blen > 0) { q->body = nq_dup(body, blen); q->blen = q->body ? blen : 0; }
    if (!q->url || !q->meth) { nq_free(q); return 0; }
    int get = ieqn(q->meth, "GET", 4) && !q->blen;
    if (get) {
        int sv = g_reval_i; nwk_resp r;
        int hit = hc_lookup(url, &r);
        g_reval_i = sv;
        if (hit) {
            q->bod = (u8*)r.body; q->bodn = r.len; q->bodcap = r.len + 1; q->status = r.status;
            for (int k = 0; k < 95 && r.ctype[k]; k++) q->ctyp[k] = r.ctype[k];
            q->final_url = nq_dup(r.final_url[0] ? r.final_url : url, -1);
            q->hdr_ready = 1; q->ok = 1; q->state = 2;
            g_netstat.cache_hits++;
            return q->id;
        }
    }
    q->state = 0;
    g_nf_live = g_nf_live ? g_nf_live : 1;
    return q->id;
}
static void nq_decode(nreq* q, int full) {
    if (q->dec_full || q->bodn <= 0) return;
    if (!full && (q->bodn > (512 << 10) || g_ticks - q->dec_t < 10)) return;
    q->dec_t = g_ticks;
    int cap = q->bodn * 8; if (cap < 262144) cap = 262144;
    for (;;) {
        if (cap > q->deccap) { if (q->dec) kfree(q->dec); q->dec = kmalloc((unsigned long)cap); q->deccap = q->dec ? cap : 0; if (!q->dec) return; }
        int dl;
        if (!full) {
            const u8* s = q->bod; int n = q->bodn, p = 0;
            if (q->gz == 1) {
                if (n < 18 || s[0] != 0x1F || s[1] != 0x8B) return;
                int flg = s[3]; p = 10;
                if (flg & 4) { if (p + 2 > n) return; p += 2 + (s[p] | (s[p + 1] << 8)); }
                if (flg & 8) { while (p < n && s[p]) p++; p++; }
                if (flg & 16) { while (p < n && s[p]) p++; p++; }
                if (flg & 2) p += 2;
                if (p >= n) return;
            } else if (n > 2) p = 2;
            dl = pz_inflate_part(s + p, n - p, q->dec, cap - 1);
        } else if (q->gz == 1) dl = gunzip(q->bod, q->bodn, q->dec, cap - 1);
        else { dl = pz_inflate_raw(q->bod + 2, q->bodn - 2, q->dec, cap - 1); if (dl < 0) dl = pz_inflate_raw(q->bod, q->bodn, q->dec, cap - 1); }
        if (dl >= 0 && dl < cap - 1) { if (dl > q->decn) q->decn = dl; if (full) q->dec_full = 1; return; }
        if (cap >= (96 << 20)) { if (full) q->dec_full = -1; return; }
        cap *= 2;
    }
}
int nwk_net_event(long owner, nwk_netev* ev) {
    for (unsigned i = 0; i < sizeof(*ev); i++) ((u8*)ev)[i] = 0;
    for (int i = 0; i < NQ_N; i++) {
        nreq* q = &g_nq[i];
        if (!q->used || q->owner != owner || q->orphan) continue;
        if (q->done_sent) { nq_free(q); continue; }
        ev->id = q->id;
        if (q->hdr_ready && !q->hdr_sent) {
            q->hdr_sent = 1;
            ev->type = 1; ev->status = q->status; ev->ctype = q->ctyp; ev->url = q->final_url ? q->final_url : q->url; ev->hdrs = q->hdrs; ev->hlen = q->hlen;
            return 1;
        }
        if (q->hdr_sent) {
            const u8* src = q->bod; int avail = q->bodn;
            if (q->gz) {
                nq_decode(q, q->state == 2);
                if (q->state == 2 && q->dec_full < 0) { src = q->bod; avail = q->bodn; if (q->delivered > avail) q->delivered = avail; }
                else { src = q->dec; avail = q->decn; }
            }
            if (src && q->delivered < avail) {
                int n = avail - q->delivered; if (n > (4 << 20)) n = 4 << 20;
                ev->type = 2; ev->data = src + q->delivered; ev->dlen = n; q->delivered += n;
                return 1;
            }
        }
        if (q->state == 2) {
            ev->type = 3; ev->ok = q->ok && q->hdr_sent; ev->status = q->err;
            if (!q->ok && !g_nwk_offline) {
                char b[1200], *p = b; int k = 0;
                int rc = q->err;
                if (rc != -7) {
                    p = put(p, rc == -100 ? "Name lookup failed: " : rc == -2 ? "Could not connect: " : rc == -5 ? "Secure connection failed: " : rc == -4 ? "Download cut off: " : rc == -6 ? "Too many redirects: " : "No answer: ");
                    p = put(p, q->meth ? q->meth : "GET"); p = put(p, " ");
                    while (q->url[k] && k < 1100) *p++ = q->url[k++];
                    *p = 0; nwk_error(b);
                }
            } else if (q->status >= 400) { char b[1200], *p = b; int k = 0; p = put(p, "HTTP "); p = putu(p, (unsigned)q->status); p = put(p, ": "); p = put(p, q->meth); p = put(p, " "); while (q->url[k] && k < 1100) *p++ = q->url[k++]; *p = 0; nwk_error(b); }
            q->done_sent = 1;
            return 1;
        }
    }
    return 0;
}
void nwk_net_abort(long owner, int id) {
    for (int i = 0; i < NQ_N; i++) {
        nreq* q = &g_nq[i];
        if (!q->used || q->owner != owner || q->id != id) continue;
        if (q->state == 1) { q->cancel = 1; q->orphan = 1; }
        else nq_free(q);
    }
}
void nwk_net_keep(int id) { for (int i = 0; i < NQ_N; i++) if (g_nq[i].used && g_nq[i].id == id) { g_nq[i].keep = 1; return; } }
void nwk_net_cancel_owner(long owner) {
    for (int i = 0; i < NQ_N; i++) {
        nreq* q = &g_nq[i];
        if (!q->used || q->owner != owner) continue;
        if (q->keep && q->state != 2) { q->orphan = 1; continue; }
        if (q->state == 1) { q->cancel = 1; q->orphan = 1; }
        else nq_free(q);
    }
}
int nwk_net_active(long owner) { int n = 0; for (int i = 0; i < NQ_N; i++) if (g_nq[i].used && !g_nq[i].orphan && (owner == 0 || g_nq[i].owner == owner)) n++; return n; }
int nwk_net_list(char* out, int cap) {
    int o = 0;
    for (int i = 0; i < NQ_N && o < cap - 400; i++) {
        nreq* q = &g_nq[i];
        if (!q->used) continue;
        char* p = out + o;
        p = put(p, q->state == 0 ? "queued " : q->state == 1 ? (q->hdr_ready ? "receiving " : "waiting ") : "done ");
        p = put(p, q->meth); p = put(p, " ");
        int k = 0; while (q->url[k] && k < 200) *p++ = q->url[k++];
        p = put(p, " ("); p = putu(p, (unsigned)q->bodn); p = put(p, " bytes)\n");
        o = (int)(p - out);
    }
    out[o] = 0;
    return o;
}

