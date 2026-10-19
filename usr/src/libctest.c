#include "posix.h"
int main(void){
    /* scanf */
    int a,b; sscanf("12 34", "%d %d", &a, &b); printf("sscanf: %d + %d = %d\n", a, b, a+b);
    /* stdbool */
    bool ok = true; printf("bool: %d\n", ok);
    /* math + %f */
    printf("sqrt(2) = %f\n", sqrt(2.0));
    printf("pow(2,10) = %f\n", pow(2.0,10.0));
    printf("sin(pi/6) = %f\n", sin(M_PI/6));
    /* time */
    printf("clock = %ld\n", clock());
    /* limits + assert */
    assert(INT_MAX > 0);
    printf("all libc features work\n");
    return 0;
}
