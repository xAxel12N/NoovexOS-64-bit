#include <stdio.h>
#include <string.h>
typedef unsigned int u32;
#define NVX_APPS_SVCUNITS_H
int svcu_parse(const char*, int, char*, int, char*, int, char*, int, char*, int, char*, int, char*, int, int*, int*, int*, int*, int*);
int svcu_split_args(const char*, char*, int);
u32 svcu_backoff(int, int);
#include "apps/system/svcunit_parse.c"
static int fails, n;
static void ck(int c, const char* m) { n++; if (!c) { fails++; printf("FAIL %s\n", m); } }
int main(void) {
    char d[80], e[256], u[33], w[128], env[512], a[128]; int ty, rs, rsec, ts, en;
    const char* t = "[Unit]\nDescription=SSH server  # trailing\nAfter=network cron\n\n[Service]\n  Type=oneshot\nExecStart=/usr/bin/dropbear -F -p 22\nUser=sshd\nWorkingDirectory=/var\nEnvironment=A=1 B=\"x y\"\nEnvironment=C=3 bogus\nRestart=on-failure\nRestartSec=999\nTimeoutStopSec=abc\n; c\n# c\n[Install]\nEnabled=yes\n";
    int r = svcu_parse(t, (int)strlen(t), d, 80, e, 256, u, 33, w, 128, env, 512, a, 128, &ty, &rs, &rsec, &ts, &en);
    ck(r == 0, "parse ok"); ck(!strcmp(d, "SSH server"), "desc + trailing comment"); ck(!strcmp(e, "/usr/bin/dropbear -F -p 22"), "exec");
    ck(!strcmp(u, "sshd"), "user"); ck(!strcmp(w, "/var"), "wd"); ck(!strcmp(env, "A=1\nB=x y\nC=3\n"), "env"); ck(!strcmp(a, "network cron"), "after");
    ck(ty == 1, "oneshot"); ck(rs == 1, "on-failure"); ck(rsec == 60, "rsec clamp"); ck(ts == 5, "bad tstop default"); ck(en == 1, "enabled");
    const char* t2 = "[Service]\nExecStart=relative/x\n";
    ck(svcu_parse(t2, (int)strlen(t2), d, 80, e, 256, u, 33, w, 128, env, 512, a, 128, &ty, &rs, &rsec, &ts, &en) < 0, "relative exec rejected");
    ck(!strcmp(u, "root") && !strcmp(w, "/") && rs == 0 && ty == 0 && en == 0, "defaults");
    ck(svcu_parse("", 0, d, 80, e, 256, u, 33, w, 128, env, 512, a, 128, &ty, &rs, &rsec, &ts, &en) < 0, "empty rejected");
    const char* t3 = "ExecStart=/bin/x\nRestart=always\nEnabled=no\n";
    ck(svcu_parse(t3, (int)strlen(t3), d, 80, e, 256, u, 33, w, 128, env, 512, a, 128, &ty, &rs, &rsec, &ts, &en) == 0 && rs == 2 && en == 0, "always/no");
    char o[64];
    ck(svcu_split_args("/bin/sh -c \"echo hi\"", o, 64) == 3 && !strcmp(o, "/bin/sh -c echo\x1fhi"), "quoted split");
    ck(svcu_split_args("  /a   b  ", o, 64) == 2 && !strcmp(o, "/a b"), "spaces");
    ck(svcu_split_args("/a \"open", o, 64) < 0, "unterminated quote");
    ck(svcu_split_args("", o, 64) < 0, "empty");
    ck(svcu_split_args("/aaaaaaaaaaaaaaaaaaaaaaaa", o, 8) < 0, "overflow");
    ck(svcu_backoff(1, 0) == 100 && svcu_backoff(1, 3) == 800 && svcu_backoff(5, 10) == 6000 && svcu_backoff(0, -1) == 100, "backoff");
    char big[3000]; for (int i = 0; i < 2999; i++) big[i] = 'x'; big[2999] = 0; memcpy(big, "ExecStart=/", 11);
    ck(svcu_parse(big, 3000, d, 80, e, 256, u, 33, w, 128, env, 512, a, 128, &ty, &rs, &rsec, &ts, &en) == 0 && strlen(e) == 255, "long line truncated");
    printf("%d checks\n", n);
    if (!fails) printf("SVC all ok\n");
    return fails != 0;
}
