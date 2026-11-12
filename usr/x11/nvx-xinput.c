#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <X11/Xatom.h>

extern int XTestFakeKeyEvent(Display*, unsigned int, Bool, unsigned long);
extern int XTestFakeButtonEvent(Display*, unsigned int, Bool, unsigned long);
extern int XTestFakeMotionEvent(Display*, int, int, int, unsigned long);
extern Bool XTestQueryExtension(Display*, int*, int*, int*, int*);
extern Bool XFixesQueryExtension(Display*, int*, int*);
extern void XFixesSelectSelectionInput(Display*, Window, Atom, unsigned long);
typedef struct { int type; unsigned long serial; Bool send_event; Display* display; Window window; int subtype; Window owner; Atom selection; Time timestamp; Time selection_timestamp; } XFixesSelectionNotifyEvent;
#define XFixesSelectionNotify 0
#define XFixesSetSelectionOwnerNotifyMask 1L

static KeySym char_sym(int c) {
    switch (c) {
    case '\n': case '\r': return XK_Return;
    case '\b': case 127: return XK_BackSpace;
    case '\t': return XK_Tab;
    case 27: return XK_Escape;
    case 17: return XK_Left;
    case 18: return XK_Right;
    case 19: return XK_Up;
    case 20: return XK_Down;
    }
    if (c >= 32 && c < 127) return (KeySym)c;
    return NoSymbol;
}

static void send_key(Display* d, int c, int mods) {
    KeySym ks = char_sym(c);
    if (ks == NoSymbol) return;
    if ((mods & 1) && ks >= 'A' && ks <= 'Z') ks = ks - 'A' + 'a';
    KeyCode kc = XKeysymToKeycode(d, ks);
    if (!kc) return;
    int shift = XkbKeycodeToKeysym(d, kc, 0, 0) != ks && XkbKeycodeToKeysym(d, kc, 0, 1) == ks;
    KeyCode sh = XKeysymToKeycode(d, XK_Shift_L), ct = XKeysymToKeycode(d, XK_Control_L), al = XKeysymToKeycode(d, XK_Alt_L);
    if ((mods & 1) && ct) XTestFakeKeyEvent(d, ct, True, 0);
    if ((mods & 2) && al) XTestFakeKeyEvent(d, al, True, 0);
    if (shift) XTestFakeKeyEvent(d, sh, True, 0);
    XTestFakeKeyEvent(d, kc, True, 0);
    XTestFakeKeyEvent(d, kc, False, 0);
    if (shift) XTestFakeKeyEvent(d, sh, False, 0);
    if ((mods & 2) && al) XTestFakeKeyEvent(d, al, False, 0);
    if ((mods & 1) && ct) XTestFakeKeyEvent(d, ct, False, 0);
}

static Atom A_CLIP, A_UTF8, A_TARGETS, A_TEXT, A_PROP;
static Window cw;
static char* cbuf; static int clen, cown, cfixes_ev = -1;
static void clip_own(Display* d, int cf) {
    char* b = malloc(65537); if (!b) return;
    int n = (int)read(cf, b, 65536); if (n <= 0) { free(b); return; }
    { char t[256]; while (read(cf, t, sizeof t) > 0) {} }
    free(cbuf); cbuf = b; clen = n; cbuf[n] = 0;
    XSetSelectionOwner(d, A_CLIP, cw, CurrentTime); cown = XGetSelectionOwner(d, A_CLIP) == cw; XFlush(d);
}
static void clip_request(Display* d, XSelectionRequestEvent* r) {
    XSelectionEvent e; memset(&e, 0, sizeof e); e.type = SelectionNotify; e.display = d; e.requestor = r->requestor; e.selection = r->selection; e.target = r->target; e.time = r->time; e.property = None;
    Atom prop = r->property != None ? r->property : r->target;
    if (cown && cbuf && r->selection == A_CLIP) {
        if (r->target == A_TARGETS) { Atom t[4] = { A_TARGETS, A_UTF8, XA_STRING, A_TEXT }; XChangeProperty(d, r->requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char*)t, 4); e.property = prop; }
        else if (r->target == A_UTF8 || r->target == XA_STRING || r->target == A_TEXT) { XChangeProperty(d, r->requestor, prop, r->target == A_TEXT ? A_UTF8 : r->target, 8, PropModeReplace, (unsigned char*)cbuf, clen); e.property = prop; }
    }
    XSendEvent(d, r->requestor, False, 0, (XEvent*)&e); XFlush(d);
}
static void clip_got(Display* d, int cf) {
    Atom at; int fmt; unsigned long n, left; unsigned char* data = NULL;
    if (XGetWindowProperty(d, cw, A_PROP, 0, 65536 / 4, True, AnyPropertyType, &at, &fmt, &n, &left, &data) == Success && data) {
        if (fmt == 8 && n > 0 && cf >= 0) { if (write(cf, data, n) < 0) {} }
        XFree(data);
    }
}
static void clip_event(Display* d, XEvent* ev, int cf) {
    if (ev->type == SelectionRequest) clip_request(d, &ev->xselectionrequest);
    else if (ev->type == SelectionClear && ev->xselectionclear.selection == A_CLIP) cown = 0;
    else if (ev->type == SelectionNotify && ev->xselection.requestor == cw && ev->xselection.property != None) clip_got(d, cf);
    else if (cfixes_ev >= 0 && ev->type == cfixes_ev + XFixesSelectionNotify) {
        XFixesSelectionNotifyEvent* f = (XFixesSelectionNotifyEvent*)ev;
        if (f->selection == A_CLIP && f->owner != None && f->owner != cw) XConvertSelection(d, A_CLIP, A_UTF8, A_PROP, cw, f->selection_timestamp);
    }
}
int main(void) {
    int fd = open("/dev/nvxxin", O_RDONLY);
    if (fd < 0) { perror("nvx-xinput: /dev/nvxxin"); return 1; }
    Display* d = NULL;
    for (int i = 0; i < 300 && !d; i++) { d = XOpenDisplay(NULL); if (!d) usleep(200000); }
    if (!d) { fprintf(stderr, "nvx-xinput: cannot open display\n"); return 1; }
    int a, b, c2, e;
    if (!XTestQueryExtension(d, &a, &b, &c2, &e)) { fprintf(stderr, "nvx-xinput: XTEST extension missing\n"); return 1; }
    int scr = DefaultScreen(d);
    int cf = open("/dev/nvxclip", O_RDWR | O_NONBLOCK);
    A_CLIP = XInternAtom(d, "CLIPBOARD", False); A_UTF8 = XInternAtom(d, "UTF8_STRING", False); A_TARGETS = XInternAtom(d, "TARGETS", False); A_TEXT = XInternAtom(d, "TEXT", False); A_PROP = XInternAtom(d, "NVX_CLIP", False);
    cw = XCreateSimpleWindow(d, RootWindow(d, scr), -10, -10, 1, 1, 0, 0, 0);
    { int fe, ferr; if (cf >= 0 && XFixesQueryExtension(d, &fe, &ferr)) { cfixes_ev = fe; XFixesSelectSelectionInput(d, RootWindow(d, scr), A_CLIP, XFixesSetSelectionOwnerNotifyMask); } }
    XFlush(d);
    for (;;) {
        struct pollfd p[3] = { { fd, POLLIN, 0 }, { ConnectionNumber(d), POLLIN, 0 }, { cf, POLLIN, 0 } };
        poll(p, cf >= 0 ? 3 : 2, 250);
        if (cf >= 0 && (p[2].revents & POLLIN)) clip_own(d, cf);
        unsigned char buf[8 * 64];
        int n = (int)read(fd, buf, sizeof buf);
        int pm = 0, px = 0, py = 0;
        for (int i = 0; i + 8 <= n; i += 8) {
            unsigned char* r = buf + i;
            int x = (short)(r[4] | (r[5] << 8)), y = (short)(r[6] | (r[7] << 8));
            if (r[0] == 1) { pm = 1; px = x; py = y; continue; }
            if (pm) { XTestFakeMotionEvent(d, scr, px, py, 0); pm = 0; }
            if (r[0] == 2) { XTestFakeMotionEvent(d, scr, x, y, 0); XTestFakeButtonEvent(d, r[1], r[2] ? True : False, 0); }
            else if (r[0] == 3) send_key(d, r[1], r[3]);
            else if (r[0] == 4) { KeyCode ct = XKeysymToKeycode(d, XK_Control_L); XTestFakeMotionEvent(d, scr, x, y, 0); if (ct) XTestFakeKeyEvent(d, ct, True, 0); XTestFakeButtonEvent(d, r[1], True, 0); XTestFakeButtonEvent(d, r[1], False, 0); if (ct) XTestFakeKeyEvent(d, ct, False, 0); }
        }
        if (pm) XTestFakeMotionEvent(d, scr, px, py, 0);
        if (n > 0) XFlush(d);
        while (XPending(d)) { XEvent ev; XNextEvent(d, &ev); clip_event(d, &ev, cf); }
    }
}
