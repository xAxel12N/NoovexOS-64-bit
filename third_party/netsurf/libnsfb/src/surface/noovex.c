#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <time.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"
#include "libnsfb_plot_util.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"
#include "cursor.h"

struct nvx_fbvar { uint32_t xres, yres, xres_virtual, yres_virtual, xoffset, yoffset, bits_per_pixel, grayscale, rest[32]; };
struct nvx_fbfix { char id[16]; unsigned long smem_start; uint32_t smem_len, type, type_aux, visual; uint16_t xpanstep, ypanstep, ywrapstep; uint32_t line_length; unsigned long mmio_start; uint32_t mmio_len, accel; uint16_t caps, reserved[2]; };

#define NVX_QN 64

struct nvx_priv {
    int fb, mice, kbd;
    int stride;
    int btn;
    struct termios old;
    int have_old;
    nsfb_event_t q[NVX_QN];
    int qh, qt;
    unsigned char kb[64];
    int kn;
    long kt;
};

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void q_push(struct nvx_priv *p, enum nsfb_event_type_e t, int code)
{
    int n = (p->qt + 1) % NVX_QN;
    if (n == p->qh) return;
    p->q[p->qt].type = t;
    p->q[p->qt].value.keycode = code;
    p->qt = n;
}

static void q_move(struct nvx_priv *p, int dx, int dy)
{
    int n = (p->qt + 1) % NVX_QN;
    if (n == p->qh) return;
    p->q[p->qt].type = NSFB_EVENT_MOVE_RELATIVE;
    p->q[p->qt].value.vector.x = dx;
    p->q[p->qt].value.vector.y = dy;
    p->q[p->qt].value.vector.z = 0;
    p->qt = n;
}

static void key_tap(struct nvx_priv *p, int code, int shift, int ctrl)
{
    if (ctrl) q_push(p, NSFB_EVENT_KEY_DOWN, NSFB_KEY_LCTRL);
    if (shift) q_push(p, NSFB_EVENT_KEY_DOWN, NSFB_KEY_LSHIFT);
    q_push(p, NSFB_EVENT_KEY_DOWN, code);
    q_push(p, NSFB_EVENT_KEY_UP, code);
    if (shift) q_push(p, NSFB_EVENT_KEY_UP, NSFB_KEY_LSHIFT);
    if (ctrl) q_push(p, NSFB_EVENT_KEY_UP, NSFB_KEY_LCTRL);
}

static const char nvx_shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
static const char nvx_plain[]   = "`1234567890-=[]\\;',./";

static void key_char(struct nvx_priv *p, unsigned char c)
{
    if (c == 127 || c == 8) { key_tap(p, NSFB_KEY_BACKSPACE, 0, 0); return; }
    if (c == '\r' || c == '\n') { key_tap(p, NSFB_KEY_RETURN, 0, 0); return; }
    if (c == '\t') { key_tap(p, NSFB_KEY_TAB, 0, 0); return; }
    if (c == 27) { key_tap(p, NSFB_KEY_ESCAPE, 0, 0); return; }
    if (c >= 1 && c <= 26) { key_tap(p, NSFB_KEY_a + (c - 1), 0, 1); return; }
    if (c >= 'A' && c <= 'Z') { key_tap(p, NSFB_KEY_a + (c - 'A'), 1, 0); return; }
    if (c >= 'a' && c <= 'z') { key_tap(p, c, 0, 0); return; }
    if (c == ' ' || (c >= '0' && c <= '9')) { key_tap(p, c, 0, 0); return; }
    const char *s = strchr(nvx_shifted, c);
    if (s != NULL && c != 0) { key_tap(p, (unsigned char)nvx_plain[s - nvx_shifted], 1, 0); return; }
    if (c >= 32 && c < 127) key_tap(p, c, 0, 0);
}

static int csi_key(const unsigned char *s, int n, int *used)
{
    int i = 2, num = 0, code = -1;
    while (i < n && s[i] >= '0' && s[i] <= '9') num = num * 10 + (s[i++] - '0');
    while (i < n && s[i] == ';') { i++; while (i < n && s[i] >= '0' && s[i] <= '9') i++; }
    if (i >= n) return -2;
    switch (s[i]) {
    case 'A': code = NSFB_KEY_UP; break;
    case 'B': code = NSFB_KEY_DOWN; break;
    case 'C': code = NSFB_KEY_RIGHT; break;
    case 'D': code = NSFB_KEY_LEFT; break;
    case 'H': code = NSFB_KEY_HOME; break;
    case 'F': code = NSFB_KEY_END; break;
    case '~':
        switch (num) {
        case 1: case 7: code = NSFB_KEY_HOME; break;
        case 2: code = NSFB_KEY_INSERT; break;
        case 3: code = NSFB_KEY_DELETE; break;
        case 4: case 8: code = NSFB_KEY_END; break;
        case 5: code = NSFB_KEY_PAGEUP; break;
        case 6: code = NSFB_KEY_PAGEDOWN; break;
        case 15: code = NSFB_KEY_F5; break;
        default: code = 0; break;
        }
        break;
    default: code = 0; break;
    }
    *used = i + 1;
    return code;
}

static void kbd_parse(struct nvx_priv *p, int flush)
{
    int i = 0;
    while (i < p->kn) {
        unsigned char *s = p->kb + i;
        int n = p->kn - i;
        if (s[0] == 27) {
            if (n == 1) { if (!flush) break; key_char(p, 27); i++; continue; }
            if (s[1] == '[' || s[1] == 'O') {
                int used = 0, code;
                if (n < 3) { if (!flush) break; key_char(p, 27); i++; continue; }
                if (s[1] == 'O') { used = 3; code = s[2] == 'H' ? NSFB_KEY_HOME : s[2] == 'F' ? NSFB_KEY_END : s[2] == 'A' ? NSFB_KEY_UP : s[2] == 'B' ? NSFB_KEY_DOWN : s[2] == 'C' ? NSFB_KEY_RIGHT : s[2] == 'D' ? NSFB_KEY_LEFT : 0; }
                else code = csi_key(s, n, &used);
                if (code == -2) { if (!flush) break; key_char(p, 27); i++; continue; }
                if (code > 0) key_tap(p, code, 0, 0);
                i += used;
                continue;
            }
            key_char(p, 27); i++; continue;
        }
        key_char(p, s[0]);
        i++;
    }
    if (i > 0) { memmove(p->kb, p->kb + i, p->kn - i); p->kn -= i; }
}

static void mice_read(struct nvx_priv *p)
{
    unsigned char b[96];
    int n = read(p->mice, b, sizeof b);
    int pos[2], absok = n >= 3 && ioctl(p->mice, 0x80084E50, pos) == 0;
    if (absok) {
        int k = (p->qt + 1) % NVX_QN;
        if (k != p->qh) {
            p->q[p->qt].type = NSFB_EVENT_MOVE_ABSOLUTE;
            p->q[p->qt].value.vector.x = pos[0];
            p->q[p->qt].value.vector.y = pos[1];
            p->q[p->qt].value.vector.z = 0;
            p->qt = k;
        }
    }
    for (int i = 0; i + 3 <= n; i += 3) {
        int dx = (signed char)b[i + 1], dy = (signed char)b[i + 2], bt = b[i] & 7;
        if (!absok && (dx || dy)) q_move(p, dx, -dy);
        static const int bit[3] = { 1, 2, 4 };
        static const int key[3] = { NSFB_KEY_MOUSE_1, NSFB_KEY_MOUSE_3, NSFB_KEY_MOUSE_2 };
        for (int k = 0; k < 3; k++) {
            if ((bt & bit[k]) && !(p->btn & bit[k])) q_push(p, NSFB_EVENT_KEY_DOWN, key[k]);
            if (!(bt & bit[k]) && (p->btn & bit[k])) q_push(p, NSFB_EVENT_KEY_UP, key[k]);
        }
        p->btn = bt;
    }
}

static int nvx_defaults(nsfb_t *nsfb)
{
    nsfb->width = 0;
    nsfb->height = 0;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    return 0;
}

static int nvx_initialise(nsfb_t *nsfb)
{
    struct nvx_priv *p;
    struct nvx_fbvar v;
    struct nvx_fbfix f;
    if (nsfb->surface_priv != NULL) return -1;
    p = calloc(1, sizeof *p);
    if (p == NULL) return -1;
    p->fb = open("/dev/fb0", O_RDWR | O_CLOEXEC);
    if (p->fb < 0) { free(p); return -1; }
    memset(&v, 0, sizeof v);
    memset(&f, 0, sizeof f);
    if (ioctl(p->fb, 0x4600, &v) < 0 || v.xres == 0) { v.xres = 1024; v.yres = 768; }
    p->stride = (ioctl(p->fb, 0x4602, &f) == 0 && f.line_length) ? (int)f.line_length : (int)v.xres * 4;
    nsfb->width = v.xres;
    nsfb->height = v.yres;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    nsfb->linelen = nsfb->width * 4;
    nsfb->ptr = calloc((size_t)nsfb->linelen, (size_t)nsfb->height);
    if (nsfb->ptr == NULL) { close(p->fb); free(p); return -1; }
    p->mice = open("/dev/input/mice", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    p->kbd = 0;
    if (tcgetattr(p->kbd, &p->old) == 0) {
        struct termios t = p->old;
        t.c_lflag &= ~(ICANON | ECHO | ISIG);
        t.c_iflag &= ~(IXON | ICRNL);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(p->kbd, TCSANOW, &t);
        p->have_old = 1;
    }
    nsfb->surface_priv = p;
    return 0;
}

static int nvx_finalise(nsfb_t *nsfb)
{
    struct nvx_priv *p = nsfb->surface_priv;
    if (p == NULL) return 0;
    if (p->have_old) tcsetattr(p->kbd, TCSANOW, &p->old);
    if (p->mice >= 0) close(p->mice);
    close(p->fb);
    free(nsfb->ptr);
    nsfb->ptr = NULL;
    free(p);
    nsfb->surface_priv = NULL;
    return 0;
}

static int nvx_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
    if (nsfb->surface_priv != NULL) return -1;
    if (width > 0) nsfb->width = width;
    if (height > 0) nsfb->height = height;
    if (format != NSFB_FMT_ANY) nsfb->format = format;
    select_plotters(nsfb);
    return 0;
}

static bool nvx_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
    struct nvx_priv *p = nsfb->surface_priv;
    if (p == NULL) return false;
    if (p->qh == p->qt) {
        struct pollfd pf[2];
        int np = 0;
        pf[np].fd = p->kbd; pf[np].events = POLLIN; pf[np].revents = 0; np++;
        if (p->mice >= 0) { pf[np].fd = p->mice; pf[np].events = POLLIN; pf[np].revents = 0; np++; }
        int t = timeout < 0 ? 100 : timeout;
        if (p->kn > 0 && t > 30) t = 30;
        int r = poll(pf, np, t);
        if (r > 0 && (pf[0].revents & POLLIN)) {
            int n = read(p->kbd, p->kb + p->kn, (int)sizeof p->kb - p->kn);
            if (n > 0) { if (p->kn == 0) p->kt = now_ms(); p->kn += n; }
        }
        if (r > 0 && np > 1 && (pf[1].revents & POLLIN)) mice_read(p);
        if (p->kn > 0) {
            kbd_parse(p, 0);
            if (p->kn > 0 && now_ms() - p->kt >= 50) kbd_parse(p, 1);
        }
    }
    if (p->qh != p->qt) {
        *event = p->q[p->qh];
        p->qh = (p->qh + 1) % NVX_QN;
        return true;
    }
    event->type = NSFB_EVENT_CONTROL;
    event->value.controlcode = NSFB_CONTROL_TIMEOUT;
    return true;
}

static void push_rect(nsfb_t *nsfb, int x0, int y0, int x1, int y1)
{
    struct nvx_priv *p = nsfb->surface_priv;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > nsfb->width) x1 = nsfb->width;
    if (y1 > nsfb->height) y1 = nsfb->height;
    if (x1 <= x0 || y1 <= y0) return;
    if (x0 == 0 && x1 == nsfb->width && p->stride == nsfb->linelen) {
        lseek(p->fb, (off_t)y0 * p->stride, SEEK_SET);
        size_t len = (size_t)(y1 - y0) * p->stride, off = 0;
        const uint8_t *src = nsfb->ptr + (size_t)y0 * nsfb->linelen;
        while (off < len) { ssize_t w = write(p->fb, src + off, len - off); if (w <= 0) break; off += (size_t)w; }
        return;
    }
    for (int y = y0; y < y1; y++) {
        lseek(p->fb, (off_t)y * p->stride + (off_t)x0 * 4, SEEK_SET);
        if (write(p->fb, nsfb->ptr + (size_t)y * nsfb->linelen + (size_t)x0 * 4, (size_t)(x1 - x0) * 4) <= 0) break;
    }
}

static int nvx_claim(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    struct nsfb_cursor_s *cursor = nsfb->cursor;
    if (cursor != NULL && cursor->plotted && nsfb_plot_bbox_intersect(box, &cursor->loc))
        nsfb_cursor_clear(nsfb, cursor);
    return 0;
}

static int nvx_cursor(nsfb_t *nsfb, struct nsfb_cursor_s *cursor)
{
    nsfb_bbox_t redraw, loc_shift;
    if (cursor == NULL || !cursor->plotted) return true;
    loc_shift = cursor->loc;
    loc_shift.x0 -= cursor->hotspot_x;
    loc_shift.y0 -= cursor->hotspot_y;
    loc_shift.x1 -= cursor->hotspot_x;
    loc_shift.y1 -= cursor->hotspot_y;
    nsfb_plot_add_rect(&cursor->savloc, &loc_shift, &redraw);
    nsfb_cursor_clear(nsfb, cursor);
    nsfb_cursor_plot(nsfb, cursor);
    push_rect(nsfb, redraw.x0, redraw.y0, redraw.x1, redraw.y1);
    return true;
}

static int nvx_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    struct nsfb_cursor_s *cursor = nsfb->cursor;
    if (cursor != NULL && !cursor->plotted) nsfb_cursor_plot(nsfb, cursor);
    push_rect(nsfb, box->x0, box->y0, box->x1, box->y1);
    return 0;
}

const nsfb_surface_rtns_t noovex_rtns = {
    .defaults = nvx_defaults,
    .initialise = nvx_initialise,
    .finalise = nvx_finalise,
    .input = nvx_input,
    .claim = nvx_claim,
    .update = nvx_update,
    .cursor = nvx_cursor,
    .geometry = nvx_geometry,
};

NSFB_SURFACE_DEF(noovex, NSFB_SURFACE_LINUX, &noovex_rtns)
