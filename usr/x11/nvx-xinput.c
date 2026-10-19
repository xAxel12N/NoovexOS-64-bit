#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

extern int XTestFakeKeyEvent(Display*, unsigned int, Bool, unsigned long);
extern int XTestFakeButtonEvent(Display*, unsigned int, Bool, unsigned long);
extern int XTestFakeMotionEvent(Display*, int, int, int, unsigned long);
extern Bool XTestQueryExtension(Display*, int*, int*, int*, int*);

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

int main(void) {
    int fd = open("/dev/nvxxin", O_RDONLY);
    if (fd < 0) { perror("nvx-xinput: /dev/nvxxin"); return 1; }
    Display* d = NULL;
    for (int i = 0; i < 300 && !d; i++) { d = XOpenDisplay(NULL); if (!d) usleep(200000); }
    if (!d) { fprintf(stderr, "nvx-xinput: cannot open display\n"); return 1; }
    int a, b, c2, e;
    if (!XTestQueryExtension(d, &a, &b, &c2, &e)) { fprintf(stderr, "nvx-xinput: XTEST extension missing\n"); return 1; }
    int scr = DefaultScreen(d);
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        poll(&p, 1, 8);
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
        }
        if (pm) XTestFakeMotionEvent(d, scr, px, py, 0);
        if (n > 0) XFlush(d);
        while (XPending(d)) { XEvent ev; XNextEvent(d, &ev); }
    }
}
