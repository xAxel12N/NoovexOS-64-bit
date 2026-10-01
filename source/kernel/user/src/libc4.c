#include "posix.h"
int main(void){
    char s[64]="hello world"; printf("strrev: %s\n", strrev(s));
    printf("memmem: %s\n", (char*)memmem("abcXYZdef",9,"XYZ",3));
    printf("cbrt(27)=%f tanh(0)=%f cosh(0)=%f\n", cbrt(27.0), tanh(0.0), cosh(0.0));
    printf("sinh(1)=%f exp2(10)=%f\n", sinh(1.0), exp2(10.0));
    char dst[16]; strlcpy(dst,"truncated string here",16); printf("strlcpy: %s\n", dst);
    printf("sysconf(pagesize)=%ld isatty(1)=%d\n", sysconf(30), isatty(1));
    return 0;
}
