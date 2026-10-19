#include "user.h"
int main(void){
    char id = *(volatile char*)0x40000050000ULL;
    for(int i=0;i<8;i++){
        char b[8]; b[0]='P'; b[1]=id; b[2]=':'; b[3]=(char)('0'+i); b[4]=' '; b[5]=0;
        sys_write(b);
        for(volatile long d=0;d<12000000;d++){ }
    }
    return 0;
}
