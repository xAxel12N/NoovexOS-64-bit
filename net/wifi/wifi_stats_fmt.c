#include "include/nvx/base.h"
#include "include/nvx/net/wifi/wifi_stats.h"
static char* ws_s(char* p,char* e,const char* s){ while(s&&*s&&p<e)*p++=*s++; return p; }
static char* ws_u(char* p,char* e,u64 v){ char t[24]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n&&p<e)*p++=t[--n]; return p; }
static char* ws_i(char* p,char* e,int v){ if(v<0){ if(p<e)*p++='-'; return ws_u(p,e,(u64)(-(i64)v)); } return ws_u(p,e,(u64)v); }
static char* ws_x100(char* p,char* e,u32 v){ p=ws_u(p,e,v/100); if(p<e)*p++='.'; if(p<e)*p++=(char)('0'+(v/10)%10); if(p<e)*p++=(char)('0'+v%10); return p; }
u32 wst_mbps_x100(u64 bytes,u32 ms){ if(!ms)ms=1; u64 r=bytes*8ULL/((u64)ms*10ULL); return r>0xFFFFFFFFULL?0xFFFFFFFFu:(u32)r; }
int wst_retry_pct(u32 retry,u32 tx){ u64 t=(u64)tx+retry; return t?(int)((u64)retry*100ULL/t):0; }
const char* wst_hint(u32 mx100,u32 link_mbps,int rssi,int retry_pct){
    if(!link_mbps)return "not connected over Wi-Fi: the Wi-Fi link is not part of this result";
    if(rssi>-127&&rssi<-75)return "weak signal: the Wi-Fi link is probably the limit (move closer to the access point)";
    if(retry_pct>=20)return "many retransmissions: the Wi-Fi link is probably the limit (interference or a busy channel)";
    if((u64)mx100*10ULL>=(u64)link_mbps*100ULL*6ULL)return "close to the Wi-Fi link rate: the Wi-Fi link is the limit";
    return "the Wi-Fi link has headroom: the limit is the internet line, the server or the OS network stack";
}
int wst_graph_y(int rssi,int h){ if(rssi<-95)rssi=-95; if(rssi>-30)rssi=-30; return (h-1)-((rssi+95)*(h-1))/65; }
int wifi_stats_fmt(char* o,int cap,const struct wifi_stats* s){ if(cap<=0)return 0; char* p=o; char* e=o+cap-1;
    p=ws_s(p,e,"Driver:       "); p=ws_s(p,e,s->drv&&s->drv[0]?s->drv:"none"); p=ws_s(p,e,"\n");
    if(!s->conn){ p=ws_s(p,e,"Link:         not connected\n"); }
    else { p=ws_s(p,e,"Network:      "); p=ws_s(p,e,s->ssid); p=ws_s(p,e,"\nChannel:      "); p=ws_u(p,e,(u64)s->ch); p=ws_s(p,e,s->ch>14?" (5 GHz), ":" (2.4 GHz), "); p=ws_u(p,e,(u64)(s->bw>0?s->bw:20)); p=ws_s(p,e," MHz\n");
        p=ws_s(p,e,"Signal:       "); if(s->rssi>-127){ p=ws_i(p,e,s->rssi); p=ws_s(p,e," dBm"); } else p=ws_s(p,e,"unknown");
        p=ws_s(p,e,"\nNoise:        "); if(s->noise>-127&&s->noise<0){ p=ws_i(p,e,s->noise); p=ws_s(p,e," dBm"); if(s->rssi>-127){ p=ws_s(p,e," (SNR "); p=ws_i(p,e,s->rssi-s->noise); p=ws_s(p,e," dB)"); } } else p=ws_s(p,e,"unknown");
        p=ws_s(p,e,"\nLink rate:    "); p=ws_x100(p,e,s->rate_kbps/10); p=ws_s(p,e," Mbit/s\n"); }
    p=ws_s(p,e,"TX packets:   "); p=ws_u(p,e,s->tx); p=ws_s(p,e,"  retries "); p=ws_u(p,e,s->retry); p=ws_s(p,e," ("); p=ws_u(p,e,(u64)wst_retry_pct(s->retry,s->tx)); p=ws_s(p,e,"%)  dropped "); p=ws_u(p,e,s->txdrop);
    p=ws_s(p,e,"\nRX packets:   "); p=ws_u(p,e,s->rx); p=ws_s(p,e,"  dropped "); p=ws_u(p,e,s->rxdrop);
    p=ws_s(p,e,"\nBA sessions:  "); p=ws_u(p,e,(u64)(s->ba>0?s->ba:0)); p=ws_s(p,e,"\n"); *p=0; return (int)(p-o); }
int wifi_speed_fmt(char* o,int cap,const char* what,u64 bytes,u32 ms,u32 link_mbps,int rssi,int retry_pct){ if(cap<=0)return 0; char* p=o; char* e=o+cap-1; u32 m=wst_mbps_x100(bytes,ms);
    p=ws_s(p,e,what); p=ws_s(p,e,": "); p=ws_x100(p,e,m); p=ws_s(p,e," Mbit/s ("); p=ws_u(p,e,bytes); p=ws_s(p,e," bytes in "); p=ws_u(p,e,ms); p=ws_s(p,e," ms)");
    if(link_mbps){ p=ws_s(p,e,", "); p=ws_u(p,e,(u64)m/link_mbps); p=ws_s(p,e,"% of the "); p=ws_u(p,e,link_mbps); p=ws_s(p,e," Mbit/s link"); }
    p=ws_s(p,e,"\n  "); p=ws_s(p,e,wst_hint(m,link_mbps,rssi,retry_pct)); p=ws_s(p,e,"\n"); *p=0; return (int)(p-o); }
