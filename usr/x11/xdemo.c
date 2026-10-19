#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned long rgb(Display* d, int r, int g, int b) {
    XColor c; c.red = r * 257; c.green = g * 257; c.blue = b * 257; c.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(d, DefaultColormap(d, DefaultScreen(d)), &c);
    return c.pixel;
}

int main(void) {
    Display* d = NULL;
    for (int i = 0; i < 100 && !d; i++) { d = XOpenDisplay(NULL); if (!d) usleep(200000); }
    if (!d) { fprintf(stderr, "xdemo: cannot open display %s\n", XDisplayName(NULL)); return 1; }
    int s = DefaultScreen(d);
    Window w = XCreateSimpleWindow(d, RootWindow(d, s), 40, 40, 560, 360, 2, BlackPixel(d, s), rgb(d, 245, 246, 250));
    XStoreName(d, w, "xdemo");
    XSelectInput(d, w, ExposureMask | KeyPressMask | ButtonPressMask | PointerMotionMask);
    XMapWindow(d, w);
    GC gc = XCreateGC(d, w, 0, NULL);
    unsigned long cols[5] = { rgb(d, 230, 70, 70), rgb(d, 70, 170, 90), rgb(d, 60, 110, 220), rgb(d, 240, 190, 40), rgb(d, 30, 36, 52) };
    printf("xdemo: connected to %s, screen %dx%d, %d bits\n", XDisplayString(d), DisplayWidth(d, s), DisplayHeight(d, s), DefaultDepth(d, s));
    fflush(stdout);
    int clicks = 0, px = -1, py = -1, ndots = 0; int dx[64], dy[64]; char typed[64] = ""; int tn = 0, btnon = 0;
    for (int frame = 0; ; frame++) {
        while (XPending(d)) {
            XEvent e; XNextEvent(d, &e);
            if (e.type == MotionNotify) { px = e.xmotion.x; py = e.xmotion.y; }
            if (e.type == ButtonPress) { clicks++; px = e.xbutton.x; py = e.xbutton.y;
                if (px >= 400 && px < 540 && py >= 20 && py < 60) btnon = !btnon;
                else if (ndots < 64) { dx[ndots] = px; dy[ndots] = py; ndots++; } }
            if (e.type == KeyPress) { char kb[8]; KeySym ks; int k = XLookupString(&e.xkey, kb, sizeof kb, &ks, NULL);
                if (ks == XK_Escape) { XCloseDisplay(d); return 0; }
                if (ks == XK_BackSpace) { if (tn > 0) typed[--tn] = 0; }
                else if (k == 1 && kb[0] >= 32 && tn < 60) { typed[tn++] = kb[0]; typed[tn] = 0; } }
        }
        XSetForeground(d, gc, rgb(d, 245, 246, 250)); XFillRectangle(d, w, gc, 0, 0, 560, 360);
        for (int i = 0; i < 4; i++) { XSetForeground(d, gc, cols[i]); XFillRectangle(d, w, gc, 20 + i * 70, 20, 60, 60); }
        XSetForeground(d, gc, cols[4]);
        const char* t1 = "Hello from X11 on NoovexOS";
        XDrawString(d, w, gc, 20, 110, t1, (int)strlen(t1));
        char buf[96]; time_t now = time(NULL); struct tm tm; localtime_r(&now, &tm);
        snprintf(buf, sizeof buf, "Xlib client, frame %d, clicks %d, time %02d:%02d:%02d", frame, clicks, tm.tm_hour, tm.tm_min, tm.tm_sec);
        XDrawString(d, w, gc, 20, 130, buf, (int)strlen(buf));
        XSetForeground(d, gc, btnon ? cols[1] : cols[2]); XFillRectangle(d, w, gc, 400, 20, 140, 40);
        XSetForeground(d, gc, rgb(d, 255, 255, 255)); XDrawString(d, w, gc, 422, 44, btnon ? "Button: ON" : "Button: OFF", btnon ? 10 : 11);
        XSetForeground(d, gc, cols[4]);
        snprintf(buf, sizeof buf, "Pointer: %d,%d   Typed: %s_", px, py, typed);
        XDrawString(d, w, gc, 20, 150, buf, (int)strlen(buf));
        XDrawString(d, w, gc, 20, 340, "Click to drop dots, click the button, type text. Esc quits.", 59);
        XSetForeground(d, gc, cols[0]); for (int i = 0; i < ndots; i++) XFillArc(d, w, gc, dx[i] - 5, dy[i] - 5, 10, 10, 0, 360 * 64);
        int cx = 420, cy = 220, r = 90;
        XSetForeground(d, gc, cols[2]); XDrawArc(d, w, gc, cx - r, cy - r, 2 * r, 2 * r, 0, 360 * 64);
        double a = frame * 0.1;
        XSetForeground(d, gc, cols[0]); XDrawLine(d, w, gc, cx, cy, cx + (int)(r * 0.9 * cos(a)), cy + (int)(r * 0.9 * sin(a)));
        XSetForeground(d, gc, cols[1]);
        for (int x = 0; x < 300; x += 4) { int y = 260 + (int)(40 * sin((x + frame * 6) * 0.03)); XFillArc(d, w, gc, 20 + x, y, 6, 6, 0, 360 * 64); }
        XFlush(d);
        usleep(100000);
    }
}
