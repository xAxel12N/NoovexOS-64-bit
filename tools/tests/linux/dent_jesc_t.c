#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned long long u64;
#include "lx_dent_old.inc"
#include "nvx_jesc.inc"
static int fails;
static void ok(int c,const char* m){ printf("%s %s\n",c?"ok":"FAIL",m); if(!c)fails++; }
static int put(u8* d,int o,const char* nm,u8 ty){ int L=(int)strlen(nm); int rl=(19+L+1+7)&~7; memset(d+o,0,rl); *(u16*)(d+o+16)=(u16)rl; d[o+18]=ty; memcpy(d+o+19,nm,L); return rl; }
int main(void){
    u8 b[256]; memset(b,0xAA,sizeof b); int n=0; n+=put(b,n,"a",4); n+=put(b,n,"hello.txt",8); n+=put(b,n,"abcde",8);
    lx_dent_old(b,n);
    ok(!strcmp((char*)b+18,"a")&&b[*(u16*)(b+16)-1]==4,"old dirent: first name moved and type at end");
    int o=*(u16*)(b+16); ok(!strcmp((char*)b+o+18,"hello.txt")&&b[o+*(u16*)(b+o+16)-1]==8,"old dirent: second record");
    u8 c[64]; memset(c,0,sizeof c); put(c,0,"x",4); *(u16*)(c+16)=0; u8 g=0x55; c[63]=g; lx_dent_old(c,32); ok(c[63]==g,"reclen 0 stops (no endless loop)");
    memset(c,0,sizeof c); put(c,0,"x",4); *(u16*)(c+16)=60000; lx_dent_old(c,32); ok(1,"reclen past end stops (ASan clean)");
    memset(c,'z',sizeof c); *(u16*)(c+16)=24; c[18]=8; lx_dent_old(c,24); ok(c[23]==8&&c[22]==0,"unterminated name is bounded by reclen");
    char out[1024]; char* e=nvx_jesc(out,"a\"b\\c\nd\x01"); *e=0; ok(!strcmp(out,"a\\\"b\\\\c\\u000ad\\u0001"),"json escape of quote, backslash and control bytes");
    char big[200]; memset(big,'"',199); big[199]=0; e=nvx_jesc(out,big); ok(e-out==128,"json escape stops after 64 input bytes");
    if(!fails)printf("DENT_JESC all ok\n"); return fails!=0;
}
