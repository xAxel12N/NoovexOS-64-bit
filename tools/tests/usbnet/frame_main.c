#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
#define NVX_BASE_H
#include "drivers/usb/class/usbnet_frame.c"
static int fails, checks; static u8 got[64][2048]; static int gl[64], gn;
static void ck(int c,const char* m){ checks++; if(!c){ fails++; printf("  FAIL %s\n",m); } else printf("  ok %s\n",m); }
static void sink(void* a,const u8* f,int n){ (void)a; if(gn<64){ memcpy(got[gn],f,n); gl[gn++]=n; } }
static void p32(u8* p,u32 v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void mk(u8* f,int n,int s){ for(int i=0;i<n;i++)f[i]=(u8)(i*7+s); }
int main(void){
    unf_ctx_t c; u8 f1[1514], f2[60], buf[20000]; mk(f1,1514,1); mk(f2,60,9);
    int kinds[]={UNF_ECM,UNF_NCM,UNF_RNDIS,UNF_IPHETH};
    for(int k=0;k<4;k++){ unf_init(&c,kinds[k],512); int n=unf_tx(&c,buf,sizeof buf,f2,60); gn=0; int r=n>0?unf_rx(&c,kinds[k]==UNF_IPHETH?buf-2+2:buf,n,sink,0):-1;
        if(kinds[k]==UNF_IPHETH){ u8 b2[2000]; b2[0]=b2[1]=0; memcpy(b2+2,buf,n); gn=0; r=unf_rx(&c,b2,n+2,sink,0); ck(r==1&&gl[0]>=60&&!memcmp(got[0],f2,60),"ipheth: tx pads to 1514, rx strips 2-byte prefix"); continue; }
        char m[80]; snprintf(m,80,"%s round trip",unf_kind_name(kinds[k])); ck(r==1&&gl[0]==60&&!memcmp(got[0],f2,60),m); }
    unf_init(&c,UNF_URE,512); int n=unf_tx(&c,buf,sizeof buf,f1,1514);
    ck(n==1524&&buf[3]==0xC0&&(buf[0]|(buf[1]<<8))==1514,"RTL815x tx: 8-byte descriptor FS|LS, 4-byte aligned");
    memset(buf,0,sizeof buf); int off=0;
    p32(buf+off,1514+4); off+=24; memcpy(buf+off,f1,1514); off+=(1518+7)&~7;
    p32(buf+off,60+4); off+=24; memcpy(buf+off,f2,60); off+=(64+7)&~7;
    gn=0; int r=unf_rx(&c,buf,off,sink,0);
    ck(r==2&&gl[0]==1514&&gl[1]==60&&!memcmp(got[0],f1,1514)&&!memcmp(got[1],f2,60),"RTL815x rx: two aggregated frames, CRC stripped, 8-byte alignment");
    p32(buf,0x7FFF); gn=0; r=unf_rx(&c,buf,off,sink,0); ck(r<0&&gn==0,"RTL815x rx: bogus length drops the rest of the buffer");
    unf_init(&c,UNF_AXEN,512); n=unf_tx(&c,buf,sizeof buf,f2,60); ck(n==68&&buf[0]==60&&buf[4]==0,"AX88179 tx: 8-byte header");
    memset(buf,0,sizeof buf); off=0; memcpy(buf,f1,1514); off=(1514+7)&~7; memcpy(buf+off,f2,60); off+=64; int ho=off;
    p32(buf+off,(1514u<<16)); off+=4; p32(buf+off,0); off+=4; p32(buf+off,(60u<<16)|0x20000000u); off+=4;
    p32(buf+off,3|((u32)ho<<16)); off+=4;
    gn=0; c.rx_bad=0; r=unf_rx(&c,buf,off,sink,0);
    ck(r==1&&gl[0]==1514&&!memcmp(got[0],f1,1514)&&c.rx_bad==1,"AX88179 rx: trailer, dummy header skipped, CRC-error frame dropped");
    u8 m[6]; unf_mac_local(m,123); ck(unf_mac_ok(m)&&(m[0]&2)&&!(m[0]&1),"random MAC is local unicast");
    srand(7); int crash=0; for(int it=0;it<200000;it++){ int k=1+it%6; unf_init(&c,k,512); int len=rand()%4096; for(int i=0;i<len;i++)buf[i]=rand(); if(rand()&1&&len>8){ p32(buf+len-4,(rand()%8)|((u32)(rand()%len)<<16)); } gn=0; unf_rx(&c,buf,len,sink,0); if(gn>64)crash=1; }
    ck(!crash,"200000 random buffers across all framings (sanitizers watch bounds)");
    printf(fails?"usbnet frame: %d FAIL of %d\n":"usbnet frame: ALL PASS (%d checks)\n",fails?fails:checks,checks); return fails?1:0;
}
