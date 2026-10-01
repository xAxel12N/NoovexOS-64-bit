#include "posix.h"
static int icmp(const void* a,const void* b){ return *(const int*)a - *(const int*)b; }
int main(void){
    char s[64]="hello,world,foo";
    printf("strrchr: %s\n", strrchr(s,','));
    printf("strspn(abc, in 'aabbcc..'): %lu\n", (unsigned long)strspn("aabZ","ab"));
    char dst[32]="ab"; strncat(dst,"cdef",2); printf("strncat: %s\n", dst);
    int arr[5]={5,2,8,1,9}; qsort(arr,5,sizeof(int),icmp);
    printf("sorted: %d %d %d %d %d\n", arr[0],arr[1],arr[2],arr[3],arr[4]);
    int key=8; int* f=(int*)bsearch(&key,arr,5,sizeof(int),icmp); printf("bsearch 8 -> %s\n", f?"found":"no");
    srand(42); printf("rand: %d %d\n", rand2(), rand2());
    printf("atof: %f  atol: %ld\n", atof("3.14"), atol("-1234"));
    printf("ctype: upper(a)=%d alnum(5)=%d xdigit(F)=%d\n", isupper('a'), isalnum('5'), isxdigit('F'));
    return 0;
}
