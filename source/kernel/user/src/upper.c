#include "posix.h"
int main(void){
    char b[256]; long n;
    while((n=read(0,b,sizeof(b)))>0){ for(long i=0;i<n;i++){ if(b[i]>='a'&&b[i]<='z')b[i]-=32; } write(1,b,(size_t)n); }
    return 0;
}
