#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>

#define TH 22
#define BW 1
#define BM 4
#define MAXC 256

typedef struct { Window frame, client; int used, min, max, ox, oy, ow, oh; } Client;
static Display* d;
static Window root;
static Client cl[MAXC];
static Atom a_proto, a_del, a_name, a_utf8, a_state, a_hidden, a_fullscreen;
static unsigned long c_bar, c_bar_off, c_text, c_border, c_close, c_btn;
static GC gc;
static XFontStruct* fnt;
static Window drag_frame; static int drag_dx, drag_dy;
static Window rs_frame; static int rs_edge, rs_x0, rs_y0, rs_fx, rs_fy, rs_fw, rs_fh;
static Window focused;
static int wfd = -1, dirty;
static int ax, ay, aw, ah;
static Window last_click; static long last_click_ms;

static int xerr(Display* dp, XErrorEvent* e) { (void)dp; (void)e; return 0; }
static long now_ms(void) { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec * 1000L + t.tv_usec / 1000; }
static unsigned long color(const char* n) { XColor c, x; XAllocNamedColor(d, DefaultColormap(d, DefaultScreen(d)), n, &c, &x); return c.pixel; }
static Client* by_frame(Window w) { for (int i = 0; i < MAXC; i++) if (cl[i].used && cl[i].frame == w) return &cl[i]; return NULL; }
static Client* by_client(Window w) { for (int i = 0; i < MAXC; i++) if (cl[i].used && cl[i].client == w) return &cl[i]; return NULL; }

static void title_of(Window w, char* out, int cap) {
    out[0] = 0;
    Atom t; int f; unsigned long n, after; unsigned char* p = NULL;
    if (XGetWindowProperty(d, w, a_name, 0, 256, False, a_utf8, &t, &f, &n, &after, &p) == Success && p) { snprintf(out, cap, "%s", (char*)p); XFree(p); if (out[0]) return; }
    char* nm = NULL; if (XFetchName(d, w, &nm) && nm) { snprintf(out, cap, "%s", nm); XFree(nm); }
    if (!out[0]) { XClassHint h; if (XGetClassHint(d, w, &h)) { snprintf(out, cap, "%s", h.res_class ? h.res_class : (h.res_name ? h.res_name : "")); if (h.res_name) XFree(h.res_name); if (h.res_class) XFree(h.res_class); } }
}

static void title_fold(char* s, int ascii) {
    static const char* const AS = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    unsigned char* i = (unsigned char*)s; char* o = s;
    while (*i) {
        unsigned c = *i, cp; int n;
        if (c < 0x80) { cp = c; n = 1; }
        else if ((c & 0xE0) == 0xC0 && (i[1] & 0xC0) == 0x80) { cp = ((c & 0x1F) << 6) | (i[1] & 0x3F); n = 2; }
        else if ((c & 0xF0) == 0xE0 && (i[1] & 0xC0) == 0x80 && (i[2] & 0xC0) == 0x80) { cp = ((c & 0x0F) << 12) | ((i[1] & 0x3F) << 6) | (i[2] & 0x3F); n = 3; }
        else if ((c & 0xF8) == 0xF0 && (i[1] & 0xC0) == 0x80 && (i[2] & 0xC0) == 0x80 && (i[3] & 0xC0) == 0x80) { cp = 0x10000; n = 4; }
        else { cp = '?'; n = 1; }
        i += n;
        const char* rep = 0; char one[2] = { 0, 0 };
        if (cp < 0x80) one[0] = (char)cp;
        else if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212) rep = "-";
        else if (cp == 0x2018 || cp == 0x2019 || cp == 0x201A) rep = "'";
        else if (cp == 0x201C || cp == 0x201D || cp == 0x201E) rep = "\"";
        else if (cp == 0x2026) rep = "...";
        else if (cp == 0x2022 || cp == 0xB7) rep = ascii ? "-" : 0;
        else if (cp == 0xA0) one[0] = ' ';
        else if (cp >= 0xC0 && cp <= 0xFF) { if (ascii) { one[0] = AS[cp - 0xC0]; if (cp == 0xDF) rep = "ss"; else if (cp == 0xC6) rep = "AE"; else if (cp == 0xE6) rep = "ae"; } else one[0] = (char)cp; }
        else if (cp >= 0xA0 && cp < 0xC0 && !ascii) one[0] = (char)cp;
        else if (cp >= 0x10000) rep = "";
        else one[0] = '?';
        if (!rep && cp == 0xB7 && !ascii) one[0] = (char)0xB7;
        if (!rep) rep = one;
        while (*rep && o < (char*)i) *o++ = *rep++;
        if (*rep) { char tail[512]; snprintf(tail, sizeof tail, "%s%s", rep, (char*)i); snprintf(o, 512, "%s", tail); o += strlen(rep); i = (unsigned char*)o; }
    }
    *o = 0;
}
static void publish(void) {
    if (wfd < 0) return;
    static char b[32768]; int n = 0;
    n += snprintf(b + n, sizeof b - n, "B\n");
    for (int i = 0; i < MAXC && n < (int)sizeof b - 400; i++) if (cl[i].used) {
        XWindowAttributes a; if (!XGetWindowAttributes(d, cl[i].frame, &a)) continue;
        char t[256]; title_of(cl[i].client, t, sizeof t); title_fold(t, 1); for (char* p = t; *p; p++) if (*p == '\n' || *p == '\r') *p = ' ';
        n += snprintf(b + n, sizeof b - n, "W %lu %d %d %d %d %d %d %.120s\n", cl[i].client, a.x, a.y, a.width + 2 * BW, a.height + 2 * BW, cl[i].min, cl[i].client == focused, t);
    }
    n += snprintf(b + n, sizeof b - n, "E\n");
    if (write(wfd, b, n) < 0) {}
    dirty = 0;
}

static void draw_frame(Client* c) {
    XWindowAttributes a; if (!XGetWindowAttributes(d, c->frame, &a)) return;
    unsigned long bar = c->client == focused ? c_bar : c_bar_off;
    XSetForeground(d, gc, bar); XFillRectangle(d, c->frame, gc, 0, 0, a.width, a.height);
    char t[256]; title_of(c->client, t, sizeof t); title_fold(t, 0);
    int tw = a.width - 3 * TH - 12; int tl = (int)strlen(t); while (fnt && tl > 0 && XTextWidth(fnt, t, tl) > tw) tl--;
    XSetForeground(d, gc, c_text); XDrawString(d, c->frame, gc, 8, 15, t, tl);
    int bx = a.width - TH;
    XSetForeground(d, gc, c_close); XFillRectangle(d, c->frame, gc, bx + 3, 3, TH - 6, TH - 6);
    XSetForeground(d, gc, c_text); XDrawLine(d, c->frame, gc, bx + 7, 7, bx + TH - 7, TH - 7); XDrawLine(d, c->frame, gc, bx + TH - 7, 7, bx + 7, TH - 7);
    bx -= TH; XSetForeground(d, gc, c_btn); XFillRectangle(d, c->frame, gc, bx + 3, 3, TH - 6, TH - 6);
    XSetForeground(d, gc, c_text); XDrawRectangle(d, c->frame, gc, bx + 7, 7, TH - 15, TH - 15); if (!c->max) XDrawLine(d, c->frame, gc, bx + 7, 8, bx + TH - 8, 8); else XDrawRectangle(d, c->frame, gc, bx + 9, 5, TH - 15, TH - 15);
    bx -= TH; XSetForeground(d, gc, c_btn); XFillRectangle(d, c->frame, gc, bx + 3, 3, TH - 6, TH - 6);
    XSetForeground(d, gc, c_text); XFillRectangle(d, c->frame, gc, bx + 7, TH - 9, TH - 14, 2);
}

static void geom(Client* c, int* x, int* y, int* w, int* h) {
    XWindowAttributes fa, ca; *x = *y = 0; *w = *h = 1;
    if (XGetWindowAttributes(d, c->frame, &fa)) { *x = fa.x + BW + BM; *y = fa.y + BW + TH; }
    if (XGetWindowAttributes(d, c->client, &ca)) { *w = ca.width; *h = ca.height; }
}

static void send_config(Client* c) {
    int x, y, w, h; geom(c, &x, &y, &w, &h);
    XEvent e; memset(&e, 0, sizeof e); e.xconfigure.type = ConfigureNotify; e.xconfigure.event = c->client; e.xconfigure.window = c->client;
    e.xconfigure.x = x; e.xconfigure.y = y; e.xconfigure.width = w; e.xconfigure.height = h; e.xconfigure.border_width = 0; e.xconfigure.above = None; e.xconfigure.override_redirect = False;
    XSendEvent(d, c->client, False, StructureNotifyMask, &e);
}

static void fit(int* x, int* y, int* w, int* h) {
    int mw = aw - 2 * BW - 2 * BM, mh = ah - TH - BM - 2 * BW;
    if (*w > mw) *w = mw;
    if (*h > mh) *h = mh;
    if (*w < 40) *w = 40;
    if (*h < 20) *h = 20;
    if (*x + *w + BM + BW > ax + aw) *x = ax + aw - *w - BM - BW;
    if (*x < ax + BM + BW) *x = ax + BM + BW;
    if (*y + *h + BM + BW > ay + ah) *y = ay + ah - *h - BM - BW;
    if (*y < ay + TH + BW) *y = ay + TH + BW;
}

static void place(Client* c, int x, int y, int w, int h, int clamp) {
    if (clamp) fit(&x, &y, &w, &h);
    XMoveResizeWindow(d, c->frame, x - BM - BW, y - TH - BW, w + 2 * BM, h + TH + BM);
    XMoveResizeWindow(d, c->client, BM, TH, w, h);
    draw_frame(c); send_config(c); dirty = 1;
}

static void focus(Client* c) {
    if (!c) return;
    Window old = focused; focused = c->client;
    XRaiseWindow(d, c->frame);
    XSetInputFocus(d, c->client, RevertToPointerRoot, CurrentTime);
    Client* o = by_client(old); if (o && o != c) draw_frame(o);
    draw_frame(c); dirty = 1;
}

static void focus_top(void) {
    focused = None;
    Window r, p, *kids; unsigned int nk;
    if (XQueryTree(d, root, &r, &p, &kids, &nk)) { for (int i = (int)nk - 1; i >= 0; i--) { Client* c = by_frame(kids[i]); if (c && !c->min) { focus(c); break; } } if (kids) XFree(kids); }
    dirty = 1;
}

static void minimize(Client* c) { if (c->min) return; c->min = 1; XUnmapWindow(d, c->frame); if (focused == c->client) focus_top(); dirty = 1; }
static void restore(Client* c) { if (c->min) { c->min = 0; XMapWindow(d, c->frame); } focus(c); }
static void maximize(Client* c) {
    if (!c->max) { geom(c, &c->ox, &c->oy, &c->ow, &c->oh); c->max = 1; place(c, ax + BM + BW, ay + TH + BW, aw - 2 * BM - 2 * BW, ah - TH - BM - 2 * BW, 0); }
    else { c->max = 0; place(c, c->ox, c->oy, c->ow, c->oh, 1); }
}

static void manage(Window w) {
    if (by_client(w)) return;
    XWindowAttributes a; if (!XGetWindowAttributes(d, w, &a) || a.override_redirect) return;
    int k = -1; for (int i = 0; i < MAXC; i++) if (!cl[i].used) { k = i; break; } if (k < 0) return;
    int x = a.x, y = a.y, cw = a.width, ch = a.height;
    if (x == 0 && y == 0) { x = ax + (aw - cw) / 2; y = ay + (ah - ch) / 2; }
    fit(&x, &y, &cw, &ch);
    if (cw != a.width || ch != a.height) XResizeWindow(d, w, cw, ch);
    Window f = XCreateSimpleWindow(d, root, x - BM - BW, y - TH - BW, cw + 2 * BM, ch + TH + BM, BW, c_border, c_bar_off);
    XSelectInput(d, f, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);
    XAddToSaveSet(d, w);
    XReparentWindow(d, w, f, BM, TH);
    XSelectInput(d, w, PropertyChangeMask | StructureNotifyMask);
    XGrabButton(d, AnyButton, AnyModifier, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    XMapWindow(d, f); XMapWindow(d, w);
    memset(&cl[k], 0, sizeof cl[k]); cl[k].frame = f; cl[k].client = w; cl[k].used = 1;
    focus(&cl[k]);
    send_config(&cl[k]);
    dirty = 1;
}

static void unmanage(Client* c, int destroyed) {
    if (!destroyed) { XReparentWindow(d, c->client, root, 0, 0); XRemoveFromSaveSet(d, c->client); }
    XDestroyWindow(d, c->frame); c->used = 0;
    if (focused == c->client) focus_top();
    dirty = 1;
}

static void close_client(Client* c) {
    Atom* p = NULL; int n = 0, has = 0;
    if (XGetWMProtocols(d, c->client, &p, &n)) { for (int i = 0; i < n; i++) if (p[i] == a_del) has = 1; XFree(p); }
    if (has) { XEvent e; memset(&e, 0, sizeof e); e.xclient.type = ClientMessage; e.xclient.window = c->client; e.xclient.message_type = a_proto; e.xclient.format = 32; e.xclient.data.l[0] = (long)a_del; e.xclient.data.l[1] = CurrentTime; XSendEvent(d, c->client, False, NoEventMask, &e); }
    else XKillClient(d, c->client);
}

static void command(char* s) {
    char op = s[0]; unsigned long id = 0; int v[4] = {0};
    if (op == 'A') { if (sscanf(s + 1, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]) == 4 && v[2] > 100 && v[3] > 100) { ax = v[0]; ay = v[1]; aw = v[2]; ah = v[3]; for (int i = 0; i < MAXC; i++) if (cl[i].used && cl[i].max) { cl[i].max = 0; maximize(&cl[i]); } } return; }
    if (sscanf(s + 1, "%lu", &id) != 1) return;
    Client* c = by_client((Window)id); if (!c) return;
    if (op == 'R') restore(c);
    else if (op == 'M') minimize(c);
    else if (op == 'X') maximize(c);
    else if (op == 'C') close_client(c);
}

static void read_commands(void) {
    static char buf[4096]; static int bn;
    for (;;) {
        int r = (int)read(wfd, buf + bn, sizeof buf - 1 - bn); if (r <= 0) break; bn += r;
        int s = 0; for (int i = 0; i < bn; i++) if (buf[i] == '\n') { buf[i] = 0; command(buf + s); s = i + 1; }
        memmove(buf, buf + s, bn - s); bn -= s; if (bn >= (int)sizeof buf - 1) bn = 0;
    }
}

static void handle(XEvent* e) {
    switch (e->type) {
    case MapRequest: { Client* c = by_client(e->xmaprequest.window); if (c) restore(c); else manage(e->xmaprequest.window); break; }
    case ConfigureRequest: {
        XConfigureRequestEvent* q = &e->xconfigurerequest; Client* c = by_client(q->window);
        XWindowChanges ch; ch.x = q->x; ch.y = q->y; ch.width = q->width; ch.height = q->height; ch.border_width = q->border_width; ch.sibling = q->above; ch.stack_mode = q->detail;
        if (c) {
            int x, y, w, h; geom(c, &x, &y, &w, &h);
            if (q->value_mask & CWX) x = q->x;
            if (q->value_mask & CWY) y = q->y;
            if (q->value_mask & CWWidth) w = q->width;
            if (q->value_mask & CWHeight) h = q->height;
            if (c->max) { c->ow = w; c->oh = h; send_config(c); } else place(c, x, y, w, h, 1);
        } else XConfigureWindow(d, q->window, q->value_mask, &ch);
        break; }
    case UnmapNotify: { Client* c = by_client(e->xunmap.window); if (c && e->xunmap.event != root) unmanage(c, 0); break; }
    case DestroyNotify: { Client* c = by_client(e->xdestroywindow.window); if (c) unmanage(c, 1); break; }
    case PropertyNotify: { Client* c = by_client(e->xproperty.window); if (c && (e->xproperty.atom == a_name || e->xproperty.atom == XA_WM_NAME)) { draw_frame(c); dirty = 1; } break; }
    case ClientMessage: { Client* c = by_client(e->xclient.window);
        if (c && e->xclient.message_type == XInternAtom(d, "WM_CHANGE_STATE", False) && e->xclient.data.l[0] == IconicState) minimize(c);
        else if (c && e->xclient.message_type == XInternAtom(d, "_NET_ACTIVE_WINDOW", False)) restore(c);
        else if (c && e->xclient.message_type == a_state) { long a1 = e->xclient.data.l[1], a2 = e->xclient.data.l[2]; Atom mv = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_VERT", False), mh = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
            if (a1 == (long)a_hidden || a2 == (long)a_hidden) { if (e->xclient.data.l[0] != 0) minimize(c); }
            else if (a1 == (long)mv || a1 == (long)mh || a2 == (long)mv || a2 == (long)mh || a1 == (long)a_fullscreen) { int want = e->xclient.data.l[0] == 2 ? !c->max : e->xclient.data.l[0] == 1; if (want != c->max) maximize(c); } }
        break; }
    case Expose: { Client* c = by_frame(e->xexpose.window); if (c && e->xexpose.count == 0) draw_frame(c); break; }
    case ButtonPress: {
        Client* c = by_frame(e->xbutton.window);
        if (c) { XWindowAttributes a; XGetWindowAttributes(d, c->frame, &a); focus(c);
            int bx = e->xbutton.x, by = e->xbutton.y;
            if (by < TH && bx >= a.width - TH) close_client(c);
            else if (by < TH && bx >= a.width - 2 * TH) maximize(c);
            else if (by < TH && bx >= a.width - 3 * TH) minimize(c);
            else if (by < TH) {
                long t = now_ms(); if (last_click == c->frame && t - last_click_ms < 400) { maximize(c); last_click = None; break; }
                last_click = c->frame; last_click_ms = t;
                if (!c->max) { drag_frame = c->frame; drag_dx = e->xbutton.x_root - a.x; drag_dy = e->xbutton.y_root - a.y; } }
            else if (!c->max) { rs_edge = 0; if (bx < BM + 4) rs_edge |= 1; if (bx >= a.width - BM - 4) rs_edge |= 2; if (by >= a.height - BM - 4) rs_edge |= 4; if (!rs_edge) rs_edge = 6;
                rs_frame = c->frame; rs_x0 = e->xbutton.x_root; rs_y0 = e->xbutton.y_root; geom(c, &rs_fx, &rs_fy, &rs_fw, &rs_fh); }
            break; }
        c = by_client(e->xbutton.window);
        if (c) focus(c);
        XAllowEvents(d, ReplayPointer, CurrentTime);
        break; }
    case MotionNotify:
        while (XCheckTypedEvent(d, MotionNotify, e)) {}
        if (drag_frame) XMoveWindow(d, drag_frame, e->xmotion.x_root - drag_dx, e->xmotion.y_root - drag_dy);
        else if (rs_frame) { Client* c = by_frame(rs_frame); if (c) { int dx = e->xmotion.x_root - rs_x0, dy = e->xmotion.y_root - rs_y0, x = rs_fx, w = rs_fw, h = rs_fh;
            if (rs_edge & 1) { x = rs_fx + dx; w = rs_fw - dx; if (w < 80) { x -= 80 - w; w = 80; } } if (rs_edge & 2) w = rs_fw + dx; if (rs_edge & 4) h = rs_fh + dy; if (w < 80) w = 80; if (h < 40) h = 40;
            XMoveResizeWindow(d, c->frame, x - BM - BW, rs_fy - TH - BW, w + 2 * BM, h + TH + BM); XMoveResizeWindow(d, c->client, BM, TH, w, h); draw_frame(c); } }
        break;
    case ButtonRelease:
        if (drag_frame) { Client* c = by_frame(drag_frame); if (c) { send_config(c); dirty = 1; } }
        if (rs_frame) { Client* c = by_frame(rs_frame); if (c) { send_config(c); dirty = 1; } }
        drag_frame = None; rs_frame = None; break;
    }
}

int main(void) {
    for (int i = 0; i < 100 && !(d = XOpenDisplay(NULL)); i++) usleep(200000);
    if (!d) { fprintf(stderr, "nvxwm: cannot open display\n"); return 1; }
    XSetErrorHandler(xerr);
    root = DefaultRootWindow(d);
    ax = 0; ay = 0; aw = DisplayWidth(d, DefaultScreen(d)); ah = DisplayHeight(d, DefaultScreen(d)) - 32;
    a_proto = XInternAtom(d, "WM_PROTOCOLS", False); a_del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    a_name = XInternAtom(d, "_NET_WM_NAME", False); a_utf8 = XInternAtom(d, "UTF8_STRING", False);
    a_state = XInternAtom(d, "_NET_WM_STATE", False); a_hidden = XInternAtom(d, "_NET_WM_STATE_HIDDEN", False); a_fullscreen = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
    c_bar = color("#3b5bdb"); c_bar_off = color("#5c6373"); c_text = color("white"); c_border = color("#20242c"); c_close = color("#d9480f"); c_btn = color("#4a5160");
    gc = XCreateGC(d, root, 0, NULL);
    fnt = XQueryFont(d, XGContextFromGC(gc));
    XSetWindowBackground(d, root, 0x010203UL); XClearWindow(d, root);
    { char z[8] = {0}; Pixmap bp = XCreateBitmapFromData(d, root, z, 8, 8); XColor bl; memset(&bl, 0, sizeof bl); Cursor nc = XCreatePixmapCursor(d, bp, bp, &bl, &bl, 0, 0); XDefineCursor(d, root, nc); }
    XSelectInput(d, root, SubstructureRedirectMask | SubstructureNotifyMask);
    { Atom sup[3] = { a_state, a_hidden, XInternAtom(d, "_NET_ACTIVE_WINDOW", False) }; XChangeProperty(d, root, XInternAtom(d, "_NET_SUPPORTED", False), XA_ATOM, 32, PropModeReplace, (unsigned char*)sup, 3); }
    XSync(d, False);
    wfd = open("/dev/nvxwin", O_RDWR | O_NONBLOCK);
    if (wfd >= 0) read_commands();
    Window r, p, *kids; unsigned int nk;
    if (XQueryTree(d, root, &r, &p, &kids, &nk)) { for (unsigned i = 0; i < nk; i++) { XWindowAttributes a; if (XGetWindowAttributes(d, kids[i], &a) && a.map_state == IsViewable) manage(kids[i]); } if (kids) XFree(kids); }
    dirty = 1;
    for (;;) {
        if (dirty) { XFlush(d); publish(); }
        if (!XPending(d)) {
            struct pollfd pf[2] = { { ConnectionNumber(d), POLLIN, 0 }, { wfd, POLLIN, 0 } };
            poll(pf, wfd >= 0 ? 2 : 1, 1000);
            if (wfd >= 0 && (pf[1].revents & POLLIN)) read_commands();
        }
        while (XPending(d)) { XEvent e; XNextEvent(d, &e); handle(&e); }
        XFlush(d);
    }
}
