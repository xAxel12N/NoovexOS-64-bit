#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include "httpio.h"

#define MIO_BIND 0x4D01
#define MIO_READAT 0x4D02
#define MIO_INFO 0x4D03
#define MIO_STATUS 0x4D04
#define MIO_GETCMD 0x4D05
#define MIO_FRAME 0x4D06
#define MIO_URL 0x4D07

#define CMD_PLAY 1
#define CMD_PAUSE 2
#define CMD_SEEK 3
#define CMD_VOLUME 4
#define CMD_SIZE 5
#define CMD_QUIT 6
#define CMD_LOOP 7

#define ST_PLAYING 1
#define ST_PAUSED 2
#define ST_ENDED 4
#define ST_ERROR 8
#define ST_VIDEO 16
#define ST_AUDIO 32
#define ST_WAITING 64
#define ST_READY 128

struct mio_readat { uint64_t off, buf; uint32_t len, stream; };
struct mio_info { uint64_t len; uint32_t complete, split; int64_t total; };
struct mio_cmd { int32_t cmd, pad; int64_t a, b; };
struct mio_frame { uint64_t buf; uint32_t w, h; int64_t pts; };
struct mio_status { int64_t cur_ms, dur_ms; uint32_t flags, vw, vh, buffered_pct; };
struct mio_url { uint64_t buf; uint32_t cap, pad; };

typedef struct { uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale; uint32_t rest[32]; } fb_var;
typedef struct { char id[16]; unsigned long smem_start; uint32_t smem_len, type, type_aux, visual; uint16_t xpanstep, ypanstep, ywrapstep; uint32_t line_length; unsigned long mmio_start; uint32_t mmio_len, accel; uint16_t caps, res[2]; } fb_fix;

typedef struct pnode { AVPacket* pkt; struct pnode* next; } pnode;
typedef struct { pnode *head, *tail; int n; int64_t bytes; pthread_mutex_t m; pthread_cond_t c; } pqueue;

typedef struct {
    int kind;
    AVFormatContext* fmt;
    AVIOContext* avio;
    hio* h;
    int stream;
    int64_t off;
    int vs, as;
    double last;
    int eof;
} source;

static source S[2];
static int nsrc;
static source *vsrc, *asrc;
static AVCodecContext *vdec, *adec;
static int vs = -1, as = -1;
static pqueue aq;
static volatile int quit, paused, aflush, stalled, ended, looping;
static volatile int vol = 1000;
static int dsp = -1, dsp_rate = 48000;
static SwrContext* swr;
static double t0, pause_at, stall_at;
static double seek_req = -1, skip_until = -1;
static int anchor = 1;
static int fbfd = -1, scr_w, scr_h, scr_stride;
static uint8_t* screen;
static struct SwsContext* sws;
static int dst_x, dst_y, dst_w, dst_h;
static struct termios tio_old;
static int have_tio;
static int embed, mfd = -1, emb_w, emb_h, out_w, out_h, shown_any;
static uint8_t* ebuf;
static size_t ebuf_cap;
static double dur, last_status, last_pts;
static int64_t emb_pts;

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }
static double clock_now(void) { if (paused) return pause_at; if (stalled) return stall_at; return now_s() - t0; }
static void clock_set(double t) { if (paused) pause_at = t; else if (stalled) stall_at = t; else t0 = now_s() - t; }

static void pq_init(pqueue* q) { memset(q, 0, sizeof(*q)); pthread_mutex_init(&q->m, 0); pthread_cond_init(&q->c, 0); }
static void pq_put(pqueue* q, AVPacket* p) {
    pnode* n = malloc(sizeof(pnode));
    n->pkt = p;
    n->next = 0;
    pthread_mutex_lock(&q->m);
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    q->n++;
    q->bytes += p->size;
    pthread_cond_signal(&q->c);
    pthread_mutex_unlock(&q->m);
}
static AVPacket* pq_get(pqueue* q) {
    pthread_mutex_lock(&q->m);
    if (!q->head && !quit) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 50000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&q->c, &q->m, &ts);
    }
    pnode* n = q->head;
    AVPacket* p = 0;
    if (n) { q->head = n->next; if (!q->head) q->tail = 0; q->n--; q->bytes -= n->pkt->size; p = n->pkt; free(n); }
    pthread_mutex_unlock(&q->m);
    return p;
}
static void pq_flush(pqueue* q) {
    pthread_mutex_lock(&q->m);
    pnode* n = q->head;
    while (n) { pnode* x = n->next; av_packet_free(&n->pkt); free(n); n = x; }
    q->head = q->tail = 0;
    q->n = 0;
    q->bytes = 0;
    pthread_mutex_unlock(&q->m);
}

static void send_status(int force) {
    if (!embed || mfd < 0) return;
    double t = now_s();
    if (!force && t - last_status < 0.1) return;
    last_status = t;
    struct mio_status st;
    memset(&st, 0, sizeof st);
    double c = ended ? (dur > 0 ? dur : last_pts) : clock_now();
    if (c < 0) c = 0;
    if (dur > 0 && c > dur) c = dur;
    st.cur_ms = (int64_t)(c * 1000);
    st.dur_ms = dur > 0 ? (int64_t)(dur * 1000) : -1;
    st.flags = ST_READY;
    if (ended) st.flags |= ST_ENDED;
    else if (paused) st.flags |= ST_PAUSED;
    else st.flags |= ST_PLAYING;
    if (stalled) st.flags |= ST_WAITING;
    if (vs >= 0) st.flags |= ST_VIDEO;
    if (as >= 0) st.flags |= ST_AUDIO;
    st.vw = vdec ? (uint32_t)vdec->width : 0;
    st.vh = vdec ? (uint32_t)vdec->height : 0;
    if (nsrc && S[0].kind == 1 && hio_size(S[0].h) > 0) {
        long long p = hio_tell(S[0].h), z = hio_size(S[0].h);
        st.buffered_pct = (uint32_t)(p * 100 / z);
    } else if (nsrc && S[0].kind == 2) {
        struct mio_info in;
        memset(&in, 0, sizeof in);
        in.split = (uint32_t)S[0].stream;
        if (ioctl(mfd, MIO_INFO, &in) == 0 && in.len) st.buffered_pct = (uint32_t)(in.complete ? 100 : (S[0].off * 100 / (int64_t)in.len));
    } else st.buffered_pct = 100;
    ioctl(mfd, MIO_STATUS, &st);
}

static void send_error(void) {
    if (!embed || mfd < 0) return;
    struct mio_status st;
    memset(&st, 0, sizeof st);
    st.flags = ST_ERROR;
    st.dur_ms = -1;
    ioctl(mfd, MIO_STATUS, &st);
}

static void set_paused(int p) {
    if (p && !paused) { pause_at = clock_now(); paused = 1; }
    else if (!p && paused) { if (stalled) stall_at = pause_at; else t0 = now_s() - pause_at; paused = 0; }
}

static int key_poll(void) {
    if (embed) return 0;
    unsigned char c[8];
    int n = (int)read(0, c, sizeof(c));
    if (n <= 0) return 0;
    if (c[0] == 27 && n >= 3 && c[1] == '[') { if (c[2] == 'C') return 'R'; if (c[2] == 'D') return 'L'; if (c[2] == 'A') return 'U'; if (c[2] == 'B') return 'D'; return 0; }
    return c[0];
}

static int control(void) {
    if (embed) {
        struct mio_cmd c;
        int guard = 0;
        while (guard++ < 32 && ioctl(mfd, MIO_GETCMD, &c) == 1) {
            if (c.cmd == CMD_QUIT) quit = 1;
            else if (c.cmd == CMD_PLAY) { if (ended) { seek_req = 0; ended = 0; } set_paused(0); }
            else if (c.cmd == CMD_PAUSE) set_paused(1);
            else if (c.cmd == CMD_SEEK) { seek_req = c.a / 1000.0; ended = 0; }
            else if (c.cmd == CMD_VOLUME) { int v = (int)c.a; vol = v < 0 ? 0 : v > 1000 ? 1000 : v; }
            else if (c.cmd == CMD_SIZE) { emb_w = (int)c.a; emb_h = (int)c.b; }
            else if (c.cmd == CMD_LOOP) looping = c.a != 0;
            send_status(1);
        }
        send_status(0);
    } else {
        int k = key_poll();
        if (k == 'q' || k == 'Q' || k == 27) quit = 1;
        if (k == ' ' || k == 'p') set_paused(!paused);
        if (k == 'R') seek_req = clock_now() + 10;
        if (k == 'L') seek_req = clock_now() - 10;
        if (k == 'U') seek_req = clock_now() + 60;
        if (k == 'D') seek_req = clock_now() - 60;
    }
    return quit || seek_req >= 0;
}

static void stall_begin(void) { if (!stalled && shown_any) { stall_at = clock_now(); stalled = 1; send_status(1); } }
static void stall_end(void) { if (stalled) { stalled = 0; if (!paused) t0 = now_s() - stall_at; else pause_at = stall_at; send_status(1); } }

static int ks_read(void* o, uint8_t* b, int n) {
    source* s = o;
    double w0 = now_s();
    for (;;) {
        struct mio_readat r = { (uint64_t)s->off, (uint64_t)(uintptr_t)b, (uint32_t)n, (uint32_t)s->stream };
        int k = ioctl(mfd, MIO_READAT, &r);
        if (k > 0) { s->off += k; stall_end(); return k; }
        if (k == 0) return AVERROR_EOF;
        if (errno != EAGAIN) return AVERROR(EIO);
        if (control()) return AVERROR_EXIT;
        if (now_s() - w0 > 0.15) stall_begin();
        usleep(8000);
    }
}
static int64_t ks_seek(void* o, int64_t off, int wh) {
    source* s = o;
    struct mio_info in;
    memset(&in, 0, sizeof in);
    in.split = (uint32_t)s->stream;
    if (wh & AVSEEK_SIZE) { if (ioctl(mfd, MIO_INFO, &in) == 0 && in.complete) return (int64_t)in.len; return -1; }
    wh &= ~AVSEEK_FORCE;
    if (wh == SEEK_CUR) off += s->off;
    else if (wh == SEEK_END) { if (ioctl(mfd, MIO_INFO, &in) || !in.complete) return -1; off += (int64_t)in.len; }
    if (off < 0) return -1;
    s->off = off;
    return off;
}
static int h_read(void* o, uint8_t* b, int n) { source* s = o; int r = hio_read(s->h, b, n); return r <= 0 ? AVERROR_EOF : r; }
static int64_t h_seek(void* o, int64_t off, int wh) {
    source* s = o;
    if (wh & AVSEEK_SIZE) return hio_size(s->h);
    wh &= ~AVSEEK_FORCE;
    if (wh == SEEK_CUR) off += hio_tell(s->h);
    else if (wh == SEEK_END) { if (hio_size(s->h) < 0) return -1; off += hio_size(s->h); }
    long long r = hio_seek(s->h, off);
    return r < 0 ? -1 : r;
}
static int interrupt_cb(void* o) { (void)o; return quit; }

static int is_url(const char* u) { return !strncmp(u, "http://", 7) || !strncmp(u, "https://", 8); }

static int open_source(source* s, const char* path, int kind, int stream) {
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->stream = stream;
    s->vs = s->as = -1;
    s->fmt = avformat_alloc_context();
    s->fmt->interrupt_callback.callback = interrupt_cb;
    if (kind == 1) {
        s->h = hio_open(path);
        if (!s->h) { fprintf(stderr, "play: cannot fetch %s\n", path); return -1; }
        uint8_t* b = av_malloc(65536);
        s->avio = avio_alloc_context(b, 65536, 0, s, h_read, 0, h_seek);
        s->avio->seekable = hio_seekable(s->h) && hio_size(s->h) > 0 ? AVIO_SEEKABLE_NORMAL : 0;
        s->fmt->pb = s->avio;
        s->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    } else if (kind == 2) {
        uint8_t* b = av_malloc(65536);
        s->avio = avio_alloc_context(b, 65536, 0, s, ks_read, 0, ks_seek);
        struct mio_info in;
        memset(&in, 0, sizeof in);
        in.split = (uint32_t)stream;
        s->avio->seekable = ioctl(mfd, MIO_INFO, &in) == 0 && in.complete && !in.split ? AVIO_SEEKABLE_NORMAL : 0;
        s->fmt->pb = s->avio;
        s->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        s->fmt->probesize = 1 << 20;
        s->fmt->max_analyze_duration = 2 * AV_TIME_BASE;
    }
    if (avformat_open_input(&s->fmt, kind == 0 ? path : 0, 0, 0) < 0) { fprintf(stderr, "play: cannot open %s\n", kind == 2 ? "stream" : path); return -1; }
    if (avformat_find_stream_info(s->fmt, 0) < 0) { fprintf(stderr, "play: no stream info\n"); return -1; }
    s->vs = av_find_best_stream(s->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, 0, 0);
    s->as = av_find_best_stream(s->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, 0, 0);
    if (s->vs < 0) s->vs = -1;
    if (s->as < 0) s->as = -1;
    for (unsigned i = 0; i < s->fmt->nb_streams; i++) if ((int)i != s->vs && (int)i != s->as) s->fmt->streams[i]->discard = AVDISCARD_ALL;
    s->last = -1;
    return 0;
}

static int open_dec(AVStream* st, AVCodecContext** out, int threads) {
    const AVCodec* c = avcodec_find_decoder(st->codecpar->codec_id);
    if (!c) return -1;
    AVCodecContext* ctx = avcodec_alloc_context3(c);
    avcodec_parameters_to_context(ctx, st->codecpar);
    ctx->pkt_timebase = st->time_base;
    if (threads) { ctx->thread_count = 0; ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE; }
    if (avcodec_open2(ctx, c, 0) < 0) { avcodec_free_context(&ctx); return -1; }
    *out = ctx;
    return 0;
}

static void* audio_thread(void* arg) {
    (void)arg;
    AVFrame* f = av_frame_alloc();
    uint8_t* out = 0;
    int outcap = 0;
    AVRational tb = asrc->fmt->streams[as]->time_base;
    while (!quit) {
        if (aflush) { avcodec_flush_buffers(adec); aflush = 0; }
        if (paused || ended) { usleep(20000); continue; }
        AVPacket* p = pq_get(&aq);
        if (!p) continue;
        if (avcodec_send_packet(adec, p) < 0) { av_packet_free(&p); continue; }
        av_packet_free(&p);
        while (!quit && !aflush && avcodec_receive_frame(adec, f) == 0) {
            int maxo = swr_get_out_samples(swr, f->nb_samples) + 256;
            if (maxo * 4 > outcap) { outcap = maxo * 4; out = realloc(out, (size_t)outcap); }
            uint8_t* o[1] = { out };
            int got = swr_convert(swr, o, maxo, (const uint8_t**)f->extended_data, f->nb_samples);
            if (got <= 0) continue;
            double pts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp * av_q2d(tb) : -1;
            if (vs < 0 && pts >= 0) {
                if (skip_until >= 0 && pts < skip_until - 0.05) continue;
                skip_until = -1;
                if (anchor) { clock_set(pts); anchor = 0; }
                shown_any = 1;
            }
            double target = pts - clock_now();
            while (pts >= 0 && target > 0.25 && !quit && !aflush) { usleep(20000); target = pts - clock_now(); }
            if (pts >= 0 && target < -0.3) continue;
            int v = vol;
            if (v < 1000) { int16_t* sm = (int16_t*)out; for (int i = 0; i < got * 2; i++) sm[i] = (int16_t)(sm[i] * v / 1000); }
            int len = got * 4, off = 0;
            while (off < len && !quit && !aflush) {
                int w = (int)write(dsp, out + off, (size_t)(len - off));
                if (w <= 0) { usleep(2000); continue; }
                off += w;
            }
            if (pts >= 0) last_pts = pts;
        }
    }
    av_frame_free(&f);
    free(out);
    return 0;
}

static void restore_tty(void) { if (have_tio) tcsetattr(0, TCSANOW, &tio_old); }

static void fit(int bw, int bh, int* ow, int* oh) {
    int vw = vdec->width, vh = vdec->height;
    AVRational sar = vdec->sample_aspect_ratio;
    double ar = (double)vw / vh * (sar.num && sar.den ? av_q2d(sar) : 1.0);
    int w = bw, h = (int)(bw / ar);
    if (h > bh) { h = bh; w = (int)(bh * ar); }
    *ow = w < 2 ? 2 : w & ~1;
    *oh = h < 2 ? 2 : h & ~1;
}

static void fb_setup(void) {
    fbfd = open("/dev/fb0", O_RDWR);
    if (fbfd < 0) return;
    fb_var v;
    fb_fix x;
    memset(&v, 0, sizeof(v));
    memset(&x, 0, sizeof(x));
    if (ioctl(fbfd, 0x4600, &v) < 0 || !v.xres) { v.xres = 1280; v.yres = 800; }
    scr_w = (int)v.xres;
    scr_h = (int)v.yres;
    scr_stride = ioctl(fbfd, 0x4602, &x) == 0 && x.line_length ? (int)x.line_length : scr_w * 4;
    screen = calloc((size_t)scr_stride * (size_t)scr_h, 1);
    fit(scr_w, scr_h, &dst_w, &dst_h);
    dst_x = (scr_w - dst_w) / 2;
    dst_y = (scr_h - dst_h) / 2;
    lseek(fbfd, 0, SEEK_SET);
    size_t len = (size_t)scr_stride * scr_h, off = 0;
    while (off < len) { ssize_t w = write(fbfd, screen + off, len - off); if (w <= 0) break; off += (size_t)w; }
}

static void draw_bar(double pos) {
    if (fbfd < 0 || dur <= 0) return;
    int bh = 6, y = scr_h - 24;
    int bw = scr_w - 80;
    int fill = (int)(bw * (pos / dur));
    if (fill < 0) fill = 0;
    if (fill > bw) fill = bw;
    for (int yy = y; yy < y + bh; yy++) {
        uint32_t* row = (uint32_t*)(screen + (size_t)yy * scr_stride);
        for (int xx = 40; xx < 40 + bw; xx++) row[xx] = xx - 40 < fill ? 0xFF3C8CF0u : 0xFF505050u;
    }
    lseek(fbfd, (off_t)y * scr_stride, SEEK_SET);
    if (write(fbfd, screen + (size_t)y * scr_stride, (size_t)bh * scr_stride) < 0) {}
}

static void present(AVFrame* f, double pts) {
    if (embed) {
        int bw = emb_w > 0 ? emb_w : 854, bh = emb_h > 0 ? emb_h : 480;
        if (bw > 1920) bw = 1920;
        if (bh > 1080) bh = 1080;
        if (bw > f->width && bh > f->height) { bw = f->width; bh = f->height; }
        fit(bw, bh, &out_w, &out_h);
        size_t need = (size_t)out_w * out_h * 4;
        if (need > ebuf_cap) { free(ebuf); ebuf = malloc(need); ebuf_cap = need; }
        sws = sws_getCachedContext(sws, f->width, f->height, f->format, out_w, out_h, AV_PIX_FMT_BGRA, SWS_BILINEAR, 0, 0, 0);
        uint8_t* d[1] = { ebuf };
        int ls[1] = { out_w * 4 };
        sws_scale(sws, (const uint8_t* const*)f->data, f->linesize, 0, f->height, d, ls);
        struct mio_frame mf = { (uint64_t)(uintptr_t)ebuf, (uint32_t)out_w, (uint32_t)out_h, (int64_t)(pts * 1000) };
        ioctl(mfd, MIO_FRAME, &mf);
        emb_pts = mf.pts;
        return;
    }
    if (fbfd < 0) return;
    sws = sws_getCachedContext(sws, f->width, f->height, f->format, dst_w, dst_h, AV_PIX_FMT_BGRA, SWS_BILINEAR, 0, 0, 0);
    uint8_t* d[1] = { screen + (size_t)dst_y * scr_stride + (size_t)dst_x * 4 };
    int ls[1] = { scr_stride };
    sws_scale(sws, (const uint8_t* const*)f->data, f->linesize, 0, f->height, d, ls);
    off_t start = (off_t)dst_y * scr_stride;
    lseek(fbfd, start, SEEK_SET);
    size_t len = (size_t)dst_h * scr_stride, off = 0;
    while (off < len) { ssize_t w = write(fbfd, screen + start + off, len - off); if (w <= 0) break; off += (size_t)w; }
    static double last_bar = -10;
    if (pts - last_bar > 1 || pts < last_bar) { draw_bar(pts); last_bar = pts; }
}

static int shown, dropped;
static AVFrame* vf;

static void drain_frames(void) {
    AVRational tb = vsrc->fmt->streams[vs]->time_base;
    while (!quit && seek_req < 0 && avcodec_receive_frame(vdec, vf) == 0) {
        double pts = vf->best_effort_timestamp != AV_NOPTS_VALUE ? vf->best_effort_timestamp * av_q2d(tb) : clock_now();
        if (skip_until >= 0) {
            if (pts < skip_until - 0.03) continue;
            skip_until = -1;
        }
        if (anchor) {
            double c = clock_now();
            if (pts > c + 0.25 || pts < c - 1.0) clock_set(pts);
            anchor = 0;
        }
        if (paused && shown_any) {
            while (paused && !quit && seek_req < 0) { control(); usleep(15000); }
            if (quit || seek_req >= 0) return;
        }
        double delay = pts - clock_now();
        if (shown_any) {
            if (delay < -0.3 && vdec->skip_frame != AVDISCARD_NONREF) vdec->skip_frame = AVDISCARD_NONREF;
            else if (delay > -0.05 && vdec->skip_frame != AVDISCARD_DEFAULT) vdec->skip_frame = AVDISCARD_DEFAULT;
            if (delay < -0.08) { dropped++; if (dropped % 8) continue; }
            while (delay > 0.002 && !quit) {
                if (control()) break;
                if (paused) { while (paused && !quit && seek_req < 0) { control(); usleep(15000); } if (quit || seek_req >= 0) break; }
                delay = pts - clock_now();
                if (delay > 0.002) usleep((useconds_t)(delay > 0.02 ? 15000 : delay * 1e6));
            }
            if (quit || seek_req >= 0) return;
        }
        present(vf, pts);
        last_pts = pts;
        shown++;
        shown_any = 1;
        control();
    }
}

static void video_packet(AVPacket* p) {
    for (int guard = 0; guard < 64; guard++) {
        int r = avcodec_send_packet(vdec, p);
        drain_frames();
        if (r != AVERROR(EAGAIN) || quit || seek_req >= 0) break;
    }
}

static void do_seek(double target) {
    if (target < 0) target = 0;
    if (dur > 0 && target > dur) target = dur;
    int64_t ts = (int64_t)(target * AV_TIME_BASE);
    int ok = 0;
    for (int i = 0; i < nsrc; i++) {
        S[i].fmt->pb->error = 0;
        S[i].fmt->pb->eof_reached = 0;
        if (av_seek_frame(S[i].fmt, -1, ts, AVSEEK_FLAG_BACKWARD) >= 0) ok++;
        S[i].eof = 0;
        S[i].last = -1;
    }
    if (vdec) { avcodec_flush_buffers(vdec); vdec->skip_frame = AVDISCARD_DEFAULT; }
    pq_flush(&aq);
    if (adec) aflush = 1;
    ended = 0;
    if (ok) { clock_set(target); skip_until = target; }
    anchor = 1;
    send_status(1);
}

static void save_ppm(AVFrame* f, const char* path) {
    struct SwsContext* c = sws_getContext(f->width, f->height, f->format, f->width, f->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, 0, 0, 0);
    uint8_t* buf = malloc((size_t)f->width * f->height * 3);
    uint8_t* d[1] = { buf };
    int ls[1] = { f->width * 3 };
    sws_scale(c, (const uint8_t* const*)f->data, f->linesize, 0, f->height, d, ls);
    FILE* o = fopen(path, "wb");
    if (o) { fprintf(o, "P6 %d %d 255\n", f->width, f->height); fwrite(buf, 1, (size_t)f->width * f->height * 3, o); fclose(o); }
    free(buf);
    sws_freeContext(c);
}

static int run_bench(const char* file, double snap_at, const char* snap_out) {
    AVFormatContext* fmt = vsrc->fmt;
    AVPacket* p = av_packet_alloc();
    AVFrame* f = av_frame_alloc();
    int frames = 0, snapped = 0;
    double ts = now_s();
    while (av_read_frame(fmt, p) >= 0) {
        if (p->stream_index == vs && avcodec_send_packet(vdec, p) >= 0) {
            while (avcodec_receive_frame(vdec, f) == 0) {
                frames++;
                double pts = f->best_effort_timestamp * av_q2d(fmt->streams[vs]->time_base);
                if (snap_out && !snapped && pts >= snap_at) { save_ppm(f, snap_out); snapped = 1; printf("saved %s at %.2fs (%dx%d %s)\n", snap_out, pts, f->width, f->height, av_get_pix_fmt_name(f->format)); av_packet_unref(p); goto done; }
            }
        }
        av_packet_unref(p);
    }
    avcodec_send_packet(vdec, 0);
    while (avcodec_receive_frame(vdec, f) == 0) frames++;
done:;
    double el = now_s() - ts;
    printf("%s: %s %dx%d, decoded %d frames in %.2fs = %.1f fps (threads %d)\n", file, avcodec_get_name(vdec->codec_id), vdec->width, vdec->height, frames, el, el > 0 ? frames / el : 0, vdec->thread_count);
    av_frame_free(&f);
    av_packet_free(&p);
    return 0;
}

static source* pick_source(void) {
    source* a = asrc && asrc != vsrc && !asrc->eof ? asrc : 0;
    source* v = vsrc && !vsrc->eof ? vsrc : 0;
    if (!a) return v ? v : (asrc && !asrc->eof ? asrc : 0);
    if (!v) { if (a->last > clock_now() + 3 && aq.n > 8) return 0; return a; }
    if (a->last < v->last || a->last < clock_now() + 0.5) return a;
    return v;
}

static void usage(void) {
    fprintf(stderr, "usage: play [-b] [-s seconds out.ppm] file|url\n  keys: space pause, left/right seek 10s, up/down seek 60s, q quit\n");
}

int main(int argc, char** argv) {
    int bench = 0, split = 0, want_url = 0, start_paused = 0, id = -1, gen = 0;
    double snap_at = -1, start = 0;
    const char* snap_out = 0;
    int ai = 1;
    while (ai < argc && argv[ai][0] == '-') {
        const char* a = argv[ai];
        if (!strcmp(a, "-b")) bench = 1;
        else if (!strcmp(a, "-s") && ai + 2 < argc) { snap_at = atof(argv[ai + 1]); snap_out = argv[ai + 2]; bench = 1; ai += 2; }
        else if (!strcmp(a, "--embed") && ai + 1 < argc) { embed = 1; id = atoi(argv[++ai]); }
        else if (!strcmp(a, "--gen") && ai + 1 < argc) gen = atoi(argv[++ai]);
        else if (!strcmp(a, "--split")) split = 1;
        else if (!strcmp(a, "--paused")) start_paused = 1;
        else if (!strcmp(a, "--url")) want_url = 1;
        else if (!strcmp(a, "--start") && ai + 1 < argc) start = atof(argv[++ai]) / 1000.0;
        else if (!strcmp(a, "--size") && ai + 1 < argc) { sscanf(argv[++ai], "%dx%d", &emb_w, &emb_h); }
        ai++;
    }
    av_log_set_level(AV_LOG_ERROR);
    pq_init(&aq);
    char* url = 0;
    if (embed) {
        mfd = open("/dev/media", O_RDWR);
        if (mfd < 0 || ioctl(mfd, MIO_BIND, (unsigned long)((id & 0xFF) | ((gen & 0xFFFF) << 8))) < 0) { fprintf(stderr, "play: cannot bind media %d\n", id); return 1; }
        if (want_url) {
            url = calloc(1, 8192);
            struct mio_url mu = { (uint64_t)(uintptr_t)url, 8192, 0 };
            if (ioctl(mfd, MIO_URL, &mu) <= 0) { send_error(); return 1; }
        }
        int r;
        if (url) r = open_source(&S[0], url, is_url(url) ? 1 : 0, 0);
        else r = open_source(&S[0], 0, 2, 0);
        if (r < 0) { send_error(); return 1; }
        nsrc = 1;
        if (split && !url) {
            if (open_source(&S[1], 0, 2, 1) == 0) nsrc = 2;
        }
    } else {
        if (ai >= argc) { usage(); return 1; }
        url = argv[ai];
        if (open_source(&S[0], url, is_url(url) ? 1 : 0, 0) < 0) return 1;
        nsrc = 1;
    }
    for (int i = 0; i < nsrc; i++) {
        if (!vsrc && S[i].vs >= 0) { vsrc = &S[i]; vs = S[i].vs; }
        if (!asrc && S[i].as >= 0) { asrc = &S[i]; as = S[i].as; }
    }
    if (vsrc && open_dec(vsrc->fmt->streams[vs], &vdec, 1) < 0) { fprintf(stderr, "play: no decoder for the video (%s)\n", avcodec_get_name(vsrc->fmt->streams[vs]->codecpar->codec_id)); vs = -1; vsrc = 0; }
    if (bench) { if (vs < 0) return 1; return run_bench(url, snap_at, snap_out); }
    if (asrc && open_dec(asrc->fmt->streams[as], &adec, 0) < 0) { as = -1; asrc = 0; }
    if (vs < 0 && as < 0) { fprintf(stderr, "play: nothing playable\n"); send_error(); return 1; }
    for (int i = 0; i < nsrc; i++) {
        double d = S[i].fmt->duration > 0 ? S[i].fmt->duration / (double)AV_TIME_BASE : 0;
        if (d > dur) dur = d;
    }
    for (int i = 0; i < nsrc; i++) if (S[i].kind == 2 && !(S[i].avio->seekable & AVIO_SEEKABLE_NORMAL)) dur = 0;
    fprintf(stderr, "play: %s %.1fs video %s %dx%d audio %s\n", embed ? (url ? "url" : split ? "mse" : "stream") : url, dur, vs >= 0 ? avcodec_get_name(vdec->codec_id) : "none", vs >= 0 ? vdec->width : 0, vs >= 0 ? vdec->height : 0, as >= 0 ? avcodec_get_name(adec->codec_id) : "none");
    if (as >= 0) {
        dsp = open("/dev/dsp", O_WRONLY);
        if (dsp >= 0) {
            int fmtv = 0x10, ch = 2, rate = 48000;
            ioctl(dsp, 0xC0045005, &fmtv);
            ioctl(dsp, 0xC0045006, &ch);
            if (ioctl(dsp, 0xC0045002, &rate) == 0 && rate > 0) dsp_rate = rate;
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_S16, dsp_rate, &adec->ch_layout, adec->sample_fmt, adec->sample_rate, 0, 0);
            if (!swr || swr_init(swr) < 0) { close(dsp); dsp = -1; }
        }
        if (dsp < 0) { as = -1; asrc = 0; }
    }
    if (vs >= 0 && !embed) fb_setup();
    if (!embed && tcgetattr(0, &tio_old) == 0) { have_tio = 1; struct termios t = tio_old; t.c_lflag &= ~(ICANON | ECHO); t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &t); atexit(restore_tty); }
    t0 = now_s();
    if (start > 0) {
        if (url || !split) do_seek(start);
        else { clock_set(start); skip_until = start; }
    }
    if (start_paused) { pause_at = clock_now(); paused = 1; }
    send_status(1);
    pthread_t at;
    if (as >= 0) pthread_create(&at, 0, audio_thread, 0);
    AVPacket* p = av_packet_alloc();
    vf = av_frame_alloc();
    while (!quit) {
        control();
        if (quit) break;
        if (seek_req >= 0) { double t = seek_req; seek_req = -1; do_seek(t); continue; }
        if (ended) { usleep(20000); continue; }
        if (paused && (shown_any || vs < 0)) { usleep(20000); continue; }
        source* s = pick_source();
        if (!s) {
            int all_eof = 1;
            for (int i = 0; i < nsrc; i++) if (!S[i].eof) all_eof = 0;
            if (!all_eof) { usleep(10000); continue; }
            if (vs >= 0) { avcodec_send_packet(vdec, 0); drain_frames(); if (seek_req >= 0 || quit) continue; }
            { double w0 = now_s(); while (!quit && as >= 0 && aq.n > 0 && seek_req < 0 && now_s() - w0 < 3 && !(dur > 0 && clock_now() > dur + 0.5)) { control(); usleep(30000); } }
            if (seek_req >= 0 || quit) continue;
            if (looping && dur > 0) { do_seek(0); continue; }
            if (!embed) { usleep(300000); break; }
            ended = 1;
            send_status(1);
            continue;
        }
        int r = av_read_frame(s->fmt, p);
        if (r < 0) {
            if (r == AVERROR_EXIT && (seek_req >= 0 || quit)) continue;
            if (r == AVERROR(EAGAIN)) { usleep(5000); continue; }
            s->eof = 1;
            continue;
        }
        double pts = p->pts != AV_NOPTS_VALUE ? p->pts * av_q2d(s->fmt->streams[p->stream_index]->time_base) : (p->dts != AV_NOPTS_VALUE ? p->dts * av_q2d(s->fmt->streams[p->stream_index]->time_base) : s->last);
        if (pts > s->last || s->last < 0) s->last = pts;
        if (s == asrc && p->stream_index == as) { AVPacket* c = av_packet_clone(p); pq_put(&aq, c); av_packet_unref(p); continue; }
        if (s == vsrc && p->stream_index == vs) { video_packet(p); av_packet_unref(p); continue; }
        av_packet_unref(p);
    }
    quit = 1;
    if (as >= 0) pthread_join(at, 0);
    if (fbfd >= 0) close(fbfd);
    if (dsp >= 0) close(dsp);
    restore_tty();
    if (!embed) printf("play: shown %d frames, dropped %d\n", shown, dropped);
    av_frame_free(&vf);
    av_packet_free(&p);
    return 0;
}
