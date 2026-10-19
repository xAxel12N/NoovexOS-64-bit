#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TH 22
#define BW 1
#define MAXC 256

typedef struct { Window frame, client; int used; } Client;
static Display* d;
static Window root;
static Client cl[MAXC];
static Atom a_proto, a_del, a_name, a_utf8;
static unsigned long c_bar, c_bar_off, c_text, c_border, c_close;
static GC gc;
static Window drag_frame; static int drag_dx, drag_dy;
static Window focused;

static int xerr(Display* dp, XErrorEvent* e) { (void)dp; (void)e; return 0; }
static unsigned long color(const char* n) { XColor c, x; XAllocNamedColor(d, DefaultColormap(d, DefaultScreen(d)), n, &c, &x); return c.pixel; }
static Client* by_frame(Window w) { for (int i = 0; i < MAXC; i++) if (cl[i].used && cl[i].frame == w) return &cl[i]; return NULL; }
static Client* by_client(Window w) { for (int i = 0; i < MAXC; i++) if (cl[i].used && cl[i].client == w) return &cl[i]; return NULL; }

static void title_of(Window w, char* out, int cap) {
    out[0] = 0;
    Atom t; int f; unsigned long n, after; unsigned char* p = NULL;
    if (XGetWindowProperty(d, w, a_name, 0, 256, False, a_utf8, &t, &f, &n, &after, &p) == Success && p) { snprintf(out, cap, "%s", (char*)p); XFree(p); if (out[0]) return; }
    char* nm = NULL; if (XFetchName(d, w, &nm) && nm) { snprintf(out, cap, "%s", nm); XFree(nm); }
}

static void draw_frame(Client* c) {
    XWindowAttributes a; if (!XGetWindowAttributes(d, c->frame, &a)) return;
    XSetForeground(d, gc, c->client == focused ? c_bar : c_bar_off);
    XFillRectangle(d, c->frame, gc, 0, 0, a.width, TH);
    char t[256]; title_of(c->client, t, sizeof t);
    XSetForeground(d, gc, c_text); XDrawString(d, c->frame, gc, 8, 15, t, (int)strlen(t));
    XSetForeground(d, gc, c_close); XFillRectangle(d, c->frame, gc, a.width - TH + 3, 3, TH - 6, TH - 6);
    XSetForeground(d, gc, c_text); XDrawLine(d, c->frame, gc, a.width - TH + 7, 7, a.width - 7, TH - 7); XDrawLine(d, c->frame, gc, a.width - 7, 7, a.width - TH + 7, TH - 7);
}

static void focus(Client* c) {
    if (!c) return;
    Window old = focused; focused = c->client;
    XRaiseWindow(d, c->frame);
    XSetInputFocus(d, c->client, RevertToPointerRoot, CurrentTime);
    Client* o = by_client(old); if (o) draw_frame(o);
    draw_frame(c);
}

static void manage(Window w) {
    if (by_client(w)) return;
    XWindowAttributes a; if (!XGetWindowAttributes(d, w, &a) || a.override_redirect) return;
    int k = -1; for (int i = 0; i < MAXC; i++) if (!cl[i].used) { k = i; break; } if (k < 0) return;
    int sw = DisplayWidth(d, DefaultScreen(d)), sh = DisplayHeight(d, DefaultScreen(d));
    if (a.width > sw - 40 || a.height > sh - TH - 40) { a.width = sw * 3 / 4; a.height = sh * 3 / 4; a.x = (sw - a.width) / 2; a.y = (sh - a.height) / 2; XResizeWindow(d, w, a.width, a.height); }
    int x = a.x, y = a.y; if (y < TH) y = TH;
    Window f = XCreateSimpleWindow(d, root, x, y - TH, a.width, a.height + TH, BW, c_border, c_bar_off);
    XSelectInput(d, f, SubstructureRedirectMask | SubstructureNotifyMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ExposureMask);
    XAddToSaveSet(d, w);
    XReparentWindow(d, w, f, 0, TH);
    XSelectInput(d, w, PropertyChangeMask | StructureNotifyMask);
    XGrabButton(d, AnyButton, AnyModifier, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    XMapWindow(d, f); XMapWindow(d, w);
    cl[k].frame = f; cl[k].client = w; cl[k].used = 1;
    focus(&cl[k]);
}

static void unmanage(Client* c, int destroyed) {
    if (!destroyed) { XReparentWindow(d, c->client, root, 0, 0); XRemoveFromSaveSet(d, c->client); }
    XDestroyWindow(d, c->frame); c->used = 0;
    if (focused == c->client) { focused = None; for (int i = MAXC - 1; i >= 0; i--) if (cl[i].used) { focus(&cl[i]); break; } }
}

static void close_client(Client* c) {
    Atom* p = NULL; int n = 0, has = 0;
    if (XGetWMProtocols(d, c->client, &p, &n)) { for (int i = 0; i < n; i++) if (p[i] == a_del) has = 1; XFree(p); }
    if (has) { XEvent e; memset(&e, 0, sizeof e); e.xclient.type = ClientMessage; e.xclient.window = c->client; e.xclient.message_type = a_proto; e.xclient.format = 32; e.xclient.data.l[0] = (long)a_del; e.xclient.data.l[1] = CurrentTime; XSendEvent(d, c->client, False, NoEventMask, &e); }
    else XKillClient(d, c->client);
}

int main(void) {
    for (int i = 0; i < 100 && !(d = XOpenDisplay(NULL)); i++) usleep(200000);
    if (!d) { fprintf(stderr, "nvxwm: cannot open display\n"); return 1; }
    XSetErrorHandler(xerr);
    root = DefaultRootWindow(d);
    a_proto = XInternAtom(d, "WM_PROTOCOLS", False); a_del = XInternAtom(d, "WM_DELETE_WINDOW", False);
    a_name = XInternAtom(d, "_NET_WM_NAME", False); a_utf8 = XInternAtom(d, "UTF8_STRING", False);
    c_bar = color("#3b5bdb"); c_bar_off = color("#5c6373"); c_text = color("white"); c_border = color("#20242c"); c_close = color("#d9480f");
    gc = XCreateGC(d, root, 0, NULL);
    XSetWindowBackground(d, root, 0x010203UL); XClearWindow(d, root);
    { char z[8] = {0}; Pixmap bp = XCreateBitmapFromData(d, root, z, 8, 8); XColor bl; memset(&bl, 0, sizeof bl); Cursor nc = XCreatePixmapCursor(d, bp, bp, &bl, &bl, 0, 0); XDefineCursor(d, root, nc); }
    XSelectInput(d, root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(d, False);
    Window r, p, *kids; unsigned int nk;
    if (XQueryTree(d, root, &r, &p, &kids, &nk)) { for (unsigned i = 0; i < nk; i++) { XWindowAttributes a; if (XGetWindowAttributes(d, kids[i], &a) && a.map_state == IsViewable) manage(kids[i]); } if (kids) XFree(kids); }
    for (;;) {
        XEvent e; XNextEvent(d, &e);
        switch (e.type) {
        case MapRequest: manage(e.xmaprequest.window); break;
        case ConfigureRequest: {
            XConfigureRequestEvent* q = &e.xconfigurerequest; Client* c = by_client(q->window);
            XWindowChanges ch; ch.x = q->x; ch.y = q->y; ch.width = q->width; ch.height = q->height; ch.border_width = q->border_width; ch.sibling = q->above; ch.stack_mode = q->detail;
            if (c) {
                int sw = DisplayWidth(d, DefaultScreen(d)), sh = DisplayHeight(d, DefaultScreen(d));
                if ((q->value_mask & CWWidth) && ch.width > sw - 40) ch.width = sw * 3 / 4;
                if ((q->value_mask & CWHeight) && ch.height > sh - TH - 40) ch.height = sh * 3 / 4;
                XWindowChanges fc = ch; fc.y = q->y - TH; fc.height = q->height + TH; fc.border_width = BW;
                XConfigureWindow(d, c->frame, q->value_mask & (CWX | CWY | CWWidth | CWHeight), &fc);
                ch.x = 0; ch.y = TH; XConfigureWindow(d, q->window, q->value_mask & (CWWidth | CWHeight), &ch);
                draw_frame(c);
            } else XConfigureWindow(d, q->window, q->value_mask, &ch);
            break; }
        case UnmapNotify: { Client* c = by_client(e.xunmap.window); if (c && e.xunmap.event != root) unmanage(c, 0); break; }
        case DestroyNotify: { Client* c = by_client(e.xdestroywindow.window); if (c) unmanage(c, 1); break; }
        case PropertyNotify: { Client* c = by_client(e.xproperty.window); if (c) draw_frame(c); break; }
        case Expose: { Client* c = by_frame(e.xexpose.window); if (c && e.xexpose.count == 0) draw_frame(c); break; }
        case ButtonPress: {
            Client* c = by_frame(e.xbutton.window);
            if (c) { XWindowAttributes a; XGetWindowAttributes(d, c->frame, &a); focus(c);
                if (e.xbutton.y < TH && e.xbutton.x >= a.width - TH) close_client(c);
                else if (e.xbutton.y < TH) { drag_frame = c->frame; drag_dx = e.xbutton.x_root - a.x; drag_dy = e.xbutton.y_root - a.y; }
                break; }
            c = by_client(e.xbutton.window);
            if (c) focus(c);
            XAllowEvents(d, ReplayPointer, CurrentTime);
            break; }
        case MotionNotify: if (drag_frame) { while (XCheckTypedEvent(d, MotionNotify, &e)) {} XMoveWindow(d, drag_frame, e.xmotion.x_root - drag_dx, e.xmotion.y_root - drag_dy); } break;
        case ButtonRelease: drag_frame = None; break;
        }
    }
}
