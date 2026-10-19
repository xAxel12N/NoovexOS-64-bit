#include "user.h"
int main(void){
    sys_write("Hello from an ELF loaded off the disk!\n");
    sys_write("This program was NOT compiled into the kernel.\n");
    for(int i=1;i<=5;i++){
        char b[32]; int n=0;
        const char* pre="  count "; while(pre[n]){b[n]=pre[n];n++;}
        b[n++]='0'+i; b[n++]='\n'; b[n]=0;
        sys_write(b);
    }
    sys_write("Goodbye from userspace.\n");
    return 0;
}
