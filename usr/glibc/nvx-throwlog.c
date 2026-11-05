#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
typedef void (*throw_fn)(void*, void*, void (*)(void*));
static int g_count;
static char g_last[1900];
static void put_name(char* out, size_t cap, const char* m) {
    size_t o = 0; const char* p = m;
    if (*p == 'N') p++;
    while (*p >= '0' && *p <= '9' && o + 2 < cap) {
        int n = 0; while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
        if (o) { out[o++] = ':'; out[o++] = ':'; }
        for (int i = 0; i < n && *p && o + 1 < cap; i++) out[o++] = *p++;
    }
    if (!o) { strncpy(out, m, cap - 1); out[cap - 1] = 0; return; }
    out[o] = 0;
}
static void put_ustr(char* out, size_t cap, const void* pdata) {
    size_t o = 0;
    if (pdata) {
        const int32_t* h = (const int32_t*)pdata; int32_t len = h[1];
        const uint16_t* b = (const uint16_t*)(h + 2);
        if (len < 0 || len > 4000) len = 0;
        for (int32_t i = 0; i < len && o + 4 < cap; i++) {
            uint16_t c = b[i];
            if (c < 0x80) out[o++] = (c >= 32 || c == '\t') ? (char)c : ' ';
            else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 63)); }
            else { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
        }
    }
    out[o] = 0;
}
void __cxa_throw(void* obj, void* tinfo, void (*dest)(void*)) {
    static throw_fn real;
    if (!real) real = (throw_fn)dlsym(RTLD_NEXT, "__cxa_throw");
    const char* m = tinfo ? ((const char* const*)tinfo)[1] : 0;
    if (m && *m == '*') m++;
    if (m && strstr(m, "3com3sun4star") && g_count < 300) {
        g_count++;
        char name[256], msg[1500];
        put_name(name, sizeof name, m);
        put_ustr(msg, sizeof msg, obj ? *(void* const*)obj : 0);
        char line[1900];
        int n = snprintf(line, sizeof line, "nvx-throwlog: %s: %s\n", name, msg[0] ? msg : "(no message)");
        if (n > 0 && strcmp(line, g_last)) { if (n > (int)sizeof line - 1) n = (int)sizeof line - 1; ssize_t w = write(2, line, (size_t)n); (void)w; memcpy(g_last, line, sizeof g_last); }
    }
    real(obj, tinfo, dest);
    abort();
}
