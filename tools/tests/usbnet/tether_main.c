#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
#include "drivers/usb/class/ipheth.h"
static int fails, passes;
static void rec(int ok,const char* name){ if(ok)passes++; else fails++; printf("  %-66s %s\n",name,ok?"ok":"FAIL"); }
static unsigned rs=4242; static unsigned rnd(void){ rs=rs*1103515245u+12345u; return rs>>8; }
static int put_if(u8* p,int num,int alt,int neps,int cls,int sub,int pro){ u8 d[9]={9,4,(u8)num,(u8)alt,(u8)neps,(u8)cls,(u8)sub,(u8)pro,0}; memcpy(p,d,9); return 9; }
static int put_ep(u8* p,int addr,int attr,int mps){ u8 d[7]={7,5,(u8)addr,(u8)attr,(u8)(mps&0xFF),(u8)(mps>>8),0}; memcpy(p,d,7); return 7; }
static int iphone_cfg(u8* c,int with_eth,int in_first){
    int n=9; memset(c,0,9); c[0]=9; c[1]=2; c[4]=with_eth?3:2; c[5]=4;
    n+=put_if(c+n,0,0,3,6,1,1); n+=put_ep(c+n,0x81,2,512); n+=put_ep(c+n,0x02,2,512); n+=put_ep(c+n,0x83,3,64);
    n+=put_if(c+n,1,0,2,0xFF,0xFE,2); n+=put_ep(c+n,0x84,2,512); n+=put_ep(c+n,0x03,2,512);
    if(with_eth){ n+=put_if(c+n,2,0,0,0xFF,0xFD,1); n+=put_if(c+n,2,1,2,0xFF,0xFD,1);
        if(in_first){ n+=put_ep(c+n,0x85,2,512); n+=put_ep(c+n,0x04,2,512); } else { n+=put_ep(c+n,0x04,2,512); n+=put_ep(c+n,0x85,2,512); } }
    c[2]=(u8)n; c[3]=(u8)(n>>8); return n;
}
#ifdef TETHER_PARSE
#include "usn_parse.inc"
static int put_union(u8* p,int m,int sl){ u8 d[5]={5,0x24,6,(u8)m,(u8)sl}; memcpy(p,d,5); return 5; }
static int android_cfg(u8* c,int cls,int sub,int pro,int ncm){
    int n=9; memset(c,0,9); c[0]=9; c[1]=2; c[4]=3; c[5]=1;
    u8 iad[8]={8,0x0B,0,2,(u8)cls,(u8)sub,(u8)pro,0}; memcpy(c+n,iad,8); n+=8;
    n+=put_if(c+n,0,0,1,cls,sub,pro); u8 hd[5]={5,0x24,0,0x10,1}; memcpy(c+n,hd,5); n+=5; n+=put_union(c+n,0,1); n+=put_ep(c+n,0x81,3,8);
    if(ncm){ n+=put_if(c+n,1,0,0,0x0A,0,1); n+=put_if(c+n,1,1,2,0x0A,0,1); } else n+=put_if(c+n,1,0,2,0x0A,0,0);
    n+=put_ep(c+n,0x82,2,512); n+=put_ep(c+n,0x01,2,512);
    n+=put_if(c+n,2,0,2,0xFF,0x42,1); n+=put_ep(c+n,0x83,2,512); n+=put_ep(c+n,0x02,2,512);
    c[2]=(u8)n; c[3]=(u8)(n>>8); return n;
}
static int pick(u8 cfgs[][512],const int* lens,int nc,usn_cand_t* best){
    memset(best,0,sizeof *best);
    for(int ci=0;ci<nc;ci++){ int cv=cfgs[ci][5];
        if(usn_parse(cfgs[ci],lens[ci],cv,best)){}
        else if(!best->kind){ iph_cand_t q={0,0,0,0,0,0}; if(iph_find(cfgs[ci],lens[ci],&q)){ best->kind=IPH_KIND; best->cfgval=cv; best->ctlif=q.ifnum; best->dataif=q.ifnum; best->alt=q.alt; best->in_ep=q.in_ep; best->in_mps=q.in_mps; best->out_ep=q.out_ep; best->out_mps=q.out_mps; } } }
    return best->kind;
}
static void sim_tests(void){
    static u8 cf[8][512]; int ln[8]; usn_cand_t b;
    ln[0]=android_cfg(cf[0],0xE0,1,3,0); rec(pick(cf,ln,1,&b)==3&&b.ctlif==0&&b.dataif==1&&b.in_ep==0x82&&b.out_ep==0x01&&strcmp(usbnet_phone_name(b.kind,0x18D1),"Phone (USB)")==0,"sim Pixel RNDIS (E0/01/03): RNDIS chosen, data if 1, Phone (USB)");
    ln[0]=android_cfg(cf[0],2,2,0xFF,0); rec(pick(cf,ln,1,&b)==3&&strcmp(usbnet_phone_name(b.kind,0x04E8),"Phone (USB)")==0,"sim Galaxy RNDIS (02/02/FF): RNDIS chosen, Phone (USB)");
    ln[0]=android_cfg(cf[0],2,0x0D,0,1); rec(pick(cf,ln,1,&b)==2&&b.alt==1&&b.in_ep==0x82,"sim Android 12+ CDC-NCM: NCM chosen, data alt 1");
    for(int i=0;i<4;i++)ln[i]=iphone_cfg(cf[i],i==3,1); for(int i=0;i<4;i++)cf[i][5]=(u8)(i+1);
    rec(pick(cf,ln,4,&b)==IPH_KIND&&b.cfgval==4&&b.dataif==2&&b.alt==1&&b.in_ep==0x85&&strcmp(usbnet_phone_name(b.kind,0x05AC),"Phone (USB)")==0,"sim iPhone with 4 configurations: ipheth in config 4, if 2 alt 1");
    for(int i=0;i<6;i++){ ln[i]=iphone_cfg(cf[i],i==5,1); cf[i][5]=(u8)(i+1); } rec(pick(cf,ln,6,&b)==IPH_KIND&&b.cfgval==6,"sim iPad with ipheth in config 6 (needs more than 4 configurations scanned)");
    for(int i=0;i<3;i++){ ln[i]=iphone_cfg(cf[i],0,1); } rec(pick(cf,ln,3,&b)==0,"sim iPhone with tethering not offered: nothing claimed");
    ln[0]=iphone_cfg(cf[0],1,1); ln[1]=android_cfg(cf[1],2,0x0D,0,1); rec(pick(cf,ln,2,&b)==2,"sim device with ipheth and CDC-NCM: standard NCM preferred");
}
#endif
int main(void){
    u8 c[512]; iph_cand_t r;
    { int n=iphone_cfg(c,1,1); memset(&r,0,sizeof r); int ok=iph_find(c,n,&r)&&r.ifnum==2&&r.alt==1&&r.in_ep==0x85&&r.out_ep==0x04&&r.in_mps==512&&r.out_mps==512;
      rec(ok,"ipheth: iPhone config 4 (PTP + usbmux + ethernet alt 1) found"); }
    { int n=iphone_cfg(c,1,0); memset(&r,0,sizeof r); rec(iph_find(c,n,&r)&&r.in_ep==0x85&&r.out_ep==0x04,"ipheth: endpoint order out-then-in"); }
    { int n=iphone_cfg(c,0,1); memset(&r,0,sizeof r); rec(!iph_find(c,n,&r),"ipheth: config without tethering interface (usbmux only) refused"); }
    { int n=9; memset(c,0,9); c[0]=9; c[1]=2; n+=put_if(c+n,0,0,2,0xFF,0xFD,1); n+=put_ep(c+n,0x81,3,64); n+=put_ep(c+n,0x02,2,512); rec(!iph_find(c,n,&r),"ipheth: interrupt endpoint is not a bulk pair"); }
    { int n=iphone_cfg(c,1,1); int ok=1; for(int cut=0;cut<n;cut++){ memset(&r,0,sizeof r); int f=iph_find(c,cut,&r); if(f&&(r.in_ep!=0x85||r.out_ep!=0x04))ok=0; } rec(ok,"ipheth: truncated descriptors never give a wrong endpoint"); }
    { u8 f[1600], b[2048]; int ok=1; for(int len=14;len<=1514;len++){ for(int i=0;i<len;i++)f[i]=(u8)(len+i*7); int n=iph_tx(b,sizeof b,f,len); if(n!=1514||memcmp(b,f,(size_t)len))ok=0; for(int i=len;i<1514;i++)if(b[i])ok=0; } rec(ok,"ipheth TX: every length 14..1514 sent as one zero-padded 1514-byte buffer"); }
    { u8 f[16]={0}, b[2048]; rec(iph_tx(b,sizeof b,f,13)<0&&iph_tx(b,1000,f,14)<0,"ipheth TX: runt frame and short buffer refused"); u8 big[1600]={0}; rec(iph_tx(b,sizeof b,big,1515)<0,"ipheth TX: oversize frame refused"); }
    { u8 b[1600]; int ok=1; for(int len=14;len<=1514;len++){ b[0]=0xAA; b[1]=0xBB; for(int i=0;i<len;i++)b[2+i]=(u8)(i^len); const u8* f=0; int fl=iph_rx(b,len+2,&f); if(fl!=len||f!=b+2)ok=0; } rec(ok,"ipheth RX: 2-byte header stripped for every length"); }
    { u8 b[2048]={0}; const u8* f; rec(iph_rx(b,0,&f)<0&&iph_rx(b,2,&f)<0&&iph_rx(b,15,&f)<0&&iph_rx(b,1517,&f)<0,"ipheth RX: empty, header-only, runt and oversize transfers dropped"); }
    { u8 b[64]={0}; b[0]=4; int a=iph_carrier(b,64); b[0]=3; int d=iph_carrier(b,64); rec(a&&!d&&!iph_carrier(b,0),"ipheth carrier: 0x04 is on, anything else off"); }
    { u8 m1[6]={0x8a,0x15,0x14,1,2,3}, m2[6]={0}, m3[6]={1,2,3,4,5,6}; rec(iph_mac_ok(m1)&&!iph_mac_ok(m2)&&!iph_mac_ok(m3),"ipheth MAC: zero and multicast addresses refused"); }
    { int ok=strcmp(usbnet_phone_name(6,0x1234),"Phone (USB)")==0&&strcmp(usbnet_phone_name(3,0x18D1),"Phone (USB)")==0&&strcmp(usbnet_phone_name(2,0x04E8),"Phone (USB)")==0;
      ok=ok&&!usbnet_phone_name(3,0x0525)&&!usbnet_phone_name(1,0x0BDA)&&!usbnet_phone_name(2,0x0B95)&&!usbnet_phone_name(0,0x18D1);
      rec(ok,"naming: iPhone, Pixel RNDIS, Samsung NCM shown as Phone (USB); gadgets/dongles not"); }
#ifdef TETHER_PARSE
    sim_tests();
#endif
    long hits=0;
    for(int t=0;t<200000;t++){ int n=(int)(rnd()%300); u8* z=malloc((size_t)n+1);
        if(t&1){ u8 g[512]; int m=iphone_cfg(g,1,(int)(rnd()&1)); if(m>n)m=n; memcpy(z,g,(size_t)m); for(int i=m;i<n;i++)z[i]=(u8)rnd(); int fl=1+(int)(rnd()%4); for(int k=0;k<fl&&n;k++)z[rnd()%(unsigned)n]=(u8)rnd(); }
        else for(int i=0;i<n;i++)z[i]=(u8)rnd();
        iph_cand_t q; memset(&q,0,sizeof q); if(iph_find(z,n,&q)){ hits++; if(!(q.in_ep&0x80)||(q.out_ep&0x80)){ fails++; printf("  fuzz: wrong direction\n"); free(z); break; } }
        const u8* f; int fl=iph_rx(z,n,&f); if(fl>=0&&(f!=z+2||fl!=n-2)){ fails++; free(z); break; }
        free(z); }
    rec(1,"fuzz: 200000 random/corrupted descriptors and transfers (ASan/UBSan)");
    printf("  (fuzz found a candidate %ld times)\n",hits);
    printf(fails?"FAILED %d\n":"ALL PASS (%d checks)\n",fails?fails:passes);
    return fails?1:0;
}
