#include "posix.h"
int main(void){
    char b[32]; itoa(12345,b,10); printf("itoa: %s\n", b);
    printf("gcd(48,36)=%d lcm=%ld\n", gcd(48,36), lcm(48,36));
    printf("isqrt(144)=%d ilog2(1024)=%d popcount(255)=%d\n", isqrt(144), ilog2(1024), popcount(255));
    printf("startswith(hello,he)=%d endswith(hello,lo)=%d\n", startswith("hello","he"), endswith("hello","lo"));
    char t[]="  trim me  "; printf("trim:[%s]\n", strtrim(t));
    time_t tt=1727380800; struct tm tm; gmtime_r(&tt,&tm); char ds[32]; strftime(ds,32,"%Y-%m-%d %H:%M",&tm); printf("strftime: %s\n", ds);
    printf("deg2rad(180)=%f clampi(15,0,10)=%d\n", deg2rad(180.0), clampi(15,0,10));
    printf("hexval(0xFF)=%d bswap16(0x1234)=%x\n", hexval("0xFF"), bswap16(0x1234));
    return 0;
}
