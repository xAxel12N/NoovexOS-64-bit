#include "user.h"
static volatile int counter = 0;   /* shared across threads (same address space) */
static void worker(void){
    for(int i=0;i<3;i++){ counter++; sys_write("  worker: bumped shared counter\n"); yield(); }
}
int main(void){
    sys_write("main: creating 2 threads that share memory\n");
    thread_create(worker);
    thread_create(worker);
    for(int i=0;i<3;i++){ counter++; sys_write("main:   bumped shared counter\n"); yield(); }
    /* let workers finish */
    for(int i=0;i<6;i++) yield();
    if(counter==9) sys_write("main: counter == 9 -> shared memory across threads works!\n");
    else           sys_write("main: done\n");
    return 0;
}
