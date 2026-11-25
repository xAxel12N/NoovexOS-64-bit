#include <stdio.h>
#include <string.h>
#include "net/wifi/wifi_ps.c"
static int fails=0;
#define CK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
int main(void){
    u8 f[24], b[6]={1,2,3,4,5,6}, m[6]={9,8,7,6,5,4};
    CK(wps_null(f,b,m,1)==24); CK(f[0]==0x48&&f[1]==0x11); CK(!memcmp(f+4,b,6)&&!memcmp(f+10,m,6)&&!memcmp(f+16,b,6));
    wps_null(f,b,m,0); CK(f[1]==0x01);
    u8 t1[]={0,3,'a','b','c', 5,4,0,3,0,0x02}; CK(wps_tim(t1,sizeof t1,1)==1); CK(wps_tim(t1,sizeof t1,2)==0);
    u8 t2[]={5,4,0,3,1,0}; CK(wps_tim(t2,sizeof t2,1)==2);
    u8 t3[]={5,4,1,3,1,0}; CK(wps_tim(t3,sizeof t3,1)==0);
    u8 t4[]={5,6,2,3,0x04,0x00,0x00,0x80}; CK(wps_tim(t4,sizeof t4,55)==1); CK(wps_tim(t4,sizeof t4,10)==0); CK(wps_tim(t4,sizeof t4,80)==0);
    u8 t5[]={5,9,0}; CK(wps_tim(t5,sizeof t5,1)==0);
    u8 t6[]={5,2,0,1}; CK(wps_tim(t6,sizeof t6,1)==0);
    CK(wps_tim(t1,sizeof t1,0)==0);
    CK(g_wps_mode==WPS_ON); g_wps_mode=WPS_AUTO; CK(wps_allowed(0,0)==0); CK(wps_allowed(1,1)==0); CK(wps_allowed(1,0)==1);
    g_wps_mode=WPS_ON; CK(wps_allowed(0,1)==1); g_wps_mode=WPS_OFF; CK(wps_allowed(1,0)==0);
    g_wps_mode=WPS_ON; CK(wps_miss()==0); CK(wps_miss()==0); CK(wps_miss()==1); CK(g_wps_off==1); CK(wps_allowed(1,0)==0); CK(wps_miss()==0);
    u8 bf[60]={0}; bf[0]=0x80; memcpy(bf+36,t1,sizeof t1); g_wps_aid=1; g_wps_tim=0; wps_rx_beacon(bf,36+sizeof t1); CK(g_wps_tim==1);
    printf(fails?"wifi_ps: %d FAILED\n":"wifi_ps: all tests passed\n",fails); return fails!=0; }
