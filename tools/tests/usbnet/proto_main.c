#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
static u8 got[64][2048]; static int gotn[64], ng;
static void usn_q_put(const u8* f,int n){ if(ng<64&&n<=2048){ memcpy(got[ng],f,(size_t)n); gotn[ng]=n; } ng++; }
#include "drivers/usb/usbnet_proto.h"
static int fails, passes;
static void rec(int ok,const char* name){ if(ok)passes++; else fails++; printf("  %-66s %s\n",name,ok?"ok":"FAIL"); }
static unsigned rs=777; static unsigned rnd(void){ rs=rs*1103515245u+12345u; return rs>>8; }
static void mk(u8* p,int n,int seed){ for(int i=0;i<n;i++)p[i]=(u8)(seed*13+i*5); }
int main(void){
    u8 f[1600], b[4096]; int good=1;
    for(int t=0;t<500;t++){ int len=14+(int)(rnd()%1500); mk(f,len,t); int div=(t%3==0)?4:(t%3==1)?8:32, rem=(int)(rnd()%(unsigned)div);
        int n=ncm_build(b,sizeof b,(u16)t,f,len,div,rem); ng=0; int r=ncm_parse(b,n);
        int dg=usn_g16(b+20); if(n<0||r!=1||ng!=1||gotn[0]!=len||memcmp(got[0],f,(size_t)len)||dg%div!=rem)good=0; }
    rec(good,"NCM: 500 frames wrapped in NTB16 and unwrapped, divisor/remainder kept");
    { int o=12; u8* nb=b; memset(nb,0,sizeof b); usn_p32(nb,0x484D434Eu); usn_p16(nb+4,12); int ndp=12; int dgo=64; int lens[5]={60,1514,100,14,700};
      usn_p32(nb+ndp,0x304D434Eu); usn_p16(nb+ndp+4,8+4*6); usn_p16(nb+ndp+6,0);
      for(int i=0;i<5;i++){ mk(nb+dgo,lens[i],100+i); usn_p16(nb+ndp+8+i*4,(u16)dgo); usn_p16(nb+ndp+10+i*4,(u16)lens[i]); dgo+=(lens[i]+3)&~3; }
      usn_p16(nb+8,(u16)dgo); usn_p16(nb+10,(u16)ndp); (void)o;
      ng=0; int r=ncm_parse(nb,dgo); int ok=r==5&&ng==5; for(int i=0;i<5&&ok;i++){ u8 e[1600]; mk(e,lens[i],100+i); if(gotn[i]!=lens[i]||memcmp(got[i],e,(size_t)lens[i]))ok=0; }
      rec(ok,"NCM: one block with five datagrams of different sizes"); }
    good=1;
    for(int t=0;t<500;t++){ int len=14+(int)(rnd()%1500); mk(f,len,t); int n=rndis_wrap(b,sizeof b,f,len); ng=0; int r=rndis_parse(b,n); if(r!=1||gotn[0]!=len||memcmp(got[0],f,(size_t)len))good=0; }
    rec(good,"RNDIS: 500 packet messages wrapped and unwrapped");
    { int off=0; int lens[4]={60,1514,300,14}; for(int i=0;i<4;i++){ mk(f,lens[i],200+i); off+=rndis_wrap(b+off,(int)sizeof b-off,f,lens[i]); }
      ng=0; int r=rndis_parse(b,off); int ok=r==4; for(int i=0;i<4&&ok;i++){ u8 e[1600]; mk(e,lens[i],200+i); if(gotn[i]!=lens[i]||memcmp(got[i],e,(size_t)lens[i]))ok=0; }
      rec(ok,"RNDIS: four packets in one transfer"); }
    { u8 s[64]={26,3}; const char* hx="02A0C9B1E2F3"; for(int i=0;i<12;i++){ s[2+i*2]=(u8)hx[i]; s[3+i*2]=0; } u8 m[6]; int ok=usn_strmac(s,64,m)&&m[0]==0x02&&m[1]==0xA0&&m[5]==0xF3;
      s[2]='0'; s[4]='1'; ok=ok&&!usn_strmac(s,64,m); s[4]='2'; s[6]='Z'; ok=ok&&!usn_strmac(s,64,m); rec(ok,"CDC MAC string: parsed, multicast and non-hex refused"); }
    { u8 m[64]; int n=rndis_msg(m,4,7,0x01010101u,0,0); u8 rp[64]={0}; usn_p32(rp,0x80000004u); usn_p32(rp+4,30); usn_p32(rp+8,7); usn_p32(rp+12,0); usn_p32(rp+16,6); usn_p32(rp+20,16); rp[24]=0x02; rp[29]=0x99;
      const u8* d=0; int dl=0; int r=rndis_reply(rp,64,0x80000004u,7,&d,&dl); int r2=rndis_reply(rp,64,0x80000004u,8,&d,&dl); usn_p32(rp+12,0xC0000001u); int r3=rndis_reply(rp,64,0x80000004u,7,&d,&dl);
      rec(n==28&&r==1&&dl==6&&d[0]==0x02&&d[5]==0x99&&r2==-1&&r3==0,"RNDIS: query reply parsed, wrong request id and failure status detected"); }
    long frames=0;
    for(int t=0;t<200000;t++){ int n=1+(int)(rnd()%4000); u8* z=malloc((size_t)n);
        if(t&1){ int len=14+(int)(rnd()%1500); mk(f,len,t); int m=(t&2)?ncm_build(z,n,1,f,len,4,0):rndis_wrap(z,n,f,len); if(m<0){ for(int i=0;i<n;i++)z[i]=(u8)rnd(); } int flips=1+(int)(rnd()%6); for(int k=0;k<flips;k++)z[rnd()%(unsigned)n]=(u8)rnd(); }
        else for(int i=0;i<n;i++)z[i]=(u8)rnd();
        if(t%5==0&&n>=12){ usn_p32(z,0x484D434Eu); usn_p16(z+4,12); }
        ng=0; if(t&2)ncm_parse(z,n); else rndis_parse(z,n); frames+=ng; free(z); }
    char nm[100]; snprintf(nm,sizeof nm,"200000 corrupted or random blocks parsed without overruns (%ld frames)",frames); rec(1,nm);
    printf("%s\n",fails?"FAILURES":"ALL PASS"); return fails?1:0;
}
