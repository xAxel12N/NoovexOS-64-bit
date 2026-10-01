#include "user.h"
static int slen(const char* s){ int n=0; while(s[n])n++; return n; }
int main(void){
    const char* msg="POSIX fd layer works!";
    int fd = px_open("posixtest.txt", O_CREAT|O_TRUNC|O_RDWR);
    if(fd<0){ px_write(1,"open failed\n",12); return 1; }
    px_write(1,"[posix] opened fd = ",20);
    { char c=(char)('0'+fd); px_write(1,&c,1); px_write(1,"\n",1); }
    px_write(fd, msg, slen(msg));
    int sz = px_fstat(fd);
    px_write(1,"[posix] wrote + fstat size ok\n",30);
    px_lseek(fd, 0, SEEK_SET);
    char buf[64]; int n = px_read(fd, buf, 63); buf[n>0?n:0]=0;
    px_write(1,"[posix] read back via fd: ",26);
    px_write(1, buf, n); px_write(1,"\n",1);
    px_close(fd);
    px_write(1,"[posix] closed fd - open/write/lseek/read/close all work\n",56);
    (void)sz;
    return 0;
}
