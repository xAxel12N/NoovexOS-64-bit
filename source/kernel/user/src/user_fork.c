#include "user.h"
int main(void){
    sys_write("parent: about to fork\n");
    long pid = fork();
    if(pid==0) sys_write("child:  hello! fork() returned 0 in me\n");
    else       sys_write("parent: fork() gave me a child process\n");
    return 0;
}
