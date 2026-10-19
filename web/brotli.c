#include "web/nwe.h"

typedef unsigned char u8;
extern const unsigned char brd_bits[32];
extern const unsigned brd_offs[32];
extern const unsigned char brd_data[];
extern const unsigned char brt_ps[];
extern const unsigned short brt_map[];
extern const int brt_n;
extern const unsigned char brt_tr[];
extern const unsigned char brc_lut[2048];

typedef struct { const u8* p; int len; int pos; unsigned bit; int err; } br;

static unsigned rb(br* b, int n) {
    unsigned v = 0;
    for (int i = 0; i < n; i++) {
        if (b->pos >= b->len) { b->err = 1; return 0; }
        v |= (unsigned)((b->p[b->pos] >> b->bit) & 1) << i;
        if (++b->bit == 8) { b->bit = 0; b->pos++; }
    }
    return v;
}
static int rbit(br* b) {
    if (b->pos >= b->len) { b->err = 1; return 0; }
    int v = (b->p[b->pos] >> b->bit) & 1;
    if (++b->bit == 8) { b->bit = 0; b->pos++; }
    return v;
}
static void ralign(br* b) { if (b->bit) { b->bit = 0; b->pos++; } }

typedef struct { short cnt[16]; short* sym; int n; short single; } huff;

static int hbuild(huff* h, const u8* lens, int n) {
    for (int i = 0; i < 16; i++) h->cnt[i] = 0;
    for (int i = 0; i < n; i++) h->cnt[lens[i]]++;
    h->cnt[0] = 0;
    short offs[16];
    offs[1] = 0;
    for (int i = 1; i < 15; i++) offs[i + 1] = (short)(offs[i] + h->cnt[i]);
    int used = 0;
    for (int i = 0; i < n; i++) if (lens[i]) h->sym[offs[lens[i]]++] = (short)i, used++;
    h->n = used;
    h->single = -1;
    if (used == 1) for (int i = 0; i < n; i++) if (lens[i]) h->single = (short)i;
    return used;
}
static int hdec(br* b, huff* h) {
    if (h->single >= 0) return h->single;
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= rbit(b);
        int count = h->cnt[len];
        if (code - count < first) return h->sym[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (b->err) return 0;
    }
    b->err = 1;
    return 0;
}

static const u8 kCLL[16] = { 2, 2, 2, 3, 2, 2, 2, 4, 2, 2, 2, 3, 2, 2, 2, 4 };
static const u8 kCLV[16] = { 0, 4, 3, 2, 0, 4, 3, 1, 0, 4, 3, 2, 0, 4, 3, 5 };
static const u8 kCLOrder[18] = { 1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static int peek4(br* b) {
    br c = *b;
    int v = 0;
    for (int i = 0; i < 4; i++) { if (c.pos >= c.len) break; v |= ((c.p[c.pos] >> c.bit) & 1) << i; if (++c.bit == 8) { c.bit = 0; c.pos++; } }
    return v;
}

static int read_huff(br* b, int asize, huff* h, short* symstore) {
    u8 lens[704];
    memset(lens, 0, (size_t)asize);
    h->sym = symstore;
    int abits = 0;
    while ((1 << abits) < asize) abits++;
    int hskip = (int)rb(b, 2);
    if (hskip == 1) {
        int nsym = (int)rb(b, 2) + 1;
        int s[4];
        for (int i = 0; i < nsym; i++) { s[i] = (int)rb(b, abits); if (s[i] >= asize) { b->err = 1; return -1; } }
        for (int i = 0; i < nsym; i++) for (int j = i + 1; j < nsym; j++) if (s[i] == s[j]) { b->err = 1; return -1; }
        if (nsym == 1) { lens[s[0]] = 1; hbuild(h, lens, asize); h->single = (short)s[0]; return 0; }
        if (nsym == 2) { lens[s[0]] = 1; lens[s[1]] = 1; }
        else if (nsym == 3) { lens[s[0]] = 1; lens[s[1]] = 2; lens[s[2]] = 2; }
        else {
            if (rbit(b)) { lens[s[0]] = 1; lens[s[1]] = 2; lens[s[2]] = 3; lens[s[3]] = 3; }
            else { lens[s[0]] = 2; lens[s[1]] = 2; lens[s[2]] = 2; lens[s[3]] = 2; }
        }
        hbuild(h, lens, asize);
        return b->err ? -1 : 0;
    }
    u8 cl[18];
    memset(cl, 0, sizeof cl);
    int space = 32, num = 0;
    for (int i = hskip; i < 18; i++) {
        int pk = peek4(b);
        int l = kCLL[pk], v = kCLV[pk];
        rb(b, l);
        cl[kCLOrder[i]] = (u8)v;
        if (v) { space -= 32 >> v; num++; if (space - 1 >= 32 || space <= 0) break; }
    }
    if (num != 1 && space != 0) { b->err = 1; return -1; }
    huff ch;
    short chs[18];
    ch.sym = chs;
    hbuild(&ch, cl, 18);
    int sym = 0, prev = 8, repeat = 0, rlen = 0;
    int sp = 32768;
    while (sym < asize && sp > 0) {
        int c = hdec(b, &ch);
        if (b->err) return -1;
        if (c < 16) {
            repeat = 0;
            lens[sym++] = (u8)c;
            if (c) { prev = c; sp -= 32768 >> c; }
        } else {
            int eb = c == 16 ? 2 : 3;
            int nl = c == 16 ? prev : 0;
            if (rlen != nl) { repeat = 0; rlen = nl; }
            int old = repeat;
            if (repeat > 0) { repeat -= 2; repeat <<= eb; }
            repeat += (int)rb(b, eb) + 3;
            int d = repeat - old;
            if (sym + d > asize) { b->err = 1; return -1; }
            for (int i = 0; i < d; i++) lens[sym++] = (u8)rlen;
            if (rlen) sp -= d << (15 - rlen);
        }
    }
    if (sp != 0) { b->err = 1; return -1; }
    hbuild(h, lens, asize);
    return b->err ? -1 : 0;
}

static int vlu8(br* b) {
    if (!rbit(b)) return 0;
    int n = (int)rb(b, 3);
    if (!n) return 1;
    return (1 << n) + (int)rb(b, n);
}

static const unsigned short kBLBase[26] = { 1, 5, 9, 13, 17, 25, 33, 41, 49, 65, 81, 97, 113, 145, 177, 209, 241, 305, 369, 497, 753, 1265, 2289, 4337, 8433, 16625 };
static const u8 kBLBits[26] = { 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 7, 8, 9, 10, 11, 12, 13, 24 };
static const unsigned kInsBase[24] = { 0, 1, 2, 3, 4, 5, 6, 8, 10, 14, 18, 26, 34, 50, 66, 98, 130, 194, 322, 578, 1090, 2114, 6210, 22594 };
static const u8 kInsBits[24] = { 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 14, 24 };
static const unsigned kCopyBase[24] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 18, 22, 30, 38, 54, 70, 102, 134, 198, 326, 582, 1094, 2118 };
static const u8 kCopyBits[24] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 9, 10, 24 };
static const u8 kCellIns[11] = { 0, 0, 0, 0, 8, 8, 0, 16, 8, 16, 16 };
static const u8 kCellCopy[11] = { 0, 8, 0, 8, 0, 8, 16, 0, 16, 8, 16 };

typedef struct { int n; huff type, len; short* ts; short* ls; int rb0, rb1; int count; int cur; } bcat;

static int read_bcat(br* b, bcat* c, short* store) {
    c->n = vlu8(b) + 1;
    c->rb0 = 1; c->rb1 = 0; c->cur = 0;
    c->count = 1 << 24;
    if (c->n >= 2) {
        c->ts = store; c->ls = store + 260;
        if (read_huff(b, c->n + 2, &c->type, c->ts) < 0) return -1;
        if (read_huff(b, 26, &c->len, c->ls) < 0) return -1;
        int s = hdec(b, &c->len);
        c->count = (int)kBLBase[s] + (int)rb(b, kBLBits[s]);
    }
    return b->err ? -1 : 0;
}
static void switch_bcat(br* b, bcat* c) {
    int s = hdec(b, &c->type);
    int t;
    if (s == 0) t = c->rb0;
    else if (s == 1) t = c->rb1 + 1;
    else t = s - 2;
    if (t >= c->n) t -= c->n;
    c->rb0 = c->rb1;
    c->rb1 = t;
    c->cur = t;
    int l = hdec(b, &c->len);
    c->count = (int)kBLBase[l] + (int)rb(b, kBLBits[l]);
}

static int read_cmap(br* b, int size, int ntrees, u8* map) {
    int rlemax = 0;
    if (rbit(b)) rlemax = (int)rb(b, 4) + 1;
    huff h;
    static short hs[272];
    if (read_huff(b, ntrees + rlemax, &h, hs) < 0) return -1;
    int i = 0;
    while (i < size) {
        int c = hdec(b, &h);
        if (b->err) return -1;
        if (c == 0) map[i++] = 0;
        else if (c <= rlemax) {
            int r = (1 << c) + (int)rb(b, c);
            if (i + r > size) { b->err = 1; return -1; }
            while (r-- > 0) map[i++] = 0;
        } else map[i++] = (u8)(c - rlemax);
    }
    if (rbit(b)) {
        u8 mtf[256];
        for (int k = 0; k < 256; k++) mtf[k] = (u8)k;
        for (int k = 0; k < size; k++) {
            int idx = map[k];
            u8 v = mtf[idx];
            map[k] = v;
            for (; idx > 0; idx--) mtf[idx] = mtf[idx - 1];
            mtf[0] = v;
        }
    }
    return b->err ? -1 : 0;
}

static int upper(u8* p, int rem) {
    if (p[0] < 0xC0) { if (p[0] >= 'a' && p[0] <= 'z') p[0] ^= 32; return 1; }
    if (p[0] < 0xE0) { if (rem > 1) p[1] ^= 32; return 2; }
    if (rem > 2) p[2] ^= 5;
    return 3;
}
static int dict_word(u8* dst, int cap, int len, int widx, int tidx) {
    if (len < 4 || len > 24 || !brd_bits[len]) return -1;
    if (tidx >= brt_n) return -1;
    const u8* word = brd_data + brd_offs[len] + (unsigned)widx * (unsigned)len;
    const u8* pre = brt_ps + brt_map[brt_tr[tidx * 3]];
    int type = brt_tr[tidx * 3 + 1];
    const u8* suf = brt_ps + brt_map[brt_tr[tidx * 3 + 2]];
    int o = 0;
    int pl = *pre++;
    if (o + pl > cap) return -1;
    memcpy(dst + o, pre, (size_t)pl);
    o += pl;
    int wl = len;
    if (type <= 9) wl -= type;
    else if (type >= 12 && type <= 20) { int sk = type - 11; if (sk > wl) sk = wl; word += sk; wl -= sk; }
    if (wl < 0) wl = 0;
    if (o + wl > cap) return -1;
    memcpy(dst + o, word, (size_t)wl);
    u8* up = dst + o;
    if (type == 10 && wl > 0) upper(up, wl);
    else if (type == 11) { int r = wl; while (r > 0) { int st = upper(up, r); up += st; r -= st; } }
    o += wl;
    int sl = *suf++;
    if (o + sl > cap) return -1;
    memcpy(dst + o, suf, (size_t)sl);
    o += sl;
    return o;
}

int brotli_decode(const u8* src, int slen, u8* dst, int dcap) {
    br b = { src, slen, 0, 0, 0 };
    int wbits;
    if (!rbit(&b)) wbits = 16;
    else {
        int n = (int)rb(&b, 3);
        if (n) wbits = 17 + n;
        else { n = (int)rb(&b, 3); if (n == 1) return -1; wbits = n ? 8 + n : 17; }
    }
    long maxback = (1L << wbits) - 16;
    int pos = 0;
    int dist[4] = { 16, 15, 11, 4 };
    int di = 0;
    int nb = pool_suspend();
    short* store = nalloc(sizeof(short) * (size_t)(3 * 600 + 256 * 256 + 704 * 256 + 640 * 256));
    huff* lh = nalloc(sizeof(huff) * 256);
    huff* ih = nalloc(sizeof(huff) * 256);
    huff* dh = nalloc(sizeof(huff) * 256);
    u8* cmapl = nalloc(64 * 256);
    u8* cmapd = nalloc(4 * 256);
    u8 cmodes[256];
    int ret = -1;
    for (;;) {
        int last = rbit(&b);
        if (last && rbit(&b)) { ret = pos; break; }
        int mn = (int)rb(&b, 2);
        int mlen = 0;
        if (mn == 3) {
            if (rbit(&b)) break;
            int skb = (int)rb(&b, 2);
            int skl = 0;
            for (int i = 0; i < skb; i++) skl |= (int)rb(&b, 8) << (8 * i);
            if (skb) skl++;
            ralign(&b);
            b.pos += skl;
            if (b.pos > b.len) break;
            if (last) { ret = pos; break; }
            continue;
        }
        int nib = mn + 4;
        for (int i = 0; i < nib; i++) mlen |= (int)rb(&b, 4) << (4 * i);
        mlen++;
        if (!last && rbit(&b)) {
            ralign(&b);
            if (b.pos + mlen > b.len || pos + mlen > dcap) break;
            memcpy(dst + pos, b.p + b.pos, (size_t)mlen);
            b.pos += mlen;
            pos += mlen;
            continue;
        }
        if (b.err) break;
        bcat L, I, D;
        if (read_bcat(&b, &L, store) < 0 || read_bcat(&b, &I, store + 600) < 0 || read_bcat(&b, &D, store + 1200) < 0) break;
        int npost = (int)rb(&b, 2);
        int ndirect = (int)rb(&b, 4) << npost;
        for (int i = 0; i < L.n; i++) cmodes[i] = (u8)rb(&b, 2);
        int ntl = vlu8(&b) + 1;
        if (ntl >= 2) { if (read_cmap(&b, 64 * L.n, ntl, cmapl) < 0) break; } else memset(cmapl, 0, (size_t)(64 * L.n));
        int ntd = vlu8(&b) + 1;
        if (ntd >= 2) { if (read_cmap(&b, 4 * D.n, ntd, cmapd) < 0) break; } else memset(cmapd, 0, (size_t)(4 * D.n));
        short* sp = store + 1800;
        int bad = 0;
        for (int i = 0; i < ntl && !bad; i++) { if (read_huff(&b, 256, &lh[i], sp) < 0) bad = 1; sp += 256; }
        for (int i = 0; i < I.n && !bad; i++) { if (read_huff(&b, 704, &ih[i], sp) < 0) bad = 1; sp += 704; }
        int dsize = 16 + ndirect + (48 << npost);
        for (int i = 0; i < ntd && !bad; i++) { if (read_huff(&b, dsize, &dh[i], sp) < 0) bad = 1; sp += dsize; }
        if (bad || b.err) break;
        int end = pos + mlen;
        if (end > dcap) break;
        while (pos < end && !b.err) {
            if (I.count == 0) switch_bcat(&b, &I);
            I.count--;
            int cmd = hdec(&b, &ih[I.cur]);
            int cell = cmd >> 6;
            int implicit0 = cmd < 128;
            int ic = kCellIns[cell] + ((cmd >> 3) & 7), cc = kCellCopy[cell] + (cmd & 7);
            int ins = (int)kInsBase[ic] + (int)rb(&b, kInsBits[ic]);
            int cpl = (int)kCopyBase[cc] + (int)rb(&b, kCopyBits[cc]);
            for (int k = 0; k < ins; k++) {
                if (pos >= end) { b.err = 1; break; }
                if (L.count == 0) switch_bcat(&b, &L);
                L.count--;
                int p1 = pos > 0 ? dst[pos - 1] : 0, p2 = pos > 1 ? dst[pos - 2] : 0;
                int mode = cmodes[L.cur];
                int ctx = brc_lut[mode * 512 + p1] | brc_lut[mode * 512 + 256 + p2];
                int tree = cmapl[L.cur * 64 + ctx];
                dst[pos++] = (u8)hdec(&b, &lh[tree]);
            }
            if (b.err || pos >= end) break;
            int distance, push = 0;
            if (implicit0) distance = dist[(di - 1) & 3];
            else {
                if (D.count == 0) switch_bcat(&b, &D);
                D.count--;
                int dctx = cpl > 4 ? 3 : cpl - 2;
                int tree = cmapd[D.cur * 4 + dctx];
                int ds = hdec(&b, &dh[tree]);
                if (ds < 16) {
                    static const int idx[16] = { 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1 };
                    static const int off[16] = { 0, 0, 0, 0, -1, 1, -2, 2, -3, 3, -1, 1, -2, 2, -3, 3 };
                    distance = dist[(di - 1 - idx[ds]) & 3] + off[ds];
                    if (distance <= 0) { b.err = 1; break; }
                    push = ds != 0;
                } else if (ds < 16 + ndirect) {
                    distance = ds - 15;
                    push = 1;
                } else {
                    int x = ds - ndirect - 16;
                    int nbits = 1 + (x >> (npost + 1));
                    int hc = x >> npost, lc2 = x & ((1 << npost) - 1);
                    long offs = ((2L + (hc & 1)) << nbits) - 4;
                    distance = (int)(((offs + (long)rb(&b, nbits)) << npost) + lc2 + ndirect + 1);
                    push = 1;
                }
            }
            long maxd = pos < maxback ? pos : maxback;
            if (distance > maxd) {
                long wid = distance - maxd - 1;
                if (cpl < 4 || cpl > 24 || !brd_bits[cpl]) { b.err = 1; break; }
                int nbits = brd_bits[cpl];
                int widx = (int)(wid & ((1 << nbits) - 1));
                int tidx = (int)(wid >> nbits);
                int w = dict_word(dst + pos, end - pos, cpl, widx, tidx);
                if (w < 0) { b.err = 1; break; }
                pos += w;
            } else {
                if (push) { dist[di & 3] = distance; di++; }
                if (pos + cpl > end) { b.err = 1; break; }
                for (int k = 0; k < cpl; k++, pos++) dst[pos] = dst[pos - distance];
            }
        }
        if (b.err || pos != end) break;
        if (last) { ret = pos; break; }
    }
    pool_resume(nb);
    return ret;
}
