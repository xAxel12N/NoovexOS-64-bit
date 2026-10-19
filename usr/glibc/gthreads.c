#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <gnu/libc-version.h>
static long total;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static void* work(void* a)
{
    long n = (long)a, s = 0;
    for (long i = 1; i <= 100000; i++) s += i % (n + 2);
    pthread_mutex_lock(&mu);
    total += s;
    pthread_mutex_unlock(&mu);
    return (void*)n;
}
int main(int argc, char** argv)
{
    struct utsname u;
    uname(&u);
    printf("glibc %s on %s %s\n", gnu_get_libc_version(), u.sysname, u.release);
    pthread_t t[4];
    for (long i = 0; i < 4; i++) pthread_create(&t[i], 0, work, (void*)i);
    long sum = 0;
    for (int i = 0; i < 4; i++) { void* r; pthread_join(t[i], &r); sum += (long)r; }
    char* p = malloc(1 << 20);
    memset(p, 7, 1 << 20);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    printf("threads ok: total=%ld joined=%ld sqrt2=%.6f malloc=%d pid=%d\n", total, sum, sqrt(2.0), p[12345], (int)getpid());
    free(p);
    return argc > 1 ? atoi(argv[1]) : 0;
}
