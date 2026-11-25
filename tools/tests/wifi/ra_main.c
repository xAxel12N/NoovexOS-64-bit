#include <stdio.h>
#include <string.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
volatile u64 g_ticks=1000; int g_wifi_ra=1, g_wifi_band5=0; int h2c_fail=0, sent=0; u32 lw0, lw1;
int rtw_h2c_cmd(u32 m,u32 w0,u32 w1){ (void)m; if(h2c_fail)return -1; sent++; lw0=w0; lw1=w1; return 0; }
void rtw_jlog(const char* a,const char* b,int v){ (void)b; printf("log: %s %x\n",a,v); }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ return p+sprintf(p,"%u",v); }
char* hx(char* p,u32 v,int d){ return p+sprintf(p,"%0*X",d,v); }
#define RTW_RA_TEST
#include "ra_src.c"
int fails=0;
#define CK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
int main(void){
    u32 mk; u8 sg; static const u8 R12[12]={2,4,11,22,12,18,24,36,48,72,96,108};
    u8 ies[]={1,8,0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24, 50,4,0x30,0x48,0x60,0x6C, 45,26,0x6E,0x00,0x17,0xFF,0xFF,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    int rid=rtw_ra_build(0,3,R12,12,ies,sizeof ies,&mk,&sg); CK(rid==3); CK(mk==0xFF015); CK(sg==1);
    rid=rtw_ra_build(0,0,R12,12,ies,sizeof ies,&mk,&sg); CK(rid==6); CK(mk==0xFF5); CK(sg==0);
    u8 b[]={1,4,0x82,0x84,0x8B,0x96}; rid=rtw_ra_build(0,0,R12,12,b,sizeof b,&mk,&sg); CK(rid==8); CK(mk==0xF);
    rid=rtw_ra_build(1,1,R12+4,8,ies,sizeof ies,&mk,&sg); CK(rid==5); CK(mk==0xFF030);
    rid=rtw_ra_build(1,0,R12+4,8,0,0,&mk,&sg); CK(rid==7); CK(mk==0xFF0);
    CK(rtw_ra_desc_rate100k(0x13,1,0)==722); CK(rtw_ra_desc_rate100k(0x13,0,0)==650); CK(rtw_ra_desc_rate100k(11,0,0)==540); CK(rtw_ra_desc_rate100k(0x2C+9,0,2)==3900);
    g_rtw_ra_ies_n=0; rtw_ra_peer_ies(ies,sizeof ies); CK(g_rtw_ra_ies_n==(int)sizeof ies);
    rid=rtw_ra_fw_setup(0,3,R12,12,&mk); CK(rid==3); CK(sent==2); CK((lw0&0xFF)==0x40); CK(((lw0>>16)&0x1F)==3); CK(lw0&(1u<<23)); CK(lw1==0xFF015);
    u8 c2h[]={0x0C,5,0x80|0x13,0,0,0,0,60,0}; rtw_c2h_rx(c2h,sizeof c2h); CK(g_rtw_ra_rpt.n==1); CK(rtw_ra_fw_mbps()==72);
    rtw_c2h_rx(c2h,sizeof c2h); CK(g_rtw_ra_rpt.n==1); rtw_c2h_rx(c2h,4); CK(g_rtw_ra_rpt.n==1);
    char s[256]; rtw_ra_status(s); printf("%s\n",s);
    h2c_fail=1; for(int i=0;i<3;i++)rtw_ra_fw_setup(0,3,R12,12,&mk); CK(g_wifi_ra==0); CK(rtw_ra_fw_mbps()==-1);
    printf(fails?"FAILED %d\n":"all ok\n",fails); return fails!=0; }
