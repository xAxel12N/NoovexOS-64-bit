#include "user.h"
static void puti(const char* pre,int v){ char m[48]; int k=0; while(pre[k]){m[k]=pre[k];k++;} char t[12]; int ti=0; int vv=v; if(vv<0){m[k++]='-';vv=-vv;} if(vv==0)t[ti++]='0'; while(vv){t[ti++]=(char)('0'+vv%10);vv/=10;} while(ti)m[k++]=t[--ti]; m[k++]='\n'; m[k]=0; sys_write(m); }
int main(void){
    char id = *(volatile char*)0x40000050000ULL;
    int shmid = sys_shm(42, 4096);
    volatile int* p = (volatile int*)sys_shmmap(shmid);
    if(id=='1'){
        sys_write("[P1] writing 12345 to shared memory, sending a message on queue 0\n");
        p[0] = 12345;
        sys_msgsend(0, "hello from P1", 14);
        for(volatile long d=0;d<20000000;d++){ }
    } else {
        int tries=0; while(p[0]==0 && tries<80){ for(volatile long d=0;d<3000000;d++){} tries++; }
        puti("[P2] read shared value = ", p[0]);
        char rb[64]; int rl=sys_msgrecv(0,rb,63);
        if(rl>0){ rb[rl]=0; sys_write("[P2] received message: "); sys_write(rb); sys_write("\n"); }
        else sys_write("[P2] no message received\n");
    }
    return 0;
}
