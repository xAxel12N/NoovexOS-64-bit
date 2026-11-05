#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <math.h>
#include <time.h>
typedef int (*f_open)(void**, const char*, int, int);
typedef int (*f_setp)(void*, int, int, unsigned, unsigned, int, unsigned);
typedef long (*f_wr)(void*, const void*, unsigned long);
typedef int (*f_pcm)(void*);
typedef int (*f_rec)(void*, int, int);
typedef long (*f_rd)(void*, void*, unsigned long);
typedef const char* (*f_err)(int);
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char** argv) {
    const char* dev = argc > 1 ? argv[1] : "default"; int rate = argc > 2 ? atoi(argv[2]) : 44100; double secs = argc > 3 ? atof(argv[3]) : 1.0;
    void* h = dlopen("libasound.so.2", RTLD_NOW); if (!h) { printf("FAIL libasound.so.2 not found: %s\n", dlerror()); return 1; }
    f_open op = (f_open)dlsym(h, "snd_pcm_open"); f_setp sp = (f_setp)dlsym(h, "snd_pcm_set_params"); f_wr wr = (f_wr)dlsym(h, "snd_pcm_writei"); f_pcm dr = (f_pcm)dlsym(h, "snd_pcm_drain"); f_pcm cl = (f_pcm)dlsym(h, "snd_pcm_close"); f_rec rc = (f_rec)dlsym(h, "snd_pcm_recover"); f_rd rd = (f_rd)dlsym(h, "snd_pcm_readi"); f_err se = (f_err)dlsym(h, "snd_strerror");
    void* p = NULL; int e = op(&p, dev, 0, 0); if (e < 0) { printf("FAIL open %s: %s\n", dev, se(e)); return 1; }
    e = sp(p, 2, 3, 2, (unsigned)rate, 1, 200000); if (e < 0) { printf("FAIL set_params: %s\n", se(e)); return 1; }
    int n = (int)(rate * secs); short* buf = malloc((size_t)n * 4); for (int i = 0; i < n; i++) { short v = (short)(8000 * sin(2 * M_PI * 440.0 * i / rate)); buf[2 * i] = v; buf[2 * i + 1] = v; }
    double t0 = now(); int done = 0, calls = 0;
    while (done < n) { long w = wr(p, buf + 2 * done, (unsigned long)(n - done < 1024 ? n - done : 1024)); calls++; if (w < 0) { w = rc(p, (int)w, 0); if (w < 0) { printf("FAIL write: %s\n", se((int)w)); return 1; } continue; } done += (int)w; }
    double t1 = now(); dr(p); double t2 = now(); cl(p);
    printf("PASS playback %s: %d frames at %d Hz in %d writes, write %.2f s, drain %.2f s, total %.2f s (expected about %.2f s)\n", dev, n, rate, calls, t1 - t0, t2 - t1, t2 - t0, secs);
    void* c = NULL; e = op(&c, dev, 1, 0);
    if (e < 0) { printf("INFO no capture device: %s\n", se(e)); return 0; }
    e = sp(c, 2, 3, 2, 48000, 1, 200000); if (e < 0) { printf("INFO capture set_params: %s\n", se(e)); cl(c); return 0; }
    short rb[2048]; long got = 0; double c0 = now(); while (got < 4800 && now() - c0 < 3) { long r = rd(c, rb, 1024); if (r < 0) { r = rc(c, (int)r, 0); if (r < 0) break; continue; } got += r; }
    cl(c); printf("%s capture: %ld frames in %.2f s\n", got >= 4800 ? "PASS" : "INFO", got, now() - c0);
    return 0; }
