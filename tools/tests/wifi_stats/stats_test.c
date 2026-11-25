#include <stdio.h>
#include <string.h>
#include "include/nvx/base.h"
#include "net/wifi/wifi_stats_fmt.c"
static int fails=0;
#define EXPECT(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
#define HAS(b,s) EXPECT(strstr(b,s)!=0)
int main(void){ char b[1024];
    EXPECT(wst_mbps_x100(1000000,1000)==800); EXPECT(wst_mbps_x100(10485760,8000)==1048); EXPECT(wst_mbps_x100(5,0)==4); EXPECT(wst_mbps_x100(0xFFFFFFFFFFFFULL,1)==0xFFFFFFFFu);
    EXPECT(wst_retry_pct(0,0)==0); EXPECT(wst_retry_pct(25,75)==25); EXPECT(wst_retry_pct(1,0)==100);
    EXPECT(wst_graph_y(-30,73)==0); EXPECT(wst_graph_y(-95,73)==72); EXPECT(wst_graph_y(-120,73)==72); EXPECT(wst_graph_y(0,73)==0); EXPECT(wst_graph_y(-60,73)>0&&wst_graph_y(-60,73)<72);
    struct wifi_stats s={ "rtw88 (Realtek RTL8821CE)","home",1,36,20,-58,-92,1,72000,1234,567,3,2,9 };
    int n=wifi_stats_fmt(b,sizeof b,&s); EXPECT(n==(int)strlen(b));
    HAS(b,"Driver:       rtw88 (Realtek RTL8821CE)\n"); HAS(b,"Network:      home\n"); HAS(b,"Channel:      36 (5 GHz), 20 MHz\n"); HAS(b,"Signal:       -58 dBm\n");
    HAS(b,"Noise:        -92 dBm (SNR 34 dB)\n"); HAS(b,"Link rate:    72.00 Mbit/s\n"); HAS(b,"TX packets:   567  retries 9 (1%)  dropped 2\n"); HAS(b,"RX packets:   1234  dropped 3\n"); HAS(b,"BA sessions:  1\n");
    s.noise=-127; s.rssi=-127; s.ch=6; s.bw=0; s.rate_kbps=6500; wifi_stats_fmt(b,sizeof b,&s); HAS(b,"Noise:        unknown"); HAS(b,"Signal:       unknown"); HAS(b,"(2.4 GHz), 20 MHz"); HAS(b,"Link rate:    6.50 Mbit/s");
    s.conn=0; s.drv=0; wifi_stats_fmt(b,sizeof b,&s); HAS(b,"Driver:       none\n"); HAS(b,"Link:         not connected\n"); EXPECT(!strstr(b,"Network:"));
    char sm[16]; memset(sm,'X',sizeof sm); n=wifi_stats_fmt(sm,10,&s); EXPECT(n==9&&sm[9]==0&&sm[10]=='X'); EXPECT(wifi_stats_fmt(sm,0,&s)==0&&sm[0]=='D');
    wifi_speed_fmt(b,sizeof b,"Download",10485760,8000,72,-55,2); HAS(b,"Download: 10.48 Mbit/s (10485760 bytes in 8000 ms), 14% of the 72 Mbit/s link\n"); HAS(b,"headroom");
    wifi_speed_fmt(b,sizeof b,"Download",5000000,1000,54,-55,2); HAS(b,"40.00 Mbit/s"); HAS(b,"74% of the 54"); HAS(b,"is the limit\n");
    wifi_speed_fmt(b,sizeof b,"Download",100000,1000,72,-80,0); HAS(b,"weak signal");
    wifi_speed_fmt(b,sizeof b,"Upload",100000,1000,72,-60,30); HAS(b,"retransmissions");
    wifi_speed_fmt(b,sizeof b,"Download",100000,1000,0,-127,0); HAS(b,"not connected over Wi-Fi"); EXPECT(!strstr(b,"% of"));
    memset(sm,'X',sizeof sm); wifi_speed_fmt(sm,12,"Download",1,1,1,1,1); EXPECT(sm[11]==0&&sm[12]=='X');
    printf(fails?"wifi_stats: %d FAILED\n":"wifi_stats: all passed\n",fails); return fails!=0; }
