#include "include/nvx/base.h"
#include "include/nvx/mem/heap.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/core/proc/jobs.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/core/media.h"

struct mstream { u8* d; u64 len, cap, base, initlen, consumed; int complete; };
struct mslot {
    int used, id, pid, gen;
    struct mstream s[2];
    u32* frame; int fw, fh; u64 fseq;
    struct media_status st;
    struct { int cmd; i64 a, b; } q[32];
    int qh, qt;
    char url[4096];
    int split, ww, wh;
    i64 durhint;
    u64 started;
};
static struct mslot g_ms[MEDIA_MAX];
static int g_mgen;

int jc_create(const char* args, int ppid, int pgid);
int jc_kill(int pid, int sig, int self);
int jc_newvf(int dev, int idx, int flags);
void vf_unref(int vi);
int jc_uok(u64 p, u64 n);
extern volatile u64 g_ticks;

static struct mslot* slot(int id) { if (id < 0 || id >= MEDIA_MAX || !g_ms[id].used) return 0; return &g_ms[id]; }

int media_new(void) {
    for (int i = 0; i < MEDIA_MAX; i++) if (!g_ms[i].used) {
        struct mslot* m = &g_ms[i];
        u8* raw = (u8*)m;
        for (u32 k = 0; k < sizeof(*m); k++) raw[k] = 0;
        m->used = 1;
        m->id = i;
        m->gen = ++g_mgen;
        m->pid = -1;
        return i;
    }
    return -1;
}

static void put_cmd(struct mslot* m, int cmd, i64 a, i64 b) {
    int n = (m->qt + 1) % 32;
    if (n == m->qh) m->qh = (m->qh + 1) % 32;
    m->q[m->qt].cmd = cmd;
    m->q[m->qt].a = a;
    m->q[m->qt].b = b;
    m->qt = n;
}

static void kill_proc(struct mslot* m) {
    if (m->pid > 0) jc_kill(m->pid, 9, 0);
    m->pid = -1;
}

static int spawn(struct mslot* m, i64 start_ms, int paused) {
    char a[200], *p = a;
    p = scopy(p, "play --embed ");
    p = udec(p, (u32)m->id);
    p = scopy(p, " --gen ");
    p = udec(p, (u32)m->gen);
    if (m->split) p = scopy(p, " --split");
    if (start_ms > 0) { p = scopy(p, " --start "); p = udec(p, (u32)start_ms); }
    if (paused) p = scopy(p, " --paused");
    if (m->ww > 0 && m->wh > 0) { p = scopy(p, " --size "); p = udec(p, (u32)m->ww); *p++ = 'x'; p = udec(p, (u32)m->wh); }
    if (m->url[0]) p = scopy(p, " --url");
    *p = 0;
    int ji = jc_create(a, 0, 0);
    if (ji < 0) { klog("media: cannot start the decoder"); return -1; }
    struct jproc* jp = &g_jp[ji];
    int nul = jc_newvf(1, -1, 0);
    int lg = jc_newvf(13, -1, 1);
    if (nul >= 0) { vf_unref(jp->fd[0]); jp->fd[0] = nul; }
    if (lg >= 0) { vf_unref(jp->fd[1]); jp->fd[1] = lg; vf_unref(jp->fd[2]); jp->fd[2] = lg; g_vf[lg].ref++; }
    m->pid = jp->pid;
    m->started = g_ticks;
    m->st.flags &= ~(u32)(MEDIA_ST_ENDED | MEDIA_ST_ERROR);
    return 0;
}

int media_start(int id, const char* url, int split, int w, int h, i64 start_ms, int paused) {
    struct mslot* m = slot(id);
    if (!m) return -1;
    m->split = split;
    m->ww = w;
    m->wh = h;
    int i = 0;
    if (url) for (; url[i] && i < (int)sizeof(m->url) - 1; i++) m->url[i] = url[i];
    m->url[i] = 0;
    m->qh = m->qt = 0;
    return spawn(m, start_ms, paused);
}

int media_alive(int id) {
    struct mslot* m = slot(id);
    if (!m || m->pid <= 0) return 0;
    int j = jc_idx(m->pid);
    return j >= 0 && g_jp[j].state != S_ZOMB && g_jp[j].state != S_FREE;
}

static int is_init(const u8* d, int len) {
    if (len >= 8 && d[4] == 'f' && d[5] == 't' && d[6] == 'y' && d[7] == 'p') return 1;
    if (len >= 4 && d[0] == 0x1A && d[1] == 0x45 && d[2] == 0xDF && d[3] == 0xA3) return 1;
    return 0;
}

int media_append(int id, int stream, const u8* d, int len) {
    struct mslot* m = slot(id);
    if (!m || stream < 0 || stream > 1 || len <= 0) return -1;
    struct mstream* s = &m->s[stream];
    u64 phys = s->len - s->base + s->initlen;
    if (s->base == 0) phys = s->len;
    if (phys + (u64)len > s->cap) {
        u64 nc = s->cap ? s->cap * 2 : 1u << 20;
        while (nc < phys + (u64)len) nc *= 2;
        u8* nd = (u8*)kmalloc((unsigned long)nc);
        if (!nd) return -1;
        if (s->d) { __builtin_memcpy(nd, s->d, (unsigned long)phys); kfree(s->d); }
        s->d = nd;
        s->cap = nc;
    }
    __builtin_memcpy(s->d + phys, d, (unsigned long)len);
    if (s->len == 0 && is_init(d, len)) s->initlen = (u64)len;
    s->len += (u64)len;
    s->complete = 0;
    return 0;
}

void media_end_stream(int id, int stream) {
    struct mslot* m = slot(id);
    if (m && stream >= 0 && stream < 2) m->s[stream].complete = 1;
}

u64 media_stream_len(int id, int stream) {
    struct mslot* m = slot(id);
    if (!m || stream < 0 || stream > 1) return 0;
    return m->s[stream].len;
}

static void compact(struct mstream* s) {
    if (!s->initlen) return;
    u64 keep_from = s->consumed > (8u << 20) ? s->consumed - (4u << 20) : 0;
    if (keep_from <= s->base + (16u << 20) || keep_from <= s->initlen) return;
    u64 oldphys = s->base ? s->len - s->base + s->initlen : s->len;
    u64 dropfrom = s->base ? s->base : s->initlen;
    if (keep_from <= dropfrom) return;
    u64 srcoff = s->base ? keep_from - s->base + s->initlen : keep_from;
    u64 n = oldphys - srcoff;
    for (u64 i = 0; i < n; i++) s->d[s->initlen + i] = s->d[srcoff + i];
    s->base = keep_from;
}

static long stream_read(struct mstream* s, u64 off, u8* dst, u64 len) {
    if (off >= s->len) return s->complete ? 0 : -11;
    u64 phys;
    if (s->base && off >= s->initlen) {
        if (off < s->base) return -22;
        phys = off - s->base + s->initlen;
    } else phys = off;
    u64 avail = s->len - off;
    if (len > avail) len = avail;
    if (s->base && off < s->initlen && off + len > s->initlen) len = s->initlen - off;
    __builtin_memcpy(dst, s->d + phys, (unsigned long)len);
    if (off + len > s->consumed) s->consumed = off + len;
    return (long)len;
}

void media_cmd(int id, int cmd, i64 a, i64 b) {
    struct mslot* m = slot(id);
    if (!m) return;
    if (cmd == MEDIA_CMD_DURATION) { m->durhint = a; return; }
    if (cmd == MEDIA_CMD_SIZE) { m->ww = (int)a; m->wh = (int)b; }
    if (cmd == MEDIA_CMD_PAUSE) { m->st.flags |= MEDIA_ST_PAUSED; m->st.flags &= ~(u32)MEDIA_ST_PLAYING; }
    if (cmd == MEDIA_CMD_PLAY) { m->st.flags &= ~(u32)MEDIA_ST_PAUSED; }
    if (cmd == MEDIA_CMD_SEEK) { m->st.cur_ms = a; m->st.flags &= ~(u32)MEDIA_ST_ENDED; }
    put_cmd(m, cmd, a, b);
}

int media_get_status(int id, struct media_status* out) {
    struct mslot* m = slot(id);
    if (!m) return -1;
    *out = m->st;
    if (out->dur_ms <= 0 && m->durhint > 0) out->dur_ms = m->durhint;
    if (m->pid > 0 && !media_alive(id) && !(m->st.flags & MEDIA_ST_ENDED)) {
        if (m->st.cur_ms > 0 && m->st.dur_ms > 0 && m->st.cur_ms >= m->st.dur_ms - 600) out->flags |= MEDIA_ST_ENDED;
        else out->flags |= MEDIA_ST_ERROR;
    }
    return 0;
}

const u32* media_frame(int id, int* w, int* h, u64* seq) {
    struct mslot* m = slot(id);
    if (!m || !m->frame) return 0;
    *w = m->fw;
    *h = m->fh;
    if (seq) *seq = m->fseq;
    return m->frame;
}

int media_restart(int id, i64 start_ms, int keep_data) {
    struct mslot* m = slot(id);
    if (!m) return -1;
    kill_proc(m);
    for (int k = 0; k < 2; k++) {
        struct mstream* s = &m->s[k];
        s->consumed = 0;
        if (!keep_data) {
            if (s->initlen) { s->len = s->initlen; s->base = 0; }
            else { s->len = 0; s->base = 0; }
            s->complete = 0;
        }
    }
    m->gen = ++g_mgen;
    m->qh = m->qt = 0;
    m->st.cur_ms = start_ms;
    m->st.flags &= ~(u32)(MEDIA_ST_ENDED | MEDIA_ST_ERROR);
    return spawn(m, start_ms, (m->st.flags & MEDIA_ST_PAUSED) != 0);
}

void media_free(int id) {
    struct mslot* m = slot(id);
    if (!m) return;
    put_cmd(m, MEDIA_CMD_QUIT, 0, 0);
    kill_proc(m);
    for (int k = 0; k < 2; k++) if (m->s[k].d) kfree(m->s[k].d);
    if (m->frame) kfree(m->frame);
    m->used = 0;
}

void media_tick(void) {
    for (int i = 0; i < MEDIA_MAX; i++) if (g_ms[i].used) { compact(&g_ms[i].s[0]); compact(&g_ms[i].s[1]); }
}

#define MIO_BIND 0x4D01
#define MIO_READAT 0x4D02
#define MIO_INFO 0x4D03
#define MIO_STATUS 0x4D04
#define MIO_GETCMD 0x4D05
#define MIO_FRAME 0x4D06
#define MIO_URL 0x4D07

struct mio_readat { u64 off, buf; u32 len, stream; };
struct mio_info { u64 len; u32 complete, split; i64 total; };
struct mio_cmd { int cmd, pad; i64 a, b; };
struct mio_frame { u64 buf; u32 w, h; i64 pts; };

static struct mslot* bound(int vfidx, int* stream) {
    int b = g_vf[vfidx].fs_idx;
    if (b < 0) return 0;
    int id = (b >> 16) & 0xFF, gen = b & 0xFFFF;
    struct mslot* m = slot(id);
    if (!m || (m->gen & 0xFFFF) != gen) return 0;
    if (stream) *stream = (b >> 24) & 1;
    return m;
}

long media_ioctl(int vfidx, u64 req, u64 arg) {
    if (req == MIO_BIND) {
        u32 id = (u32)(arg & 0xFF), gen = (u32)((arg >> 8) & 0xFFFF), st = (u32)((arg >> 24) & 1);
        struct mslot* m = slot((int)id);
        if (!m || (m->gen & 0xFFFF) != gen) return -2;
        g_vf[vfidx].fs_idx = (int)((st << 24) | (id << 16) | gen);
        return 0;
    }
    int stream = 0;
    struct mslot* m = bound(vfidx, &stream);
    if (!m) return -9;
    if (req == MIO_READAT) {
        struct mio_readat r;
        if (!jc_uok(arg, sizeof r)) return -14;
        __builtin_memcpy(&r, (void*)arg, sizeof r);
        if (r.stream > 1 || !jc_uok(r.buf, r.len)) return -14;
        return stream_read(&m->s[r.stream], r.off, (u8*)r.buf, r.len);
    }
    if (req == MIO_INFO) {
        struct mio_info in;
        if (!jc_uok(arg, sizeof in)) return -14;
        int sidx = (int)(((struct mio_info*)arg)->split & 1);
        in.len = m->s[sidx].len;
        in.complete = (u32)m->s[sidx].complete;
        in.split = (u32)m->split;
        in.total = m->s[sidx].complete ? (i64)m->s[sidx].len : -1;
        __builtin_memcpy((void*)arg, &in, sizeof in);
        return 0;
    }
    if (req == MIO_STATUS) {
        if (!jc_uok(arg, sizeof(struct media_status))) return -14;
        u32 keep = m->st.flags & MEDIA_ST_PAUSED;
        __builtin_memcpy(&m->st, (void*)arg, sizeof(struct media_status));
        if (keep && !(m->st.flags & MEDIA_ST_PLAYING)) m->st.flags |= MEDIA_ST_PAUSED;
        return 0;
    }
    if (req == MIO_GETCMD) {
        if (!jc_uok(arg, sizeof(struct mio_cmd))) return -14;
        if (m->qh == m->qt) return 0;
        struct mio_cmd c;
        c.cmd = m->q[m->qh].cmd;
        c.pad = 0;
        c.a = m->q[m->qh].a;
        c.b = m->q[m->qh].b;
        m->qh = (m->qh + 1) % 32;
        __builtin_memcpy((void*)arg, &c, sizeof c);
        return 1;
    }
    if (req == MIO_URL) {
        struct { u64 buf; u32 cap, pad; } u;
        if (!jc_uok(arg, sizeof u)) return -14;
        __builtin_memcpy(&u, (void*)arg, sizeof u);
        if (!u.cap || !jc_uok(u.buf, u.cap)) return -14;
        u32 n = 0;
        while (m->url[n] && n + 1 < u.cap) { ((char*)u.buf)[n] = m->url[n]; n++; }
        ((char*)u.buf)[n] = 0;
        return (long)n;
    }
    if (req == MIO_FRAME) {
        struct mio_frame f;
        if (!jc_uok(arg, sizeof f)) return -14;
        __builtin_memcpy(&f, (void*)arg, sizeof f);
        if (!f.w || !f.h || f.w > 4096 || f.h > 4096) return -22;
        u64 bytes = (u64)f.w * f.h * 4;
        if (!jc_uok(f.buf, bytes)) return -14;
        if (!m->frame || m->fw != (int)f.w || m->fh != (int)f.h) {
            if (m->frame) kfree(m->frame);
            m->frame = (u32*)kmalloc((unsigned long)bytes);
            if (!m->frame) return -12;
            m->fw = (int)f.w;
            m->fh = (int)f.h;
        }
        __builtin_memcpy(m->frame, (void*)f.buf, (unsigned long)bytes);
        m->fseq++;
        return 0;
    }
    return -22;
}

void media_vf_close(int vfidx) { (void)vfidx; }
