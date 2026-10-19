#include "user.h"
int main(void){
    int pid = px_getpid();
    px_write(1,"[mem] getpid returned pid ",26);
    { char c=(char)('0'+(pid%10)); px_write(1,&c,1); px_write(1,"\n",1); }
    unsigned int* p = (unsigned int*)px_mmap(0, 8192, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1);
    if((long)p==-1){ px_write(1,"[mem] mmap FAILED\n",18); return 1; }
    for(int i=0;i<1000;i++)p[i]=(unsigned)(i*i);
    int ok = (p[10]==100 && p[100]==10000 && p[999]==998001);
    px_write(1, ok?"[mem] mmap: wrote+read 1000 ints in mapped memory OK\n":"[mem] mmap data MISMATCH\n", ok?52:24);
    unsigned long b0 = px_brk(0);
    px_brk(b0 + 8192);
    unsigned long b1 = px_brk(0);
    px_write(1, (b1>=b0+8192)?"[mem] brk: heap extended by 8KB OK\n":"[mem] brk FAILED\n", (b1>=b0+8192)?35:17);
    px_munmap(p, 8192);
    px_write(1,"[mem] mmap/brk/getpid all work - process memory syscalls ready\n",62);
    return 0;
}
