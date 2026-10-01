#include "posix.h"
int main(void){
    char name[80]; printf("What's your name? ");
    long n=read(0,name,sizeof(name)-1); if(n<=0){ printf("\n(no input)\n"); return 1; }
    name[n]=0; if(name[n-1]=='\n')name[n-1]=0;
    printf("Hello, %s! I am pid %d in process group %d.\n",name,getpid(),getpgrp());
    return 0;
}
