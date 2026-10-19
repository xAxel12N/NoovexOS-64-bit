#include "posix.h"
static volatile int hits=0;
static void on_int(int s){ (void)s; hits++; printf("\n  caught SIGINT (%d of 3)\n",hits); }
static void on_usr1(int s){ (void)s; printf("  got SIGUSR1\n"); }
int main(void){
    signal(SIGINT,on_int); signal(SIGUSR1,on_usr1);
    printf("sigdemo pid %d: press Ctrl-C three times, or 'kill -USR1 %d' from another job\n",getpid(),getpid());
    while(hits<3){ printf("."); usleep(300000); }
    printf("\nsigdemo: done\n");
    return 0;
}
