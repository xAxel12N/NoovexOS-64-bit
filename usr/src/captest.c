#include "posix.h"
static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
int main(void){
    struct termios t; tcgetattr(0,&t); cfmakeraw(&t); tcsetattr(0,TCSANOW,&t);
    printf("raw mode set\n");
    fd_set rd; FD_ZERO(&rd); FD_SET(0,&rd);
    struct timeval tv={0,0}; int r=select(1,&rd,0,0,&tv);
    printf("select stdin ready=%d\n", r);
    pthread_mutex_lock(&mx); printf("mutex locked\n"); pthread_mutex_unlock(&mx);
    printf("pthread_self=%lu\n", (unsigned long)pthread_self());
    void* h=dlopen("libfoo.so",RTLD_NOW); printf("dlopen -> %p (%s)\n", h, dlerror());
    return 0;
}
