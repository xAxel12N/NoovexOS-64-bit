#include "posix.h"
int main(void){
    int fd[2]; if(pipe(fd)<0){ printf("pipe failed\n"); return 1; }
    pid_t c=fork();
    if(c<0){ printf("fork failed\n"); return 1; }
    if(c==0){ close(fd[0]); dup2(fd[1],1); close(fd[1]); printf("hello through a pipe, from child %d\n",getpid()); printf("second line\n"); return 7; }
    close(fd[1]);
    char buf[256]; long n, tot=0;
    while((n=read(fd[0],buf,sizeof(buf)))>0){ write(1,"  parent read: ",15); write(1,buf,(size_t)n); tot+=n; }
    close(fd[0]);
    int st=0; pid_t w=waitpid(c,&st,0);
    printf("  waitpid(%d) -> %d, exited=%d status=%d, %ld bytes\n",c,w,WIFEXITED(st),WEXITSTATUS(st),tot);
    int u=umask(077); printf("  umask was %03o, now 077\n",u);
    int cl=fcntl(1,F_GETFD); printf("  fd 1 close-on-exec=%d, pgid=%d sid=%d\n",cl,getpgrp(),getsid(0));
    return 0;
}
