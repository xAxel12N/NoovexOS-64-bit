#define CRON_NO_MAIN
#include "../../../usr/musl/cron.c"

static int fails, checks;
#define CK(c) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static struct tm mk(int y, int mo, int d, int h, int mi)
{
    struct tm t = {0}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
    time_t x = mktime(&t); localtime_r(&x, &t); return t;
}

static int sc(const char* s, sched_t* o) { char b[256]; snprintf(b, sizeof b, "%s", s); char* p = b; return cron_sched(&p, o); }

static int fired_n; static char fired_cmd[16][64];
static void fire(job_t* j) { if (fired_n < 16) snprintf(fired_cmd[fired_n], 64, "%s", j->cmd); fired_n++; }

static int loadstr(const char* txt, int sys)
{
    jobs_reset(); FILE* f = fmemopen((void*)txt, strlen(txt), "r"); int bad = 0;
    int e = cron_parse(f, sys, add_job, (void*)"root", &bad); fclose(f); return e ? -bad : njobs;
}

int main(void)
{
    setenv("TZ", "UTC", 1); tzset();
    unsigned long long b; sched_t s;
    CK(!cron_field("*", 0, 59, 0, &b) && b == (1ULL << 60) - 1);
    CK(!cron_field("*/15", 0, 59, 0, &b) && b == (1ULL | 1ULL << 15 | 1ULL << 30 | 1ULL << 45));
    CK(!cron_field("1-5/2,10", 0, 59, 0, &b) && b == (1ULL << 1 | 1ULL << 3 | 1ULL << 5 | 1ULL << 10));
    CK(!cron_field("50/5", 0, 59, 0, &b) && b == (1ULL << 50 | 1ULL << 55));
    CK(!cron_field("jan-mar", 1, 12, mons, &b) && b == (1ULL << 1 | 1ULL << 2 | 1ULL << 3));
    CK(!cron_field("MON,fri", 0, 7, dows, &b) && b == (1ULL << 1 | 1ULL << 5));
    CK(cron_field("60", 0, 59, 0, &b)); CK(cron_field("5-1", 0, 59, 0, &b)); CK(cron_field("*/0", 0, 59, 0, &b));
    CK(cron_field("", 0, 59, 0, &b)); CK(cron_field("1,", 0, 59, 0, &b)); CK(cron_field("0", 1, 31, 0, &b)); CK(cron_field("x", 0, 59, 0, &b));
    CK(cron_field("99999999999999999999", 0, 59, 0, &b)); CK(cron_field("januar", 1, 12, mons, &b)); CK(cron_field("13", 1, 12, mons, &b));
    CK(!sc("0 0 * * 7 x", &s) && (s.dow & 1) && !(s.dow & 0x80));
    struct tm sun = mk(2026, 11, 15, 0, 0), mon = mk(2026, 11, 16, 0, 0), first = mk(2026, 12, 1, 0, 0);
    CK(sun.tm_wday == 0 && mon.tm_wday == 1);
    CK(cron_match(&s, &sun)); CK(!cron_match(&s, &mon));
    CK(!sc("0 0 1 * 1 x", &s)); CK(cron_match(&s, &mon)); CK(cron_match(&s, &first)); CK(!cron_match(&s, &sun));
    CK(!sc("0 0 * * 1 x", &s)); CK(cron_match(&s, &mon)); CK(!cron_match(&s, &first));
    CK(!sc("0 0 1 * * x", &s)); CK(!cron_match(&s, &mon)); CK(cron_match(&s, &first));
    CK(!sc("@daily x", &s) && cron_match(&s, &mon)); { struct tm t = mk(2026, 11, 16, 0, 1); CK(!cron_match(&s, &t)); }
    CK(!sc("@hourly x", &s)); { struct tm t = mk(2026, 11, 16, 7, 0); CK(cron_match(&s, &t)); }
    CK(!sc("@weekly x", &s) && cron_match(&s, &sun) && !cron_match(&s, &mon));
    CK(!sc("@monthly x", &s) && cron_match(&s, &first));
    CK(!sc("@yearly x", &s)); { struct tm t = mk(2027, 1, 1, 0, 0); CK(cron_match(&s, &t)); CK(!cron_match(&s, &first)); }
    CK(!sc("@reboot x", &s) && (s.flags & F_REBOOT) && !cron_match(&s, &mon));
    CK(sc("@often x", &s)); CK(sc("1 2 3 4", &s)); CK(sc("1 2 3 4 funday x", &s));

    CK(loadstr("# c\n\nSHELL=/bin/sh\nFOO = \"a b\"\n*/5 * * * * echo hi\n@reboot echo boot\n", 0) == 2);
    CK(nenv == 2 && !strcmp(envs[0], "SHELL=/bin/sh") && !strcmp(envs[1], "FOO=a b"));
    CK(jobs[0].envn == 2 && !strcmp(jobs[0].cmd, "echo hi") && jobs[0].uid == 0);
    CK(loadstr("* * * * * root echo sys\n", 1) == 1 && !strcmp(jobs[0].cmd, "echo sys"));
    CK(loadstr("* * * * * nosuchuser_zz echo sys\n", 1) <= 0);
    CK(loadstr("ok\n* * * * *\n", 0) == -1);
    CK(loadstr("* * * * * a\n61 * * * * b\n", 0) == -2);
    { char big[6000]; memset(big, 'a', sizeof big); big[sizeof big - 1] = 0; memcpy(big, "* * * * * ", 10); char t[6100]; snprintf(t, sizeof t, "%s\n* * * * * ok\n", big); CK(loadstr(t, 0) == -1); }

    CK(loadstr("* * * * * every\n0 * * * * hourly\n30 2 * * * nightly\n", 0) == 3);
    time_t t0 = 1763337600;
    fired_n = 0; CK(cron_catchup(t0 - 60, t0, 0, fire) == 2 && fired_n == 2);
    fired_n = 0; cron_catchup(t0, t0 + 60, 0, fire); CK(fired_n == 1 && !strcmp(fired_cmd[0], "every"));
    fired_n = 0; cron_catchup(t0, t0 + 6 * 3600, 0, fire); CK(fired_n == 3);
    fired_n = 0; cron_catchup(t0, t0 + 30 * 86400, 0, fire); CK(fired_n == 3);
    { time_t n = t0 + 9000; fired_n = 0; cron_catchup(n - 60, n, n, fire); CK(fired_n == 1 && !strcmp(fired_cmd[0], "every")); fired_n = 0; cron_catchup(n - 60, n, 0, fire); CK(fired_n == 2); }
    fired_n = 0; cron_catchup(t0 + 60, t0, 0, fire); CK(fired_n == 0);

    { static char big[8192]; int o = 0; for (int i = 0; i < 70; i++) o += snprintf(big + o, sizeof big - o, "* * * * * job%d\n", i); CK(loadstr(big, 0) == -65); CK(njobs == 64); }
    { static char big[8192]; int o = 0; for (int i = 0; i < 70; i++) o += snprintf(big + o, sizeof big - o, "* * * * * root job%d\n", i); CK(loadstr(big, 1) == 70); }
    { static char big[8192]; int o = 0; for (int i = 0; i < 80; i++) o += snprintf(big + o, sizeof big - o, "V%d=x\n", i); o += snprintf(big + o, sizeof big - o, "* * * * * j\n"); CK(loadstr(big, 0) == 1 && jobs[0].envn == 64); }
    CK(okname("alice") && !okname(".tmp.x") && !okname("a/b") && !okname("") && !okname("../x"));
    jobs_reset();
    printf("%d checks, %d failed\n", checks, fails);
    if (!fails) puts("ALL PASS");
    return fails != 0;
}
