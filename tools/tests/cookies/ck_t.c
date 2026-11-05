#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net/http/cookies.c"
FsFile FS[FS_N];
static u64 g_now = 1800000000000ull;
u64 k_realtime_ms(void) { return g_now; }
void* kmalloc(unsigned long n) { return malloc(n); }
void kfree(void* p) { free(p); }
char* fsd(int k) { return FS[k].data; }
void acct_profile_name(const char* base, char* out, int cap) { strncpy(out, base, (size_t)cap - 1); out[cap - 1] = 0; }
int fs_save_bin(const char* name, const u8* data, int len, int folder) { (void)folder; FsFile* f = &FS[0]; strcpy(f->name, name); free(f->data); f->data = malloc(len + 1); memcpy(f->data, data, len); f->len = len; f->used = 1; return 0; }
static int fails;
#define OK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); fails++; } else printf("ok   %s\n", m); } while (0)
static char hdr[20000];
int main(void) {
    static char line[6000], val[5000];
    for (int i = 0; i < 3000; i++) val[i] = 'a' + i % 26; val[3000] = 0;
    snprintf(line, sizeof line, "BIG=%s; Domain=.example.com; Path=/; Secure; Max-Age=3600", val);
    cookie_set("https://accounts.example.com/x", line, 0);
    cookie_header("https://mail.example.com/", 0, hdr, 16384);
    OK(strlen(hdr) == 4 + 3000 && !memcmp(hdr + 4, val, 3000), "3000-byte cookie kept whole");
    for (int i = 0; i < 4100; i++) val[i] = 'z'; val[4100] = 0;
    snprintf(line, sizeof line, "HUGE=%s", val);
    cookie_set("https://accounts.example.com/", line, 0);
    cookie_header("https://accounts.example.com/", 0, hdr, 16384);
    OK(!strstr(hdr, "HUGE="), "over 4096 bytes rejected like Chrome");
    snprintf(line, sizeof line, "BIG=short; Domain=.example.com; Path=/; Max-Age=3600");
    cookie_set("https://accounts.example.com/x", line, 0);
    cookie_header("https://mail.example.com/", 0, hdr, 16384);
    OK(!strcmp(hdr, "BIG=short"), "replace shrinks value");
    for (int i = 0; i < 40; i++) { for (int j = 0; j < 300; j++) val[j] = 'A' + (i + j) % 26; val[300] = 0; snprintf(line, sizeof line, "c%02d=%s; Max-Age=3600", i, val); cookie_set("https://g.test/", line, 0); }
    int hl = cookie_header("https://g.test/", 0, hdr, 16384);
    int cnt = 0; for (char* p = hdr; (p = strstr(p, "=")); p++) cnt++;
    OK(hl > 12000 && cnt == 40, "40 x 300-byte cookies all sent (12 KB header)");
    for (int i = 0; i < 200; i++) { g_now++; snprintf(line, sizeof line, "s%03d=v%d; Max-Age=3600", i, i); cookie_set("https://many.test/", line, 0); }
    cookie_header("https://many.test/", 0, hdr, 16384);
    cnt = 0; for (char* p = hdr; (p = strstr(p, "=")); p++) cnt++;
    OK(cnt == 180 && strstr(hdr, "s199=v199") && !strstr(hdr, "s000="), "180 per site, oldest removed first");
    hl = cookie_header("https://g.test/", 0, hdr, 16384);
    OK(hl > 12000, "other site untouched by per-site cap");
    for (int i = 0; i < 3500; i++) { g_now++; snprintf(line, sizeof line, "k=v%d; Max-Age=3600", i); char u[64]; snprintf(u, sizeof u, "https://s%d.test%d/", i, i); cookie_set(u, line, 0); }
    OK(cookie_count() <= CK_MAX && cookie_count() > 2900, "jar holds about 3000");
    cookie_header("https://s3499.test3499/", 0, hdr, 16384);
    OK(!strcmp(hdr, "k=v3499"), "newest cookie present after jar fills");
    cookies_flush(1);
    int before = cookie_count();
    g_ck_loaded = 0; for (int i = 0; i < CK_MAX; i++) g_ck[i].used = 0;
    ck_load();
    OK(cookie_count() == before, "save and reload keeps every cookie");
    for (int i = 0; i < 3500; i++) val[i] = 'q'; val[3500] = 0; snprintf(line, sizeof line, "BIG2=%s; Max-Age=3600", val);
    cookie_set("https://re.test/", line, 0); cookies_flush(1);
    g_ck_loaded = 0; for (int i = 0; i < CK_MAX; i++) g_ck[i].used = 0; ck_load();
    cookie_header("https://re.test/", 0, hdr, 16384);
    OK(strlen(hdr) == 5 + 3500, "long cookie survives reload");
    cookie_set("https://js.test/", "h=1; HttpOnly; Max-Age=60", 0);
    cookie_set("https://js.test/", "j=2", 1);
    cookie_header("https://js.test/", 1, hdr, 16384);
    OK(!strcmp(hdr, "j=2"), "HttpOnly hidden from scripts");
    cookie_clear();
    cookie_set("https://ss.test/", "st=1; SameSite=Strict; Max-Age=600", 0);
    cookie_set("https://ss.test/", "lx=2; SameSite=Lax; Max-Age=600", 0);
    cookie_set("https://ss.test/", "no=3; SameSite=None; Secure; Max-Age=600", 0);
    cookie_set("https://ss.test/", "un=4; Max-Age=600", 0);
    cookie_header2("https://ss.test/", 0, hdr, 16384, 1, 0, 1);
    OK(!strcmp(hdr, "no=3"), "cross-site subresource gets only SameSite=None");
    cookie_header2("https://ss.test/", 0, hdr, 16384, 1, 1, 1);
    OK(strstr(hdr, "lx=2") && strstr(hdr, "un=4") && strstr(hdr, "no=3") && !strstr(hdr, "st=1"), "cross-site top-level GET gets Lax, default and None");
    cookie_header2("https://ss.test/", 0, hdr, 16384, 1, 1, 0);
    OK(!strstr(hdr, "lx=2") && strstr(hdr, "un=4") && strstr(hdr, "no=3"), "cross-site top-level POST: None, and default-Lax cookies younger than 2 minutes");
    g_now += 200000;
    cookie_header2("https://ss.test/", 0, hdr, 16384, 1, 1, 0);
    OK(!strstr(hdr, "un=4") && strstr(hdr, "no=3"), "default-Lax cookie older than 2 minutes is not sent on a cross-site POST");
    cookie_header("https://ss.test/", 0, hdr, 16384);
    OK(strstr(hdr, "st=1") && strstr(hdr, "lx=2") && strstr(hdr, "un=4") && strstr(hdr, "no=3"), "same-site request gets all four");
    cookies_flush(1); g_ck_loaded = 0; for (int i = 0; i < CK_MAX; i++) g_ck[i].used = 0; ck_load();
    cookie_header2("https://ss.test/", 0, hdr, 16384, 1, 0, 1);
    OK(!strcmp(hdr, "no=3"), "SameSite survives save and reload");
    cookie_clear();
    OK(cookie_count() == 0, "clear empties jar");
    free(g_ck); free(FS[0].data);
    printf(fails ? "COOKIES FAIL %d\n" : "COOKIES all ok\n", fails);
    return fails != 0;
}
