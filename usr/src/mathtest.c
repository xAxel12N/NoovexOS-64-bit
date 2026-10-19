#include "posix.h"
static jmp_buf jb;
static void boom(){ longjmp(jb,42); }
int main(void){
    printf("isnan(NAN)=%d isinf(1/0-ish)=%d\n", isnan(NAN), isinf(HUGE_VAL));
    printf("signbit(-3.0)=%d fpclassify(0.0)=%d\n", signbit(-3.0), fpclassify(0.0));
    int e; double m=frexp(8.0,&e); printf("frexp(8)=%f e=%d\n", m, e);
    printf("strerror(EACCES)=%s\n", strerror(EACCES));
    int r=setjmp(jb); if(r==0){ boom(); } else printf("longjmp returned %d\n", r);
    return 0;
}
