#include "posix.h"
int main(void){
    unsigned long n=0, last=time_ticks();
    for(;;){ n++; unsigned long t=time_ticks(); if(t-last>=1000){ printf("spin: %lu loops/s (pid %d)\n",n,getpid()); n=0; last=t; } }
    return 0;
}
