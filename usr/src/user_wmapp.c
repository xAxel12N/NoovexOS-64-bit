#include "user.h"
#define WM_CREATE 1
#define WM_DAMAGE 2
struct wmsg { int type,w,h,key,client,x,y,btn; };
int main(void){
    char id = *(volatile char*)0x40000050000ULL;
    if(id!='1') return 0;
    int WW=200, HH=120, KEY=100;
    int shmid = sys_shm(KEY, WW*HH*4);
    unsigned int* buf = (unsigned int*)sys_shmmap(shmid);
    for(int y=0;y<HH;y++) for(int x=0;x<WW;x++){
        unsigned int r=(unsigned)(x*255/WW), g=(unsigned)(y*255/HH);
        buf[y*WW+x]=(r<<16)|(g<<8)|150u;
    }
    for(int y=34;y<80;y++) for(int x=50;x<150;x++) buf[y*WW+x]=0xF0F0FFu;
    for(int y=44;y<70;y++) for(int x=60;x<140;x++) buf[y*WW+x]=0x2050C0u;
    struct wmsg m; m.type=WM_CREATE; m.w=WW; m.h=HH; m.key=KEY; m.client=1; m.x=0; m.y=0; m.btn=0;
    sys_msgsend(0,&m,sizeof(m));
    struct wmsg d; d.type=WM_DAMAGE; d.w=0; d.h=0; d.key=KEY; d.client=1; d.x=0; d.y=0; d.btn=0;
    sys_msgsend(0,&d,sizeof(d));
    sys_write("[app] created a 200x120 window via the protocol, drew a gradient into shared memory\n");
    for(volatile long dd=0;dd<25000000;dd++){ }
    return 0;
}
