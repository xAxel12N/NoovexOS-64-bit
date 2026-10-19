#include "posix.h"
int main(void){
    printf("exp(1)=%f (e=2.718)\n", exp(1.0));
    printf("log(e)=%f (1.0)\n", log(2.718281828));
    printf("tan(pi/4)=%f (1.0)\n", tan(M_PI/4));
    printf("atan2(1,1)=%f (0.785)\n", atan2(1.0,1.0));
    printf("hypot(3,4)=%f (5.0)\n", hypot(3.0,4.0));
    printf("strcasecmp(Hi,hi)=%d\n", strcasecmp("Hi","hi"));
    printf("strtoul(ff,16)=%lu\n", strtoul("ff",0,16));
    printf("fmax(2,7)=%f fmin=%f\n", fmax(2,7), fmin(2,7));
    return 0;
}
