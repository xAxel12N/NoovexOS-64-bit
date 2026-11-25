#include <stdio.h>
#include "hd_ext.c"
static int bad;
#define CK(c,m) do{ if(c)printf("ok %s\n",m); else { printf("FAIL %s\n",m); bad++; } }while(0)
int main(void){
    u64 s; int a,b,c;
    hd_cr4_bits(0,0,1,&s,&a,&b,&c); CK(s==0&&a==HD_NOCPU&&b==HD_NOCPU&&c==HD_NOCPU,"old CPU: no CR4 bits set, all reported as not on this CPU");
    hd_cr4_bits(1u<<7,0,0,&s,&a,&b,&c); CK(s==(1ULL<<20)&&a==HD_ON&&b==HD_NOCPU,"SMEP only: CR4 bit 20");
    hd_cr4_bits((1u<<7)|(1u<<20),1u<<2,0,&s,&a,&b,&c); CK(s==((1ULL<<20)|(1ULL<<11))&&b==HD_OFF&&c==HD_ON,"SMAP present but not wanted: bit 21 stays clear, UMIP bit 11 set");
    hd_cr4_bits((1u<<7)|(1u<<20),1u<<2,1,&s,&a,&b,&c); CK(s==((1ULL<<20)|(1ULL<<21)|(1ULL<<11))&&b==HD_ON,"SMAP wanted: bits 20,21,11");
    hd_cr4_bits(1u<<20,0,1,&s,&a,&b,&c); CK(s==(1ULL<<21)&&a==HD_NOCPU,"SMAP without SMEP");
    int lowok=1, nz=1, dup=0; u64 prev=0;
    for(u64 i=0;i<100000;i++){ u64 g=hd_guard_mix(0x00E4B79A5C3D2F17ULL,0,i*977+13); if(g&0xFF)lowok=0; if(!(g>>8))nz=0; if(g==prev)dup++; prev=g; }
    CK(lowok,"guard low byte is zero (stops string copies leaking it)"); CK(nz,"guard never zero above the low byte"); CK(dup==0,"TSC-only guard changes with the clock");
    CK(hd_guard_mix(0,0,0)>>8,"all-zero input still gives a non-zero guard");
    CK(hd_guard_mix(1,0x1234,5)!=hd_guard_mix(1,0x1235,5),"RDRAND/RDSEED word changes the guard");
    if(!bad){ printf("HARDEN all ok\n"); } return bad!=0; }
