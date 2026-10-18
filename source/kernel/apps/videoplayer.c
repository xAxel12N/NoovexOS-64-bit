#include "include/nvx/base.h"
#include "include/nvx/lib/fmt.h"
#include "include/nvx/drivers/video/framebuffer.h"
#include "include/nvx/gui/theme.h"
#include "include/nvx/gui/wm.h"
#include "include/nvx/fs/ramfs.h"
#include "include/nvx/fs/dirtree.h"
#include "include/nvx/core/media.h"
#include "include/nvx/apps/videoplayer.h"

int fext_eq(const char* name, const char* ext);

static int vp_id = -1;
static char vp_title[128] = "Video Player";
static char vp_src[1024];
static int vp_vol = 1000, vp_muted, vp_loop;
static int vp_bw, vp_bh;
static int vp_full;
static u64 vp_seen_seq;
static u32 vp_seen_flags;
static i64 vp_seen_sec = -1;

static void tfmt(char* o, i64 ms) {
    if (ms < 0) ms = 0;
    u32 s = (u32)(ms / 1000), h = s / 3600, m = (s / 60) % 60, ss = s % 60;
    char* p = o;
    if (h) { p = udec(p, h); *p++ = ':'; if (m < 10) *p++ = '0'; }
    p = udec(p, m);
    *p++ = ':';
    if (ss < 10) *p++ = '0';
    p = udec(p, ss);
    *p = 0;
}

int video_is_media_name(const char* n) {
    static const char* ex[] = { "mp4", "m4v", "mov", "mkv", "webm", "mp3", "m4a", "aac", "ogg", "oga", "ogv", "opus", "wav", 0 };
    for (int i = 0; ex[i]; i++) if (fext_eq(n, ex[i])) return 1;
    return 0;
}

void media_blit(int id, int x, int y, int w, int h, u32 bg) {
    int fw, fh;
    u64 seq;
    const u32* f = media_frame(id, &fw, &fh, &seq);
    if (!f || fw <= 0 || fh <= 0 || w <= 0 || h <= 0) { fill_rect(x, y, w, h, bg); return; }
    int dw = w, dh = (int)((i64)w * fh / fw);
    if (dh > h) { dh = h; dw = (int)((i64)h * fw / fh); }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int dx = x + (w - dw) / 2, dy = y + (h - dh) / 2;
    if (dw != w || dh != h) {
        fill_rect(x, y, w, dy - y, bg);
        fill_rect(x, dy + dh, w, y + h - dy - dh, bg);
        fill_rect(x, dy, dx - x, dh, bg);
        fill_rect(dx + dw, dy, x + w - dx - dw, dh, bg);
    }
    int x0 = dx < 0 ? 0 : dx, x1 = dx + dw > (int)W ? (int)W : dx + dw;
    int y0 = dy < 0 ? 0 : dy, y1 = dy + dh > (int)H ? (int)H : dy + dh;
    if (x0 >= x1 || y0 >= y1) return;
    if (dw == fw && dh == fh) {
        for (int yy = y0; yy < y1; yy++) {
            const u32* s = f + (u64)(yy - dy) * fw + (x0 - dx);
            u32* d = &SCENE[(u64)yy * SW + x0];
            for (int k = 0; k < x1 - x0; k++) d[k] = s[k] & 0xFFFFFFu;
        }
        return;
    }
    u32 sxs = (u32)(((u64)fw << 16) / (u32)dw), sys = (u32)(((u64)fh << 16) / (u32)dh);
    for (int yy = y0; yy < y1; yy++) {
        u32 sy = (u32)(((u64)(yy - dy) * sys) >> 16);
        if (sy >= (u32)fh) sy = (u32)fh - 1;
        const u32* srow = f + (u64)sy * fw;
        u32* d = &SCENE[(u64)yy * SW];
        u32 sx = (u32)(x0 - dx) * sxs;
        for (int xx = x0; xx < x1; xx++, sx += sxs) { u32 si = sx >> 16; if (si >= (u32)fw) si = (u32)fw - 1; d[xx] = srow[si] & 0xFFFFFFu; }
    }
}

void video_stop(void) {
    if (vp_id >= 0) media_free(vp_id);
    vp_id = -1;
    if (vp_full) { vp_full = 0; if (WN[W_VIDEO].max) win_max(W_VIDEO); }
}

int video_open_url(const char* url, const char* title) {
    video_stop();
    vp_id = media_new();
    if (vp_id < 0) return -1;
    int i = 0;
    for (; url[i] && i < (int)sizeof(vp_src) - 1; i++) vp_src[i] = url[i];
    vp_src[i] = 0;
    const char* t = title && title[0] ? title : url;
    for (i = 0; t[i] && i < (int)sizeof(vp_title) - 1; i++) vp_title[i] = t[i];
    vp_title[i] = 0;
    open_win(W_VIDEO);
    vp_bw = WN[W_VIDEO].w - 2;
    vp_bh = WN[W_VIDEO].h - TITLEH - 52;
    vp_seen_seq = 0;
    if (media_start(vp_id, vp_src, 0, vp_bw, vp_bh, 0, 0) < 0) return -1;
    if (vp_muted || vp_vol != 1000) media_cmd(vp_id, MEDIA_CMD_VOLUME, vp_muted ? 0 : vp_vol, 0);
    if (vp_loop) media_cmd(vp_id, MEDIA_CMD_LOOP, 1, 0);
    return 0;
}

int video_open_slot(int slot) {
    if (slot < 0 || slot >= FS_N || !FS[slot].used) return -1;
    char path[1024];
    dt_path_n(FS[slot].dir, path, (int)sizeof(path) - 70);
    int n = 0;
    while (path[n]) n++;
    if (n == 0 || path[n - 1] != '/') path[n++] = '/';
    for (int k = 0; FS[slot].name[k] && n < (int)sizeof(path) - 1; k++) path[n++] = FS[slot].name[k];
    path[n] = 0;
    return video_open_url(path, FS[slot].name);
}

static void geom(int* vx, int* vy, int* vw, int* vh, int* cy) {
    int i = W_VIDEO;
    *vx = WN[i].x + 1;
    *vy = WN[i].y + TITLEH;
    *vw = WN[i].w - 2;
    *vh = WN[i].h - TITLEH - 52;
    *cy = *vy + *vh;
}

static void tri(int x, int y, int s, u32 c) { for (int r = 0; r < s; r++) { int l = r < s / 2 ? r : s - 1 - r; fill_rect(x, y + r, l + 1, 1, c); } }

void video_draw(int i) {
    win_frame(i, vp_title);
    int vx, vy, vw, vh, cy;
    geom(&vx, &vy, &vw, &vh, &cy);
    struct media_status st;
    st.flags = 0; st.cur_ms = 0; st.dur_ms = -1; st.vw = st.vh = 0; st.buffered_pct = 0;
    if (vp_id >= 0) media_get_status(vp_id, &st);
    int fw = 0, fh = 0;
    u64 seq = 0;
    if (vp_id >= 0 && media_frame(vp_id, &fw, &fh, &seq)) media_blit(vp_id, vx, vy, vw, vh, 0);
    else {
        fill_rect(vx, vy, vw, vh, 0);
        const char* msg = vp_id < 0 ? "Open a video from Files, or type: video <file or URL>" : (st.flags & MEDIA_ST_ERROR) ? "This file cannot be played" : (st.flags & MEDIA_ST_AUDIO) && !(st.flags & MEDIA_ST_VIDEO) && (st.flags & MEDIA_ST_READY) ? "Audio" : "Loading...";
        dstr_ctr(vx + vw / 2, vy + vh / 2 - 8, msg, rgb(200, 205, 220), 1);
        if ((st.flags & MEDIA_ST_AUDIO) && !(st.flags & MEDIA_ST_VIDEO)) { fill_circle(vx + vw / 2, vy + vh / 2 - 60, 34, rgb(60, 140, 240)); tri(vx + vw / 2 - 8, vy + vh / 2 - 76, 32, rgb(255, 255, 255)); }
    }
    if ((st.flags & MEDIA_ST_WAITING) && fw) dstr_ctr(vx + vw / 2, vy + 16, "Buffering...", rgb(255, 255, 255), 1);
    fill_rect(vx, cy, vw, 52, rgb(22, 24, 32));
    int playing = (st.flags & MEDIA_ST_PLAYING) && !(st.flags & MEDIA_ST_ENDED);
    int bx = vx + 14, by = cy + 26;
    fill_circle(bx + 10, by + 4, 15, rgb(60, 140, 240));
    if (playing) { fill_rect(bx + 4, by - 4, 4, 16, rgb(255, 255, 255)); fill_rect(bx + 12, by - 4, 4, 16, rgb(255, 255, 255)); }
    else tri(bx + 6, by - 5, 18, rgb(255, 255, 255));
    char t1[16], t2[16], tt[40];
    tfmt(t1, st.cur_ms);
    tfmt(t2, st.dur_ms);
    char* p = tt;
    p = scopy(p, t1);
    if (st.dur_ms > 0) { p = scopy(p, " / "); p = scopy(p, t2); }
    *p = 0;
    int tx = bx + 34;
    dstr(tx, by - 4, tt, rgb(220, 224, 236), 1);
    int sx = tx + 120, sw = vw - (sx - vx) - 120, sy = cy + 8;
    if (sw > 40) {
        fill_round(sx, sy + 2, sw, 6, 3, 15, rgb(70, 74, 92));
        if (st.dur_ms > 0) {
            int fill = (int)((i64)sw * st.cur_ms / st.dur_ms);
            if (fill > sw) fill = sw;
            if (fill < 0) fill = 0;
            if (fill > 0) fill_round(sx, sy + 2, fill, 6, 3, 15, rgb(60, 140, 240));
            fill_circle(sx + fill, sy + 5, 7, rgb(255, 255, 255));
        }
    }
    int ax = vx + vw - 106;
    u32 ic = rgb(220, 224, 236);
    fill_rect(ax, by, 5, 8, ic);
    tri(ax + 3, by - 4, 17, ic);
    if (vp_muted) { for (int k = 0; k < 9; k++) { putpx(ax + 14 + k, by - 1 + k, rgb(240, 90, 90)); putpx(ax + 22 - k, by - 1 + k, rgb(240, 90, 90)); } }
    else { int lv = vp_vol * 3 / 1000; for (int k = 0; k < lv; k++) fill_rect(ax + 14 + k * 4, by + 6 - k * 3, 2, 3 + k * 3, ic); }
    int lx = ax + 38;
    fill_round(lx, by - 3, 22, 14, 4, 15, vp_loop ? rgb(60, 140, 240) : rgb(50, 54, 70));
    dstr(lx + 4, by - 3, "L", rgb(255, 255, 255), 1);
    int fx = ax + 70;
    u32 fc = rgb(220, 224, 236);
    fill_rect(fx, by - 4, 7, 2, fc); fill_rect(fx, by - 4, 2, 7, fc);
    fill_rect(fx + 13, by - 4, 7, 2, fc); fill_rect(fx + 18, by - 4, 2, 7, fc);
    fill_rect(fx, by + 10, 7, 2, fc); fill_rect(fx, by + 5, 2, 7, fc);
    fill_rect(fx + 13, by + 10, 7, 2, fc); fill_rect(fx + 18, by + 5, 2, 7, fc);
    if (vp_id >= 0 && (vw != vp_bw || vh != vp_bh)) { vp_bw = vw; vp_bh = vh; media_cmd(vp_id, MEDIA_CMD_SIZE, vw, vh); }
    vp_seen_seq = seq;
}

static void toggle_play(void) {
    if (vp_id < 0) return;
    struct media_status st;
    media_get_status(vp_id, &st);
    if (st.flags & MEDIA_ST_ERROR) { media_restart(vp_id, 0, 1); return; }
    if ((st.flags & MEDIA_ST_PLAYING) && !(st.flags & MEDIA_ST_ENDED)) media_cmd(vp_id, MEDIA_CMD_PAUSE, 0, 0);
    else media_cmd(vp_id, MEDIA_CMD_PLAY, 0, 0);
}

static void seek_rel(i64 d) {
    if (vp_id < 0) return;
    struct media_status st;
    media_get_status(vp_id, &st);
    i64 t = st.cur_ms + d;
    if (t < 0) t = 0;
    if (st.dur_ms > 0 && t > st.dur_ms) t = st.dur_ms;
    media_cmd(vp_id, MEDIA_CMD_SEEK, t, 0);
}

static void set_vol(void) { if (vp_id >= 0) media_cmd(vp_id, MEDIA_CMD_VOLUME, vp_muted ? 0 : vp_vol, 0); }

static void toggle_full(void) { vp_full = !vp_full; win_max(W_VIDEO); }

void video_click(int px, int py) {
    int vx, vy, vw, vh, cy;
    geom(&vx, &vy, &vw, &vh, &cy);
    if (py < cy) { toggle_play(); return; }
    int bx = vx + 14;
    if (px >= bx - 8 && px < bx + 28) { toggle_play(); return; }
    int sx = bx + 34 + 120, sw = vw - (sx - vx) - 120;
    if (sw > 40 && px >= sx - 4 && px <= sx + sw + 4 && py < cy + 24) {
        struct media_status st;
        if (vp_id >= 0 && media_get_status(vp_id, &st) == 0 && st.dur_ms > 0) {
            int r = px - sx;
            if (r < 0) r = 0;
            if (r > sw) r = sw;
            media_cmd(vp_id, MEDIA_CMD_SEEK, (i64)r * st.dur_ms / sw, 0);
        }
        return;
    }
    int ax = vx + vw - 106;
    if (px >= ax - 4 && px < ax + 32) { vp_muted = !vp_muted; set_vol(); return; }
    if (px >= ax + 36 && px < ax + 62) { vp_loop = !vp_loop; if (vp_id >= 0) media_cmd(vp_id, MEDIA_CMD_LOOP, vp_loop, 0); return; }
    if (px >= ax + 66 && px < ax + 96) { toggle_full(); return; }
}

void video_key(int c) {
    if (c == ' ' || c == 'k' || c == 'p') toggle_play();
    else if (c == 18 || c == 'l') seek_rel(c == 'l' ? 10000 : 5000);
    else if (c == 17 || c == 'j') seek_rel(c == 'j' ? -10000 : -5000);
    else if (c == 19) { vp_vol += 100; if (vp_vol > 1000) vp_vol = 1000; vp_muted = 0; set_vol(); }
    else if (c == 20) { vp_vol -= 100; if (vp_vol < 0) vp_vol = 0; set_vol(); }
    else if (c == 'm') { vp_muted = !vp_muted; set_vol(); }
    else if (c == 'f') toggle_full();
    else if (c == 27 && vp_full) toggle_full();
    else if (c >= '0' && c <= '9' && vp_id >= 0) {
        struct media_status st;
        media_get_status(vp_id, &st);
        if (st.dur_ms > 0) media_cmd(vp_id, MEDIA_CMD_SEEK, st.dur_ms * (c - '0') / 10, 0);
    }
}

int desk_win_exposed(int i);
void present_rect(int x, int y, int w, int h);

int video_fast(void) {
    if (vp_id < 0 || !WN[W_VIDEO].open || WN[W_VIDEO].min) return 0;
    int fw, fh;
    u64 seq = 0;
    if (!media_frame(vp_id, &fw, &fh, &seq) || seq == vp_seen_seq) return 0;
    if (!desk_win_exposed(W_VIDEO)) return 1;
    int vx, vy, vw, vh, cy;
    geom(&vx, &vy, &vw, &vh, &cy);
    media_blit(vp_id, vx, vy, vw, vh, 0);
    present_rect(vx, vy, vw, vh);
    vp_seen_seq = seq;
    return 0;
}

int video_tick(void) {
    if (vp_id < 0 || !WN[W_VIDEO].open || WN[W_VIDEO].min) return 0;
    int fw, fh;
    u64 seq = 0;
    media_frame(vp_id, &fw, &fh, &seq);
    struct media_status st;
    media_get_status(vp_id, &st);
    int ch = 0;
    if (seq != vp_seen_seq && !vp_seen_seq) ch = 1;
    if (st.flags != vp_seen_flags) { vp_seen_flags = st.flags; ch = 1; }
    if (st.cur_ms / 1000 != vp_seen_sec) { vp_seen_sec = st.cur_ms / 1000; ch = 1; }
    return ch;
}
