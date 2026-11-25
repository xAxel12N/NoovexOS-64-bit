#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/nvx/base.h"
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
#include "net/wifi/wifi_ampdu.c"
static int fails=0;
#define CHK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)
static int got[4096], ng;
static void drain(void){ u8 b[2000]; u64 pn; int tid,r; while((r=ampdu_pop(b,sizeof b,&pn,&tid))>=0){ if(r>=2)got[ng++]=b[0]|(b[1]<<8); else got[ng++]=-1; } }
static void rx(int tid,int seq,int len){ u8 f[100]; f[0]=(u8)seq; f[1]=(u8)(seq>>8); drain(); int r=ampdu_rx(tid,(u16)seq,f,len,(u64)seq+1,0); if(r!=1&&r!=2){ printf("rx %d not consumed\n",seq); fails++; } drain(); }
static void rxt(int seq,u64 now){ u8 f[4]; f[0]=(u8)seq; f[1]=(u8)(seq>>8); drain(); ampdu_rx(0,(u16)seq,f,4,(u64)seq+1,now); drain(); }
int main(void){
    ampdu_reset(); CHK(!ampdu_active(0)); CHK(ampdu_rx(0,5,(const u8*)"ab",2,1,0)==0);
    CHK(ampdu_addba(0,100,0,0)==64); CHK(ampdu_active(0)); CHK(g_ampdu_sess==1);
    ng=0; rx(0,100,4); rx(0,102,4); rx(0,103,4); CHK(ng==1&&got[0]==100); rx(0,101,4); CHK(ng==4&&got[1]==101&&got[2]==102&&got[3]==103);
    ng=0; rx(0,101,4); rx(0,50,4); CHK(ng==0); CHK(g_ampdu_old==2);
    ng=0; rx(0,105,4); rx(0,104+64,4); CHK(ng==1&&got[0]==105);
    ng=0; rx(0,104,4); CHK(ng==0);
    ampdu_reset(); CHK(ampdu_addba(0,4090,0,0)==64);
    ng=0; for(int s=4095;s>=4090;s--)rx(0,s&0xFFF,4); for(int s=4096;s<4100;s++)rx(0,s&0xFFF,4); CHK(ng==10); for(int i=0;i<ng;i++)CHK(got[i]==((4090+i)&0xFFF));
    ampdu_reset(); ampdu_addba(0,0,0,0); ng=0; rx(0,0,4); rx(0,1,4); rx(0,10,4); rx(0,600,4); CHK(ng==4&&got[2]==10&&got[3]==600);
    rx(0,601,4); CHK(ng==5&&got[4]==601);
    ampdu_reset(); ampdu_addba(0,0,0,0); ng=0; rxt(1,0); rxt(3,5); CHK(ng==0); ampdu_tick(9); drain(); CHK(ng==0); ampdu_tick(10); drain(); CHK(ng==2&&got[0]==1&&got[1]==3); CHK(g_ampdu_flush==2);
    rxt(4,11); CHK(ng==3&&got[2]==4); rxt(0,12); CHK(ng==3);
    ampdu_reset(); ampdu_addba(0,0,0,0); ng=0; rx(0,0,0); rx(0,2,4); rx(0,1,0); CHK(ng==3&&got[0]==-1&&got[1]==-1&&got[2]==2);
    ampdu_reset(); ampdu_addba(0,0,0,0); ng=0; rx(0,1,4); rx(0,2,4); ampdu_delba(0); drain(); CHK(ng==2&&got[0]==1&&got[1]==2); CHK(!ampdu_active(0)&&g_ampdu_sess==0);
    ampdu_reset(); CHK(ampdu_addba(0,0,16,0)==16); CHK(ampdu_addba(5,0,0,0)==64); CHK(ampdu_addba(6,0,0,0)==0); CHK(ampdu_addba(0,7,8,0)==8); CHK(g_ampdu_sess==2);
    ampdu_reset(); ampdu_addba(0,0,0,0); { u8 f[4]={0}; u64 p; int t; u8 b[8]; CHK(ampdu_rx(0,0,f,4,1,0)==1); CHK(ampdu_rx(0,3,f,4,4,0)==2); CHK(ampdu_pop(b,8,&p,&t)==4&&p==1); CHK(ampdu_pop(b,8,&p,&t)<0); CHK(ampdu_rx(0,2,f,4,3,0)==1); CHK(ampdu_rx(0,2,f,4,3,0)==2); CHK(ampdu_rx(0,4000,f,4,9,0)==2); CHK(ampdu_rx(0,5,f,4,3,0)==2); CHK(ampdu_rx(0,300,f,4,3,0)==2); CHK(ampdu_active(0)); CHK(ampdu_pop(b,8,&p,&t)<0); CHK(ampdu_rx(0,1,f,4,2,0)==1); CHK(ampdu_pop(b,8,&p,&t)==4&&p==2); CHK(ampdu_pop(b,8,&p,&t)==4&&p==3); }
    ampdu_reset(); ampdu_addba(0,0,0,0); ng=0; rx(0,0,4); rx(0,2,4); rx(0,3,4); CHK(ng==1); CHK(ampdu_addba(0,10,32,0)==32); drain(); CHK(ng==3&&got[1]==2&&got[2]==3); CHK(g_ampdu_sess==1); rx(0,10,4); CHK(ng==4&&got[3]==10);
    g_ampdu_on=0; ampdu_reset(); CHK(ampdu_addba(0,0,0,0)==0); g_ampdu_on=1;
    srand(7); for(int round=0;round<200;round++){ ampdu_reset(); int base=rand()&0xFFF; ampdu_addba(0,(u16)base,0,0); ng=0; int n=1+rand()%200; int ord[200]; for(int i=0;i<n;i++)ord[i]=i;
        for(int i=0;i<n;i++){ int lim=i+1+rand()%8; if(lim>n)lim=n; int j=i+rand()%(lim-i); int t=ord[i]; ord[i]=ord[j]; ord[j]=t; }
        int bad=0; for(int i=0;i<n;i++){ if(abs(ord[i]-i)>=60)bad=1; } if(bad)continue;
        for(int i=0;i<n;i++)rx(0,(base+ord[i])&0xFFF,4); ampdu_tick(1000); drain();
        if(ng!=n){ printf("round %d n=%d got %d\n",round,n,ng); fails++; continue; } for(int i=0;i<n;i++)if(got[i]!=((base+i)&0xFFF)){ printf("round %d order wrong at %d\n",round,i); fails++; break; } }
    printf(fails?"ampdu: %d FAILED\n":"ampdu: all ok\n",fails); return fails?1:0; }
