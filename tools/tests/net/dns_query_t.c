#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16;
static void be16(u8* p,u16 v){ p[0]=v>>8; p[1]=(u8)v; }
#include "dns_build_query.inc"
int main(void){
    u8 q[300]; int f=0; char big[80]; memset(big,'a',64); strcpy(big+64,".evil.com");
    if(dns_build_query("example.com",1,q,300)!=12+13+4){ puts("FAIL ok-name"); f=1; }
    if(dns_build_query("example.com.",1,q,300)!=12+13+4){ puts("FAIL trailing-dot"); f=1; }
    if(dns_build_query(big,1,q,300)!=-1){ puts("FAIL 64-byte label accepted"); f=1; }
    if(dns_build_query("a..b",1,q,300)!=-1){ puts("FAIL empty label accepted"); f=1; }
    if(dns_build_query(".a",1,q,300)!=-1){ puts("FAIL leading dot accepted"); f=1; }
    puts(f?"FAILED":"dns_query_t: all passed"); return f;
}
