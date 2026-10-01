#include "posix.h"
int main(int argc,char** argv){
    int n=argc>1?atoi(argv[1]):10; int ms=argc>2?atoi(argv[2]):500;
    for(int i=1;i<=n;i++){ printf("%d\n",i); usleep((unsigned)ms*1000); }
    return 0;
}
