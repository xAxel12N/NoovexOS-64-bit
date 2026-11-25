#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/http/http.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wifi_mgr.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wifi_ampdu.h"
#include "include/nvx/net/wifi/wifi_stats.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/graphics/ui/desktop.h"
u32 g_wst_retry=0, g_wst_rate_kbps=0; int g_wst_noise=-127, g_wst_bw=20;
static signed char g_wst_h[WST_HIST]; static int g_wst_hn=0, g_wst_hh=0; static u64 g_wst_last=0;
void wifi_stats_get(struct wifi_stats* s){ extern u32 g_wnd_txfull; static char nm[48];
    if(wifi_hw_generic())s->drv=wifi_hw_ops_get()->name; else if(rtw_find_mmio()&&g_rtw_chip){ char* p=nm; const char* a="rtw88 (Realtek "; while(*a)*p++=*a++; for(const char* c=g_rtw_chip->name;c&&*c&&p<nm+44;c++)*p++=*c; *p++=')'; *p=0; s->drv=nm; } else s->drv=0;
    s->conn=g_wm_state==WM_CONNECTED; s->ssid=g_wifi_conn; s->ch=g_wm_ch; s->bw=g_wst_bw; s->rssi=g_wifi_rssi; s->noise=g_wst_noise; s->ba=(int)g_ampdu_sess;
    s->rate_kbps=g_wst_rate_kbps?g_wst_rate_kbps:(u32)wifi_rate_mbps()*1000u; s->rx=g_wnd_rx_frames; s->tx=g_wnd_tx_frames; s->rxdrop=g_wnd_rx_drop; s->txdrop=g_wnd_txfull; s->retry=g_wst_retry; }
void wifi_stats_sample(void){ if(g_ticks-g_wst_last<100)return; g_wst_last=g_ticks; int r=g_wm_state==WM_CONNECTED?g_wifi_rssi:-127; if(r<-127)r=-127; if(r>0)r=0;
    g_wst_h[g_wst_hh]=(signed char)r; g_wst_hh=(g_wst_hh+1)%WST_HIST; if(g_wst_hn<WST_HIST)g_wst_hn++; }
int wifi_stats_hist(signed char* out,int max){ int n=g_wst_hn<max?g_wst_hn:max; int s=(g_wst_hh-n+WST_HIST)%WST_HIST; for(int i=0;i<n;i++)out[i]=g_wst_h[(s+i)%WST_HIST]; return n; }
static u8 g_wst_buf[49152];
static int wst_req(char* q,const char* m,const char* host,const char* path,u32 clen){ int r=0; const char* parts[]={m," ",path," HTTP/1.0\r\nHost: ",host,"\r\nUser-Agent: Noovex64\r\nConnection: close\r\n",0};
    for(int i=0;parts[i];i++)for(const char* c=parts[i];*c&&r<900;c++)q[r++]=*c;
    if(clen){ const char* c="Content-Type: application/octet-stream\r\nContent-Length: "; while(*c)q[r++]=*c++; char t[12]; int n=0; do{ t[n++]=(char)('0'+clen%10); clen/=10; }while(clen); while(n)q[r++]=t[--n]; q[r++]='\r'; q[r++]='\n'; }
    q[r++]='\r'; q[r++]='\n'; return r; }
static int wst_open(const char* host){ u32 ip; if(!parse_ip(host,&ip)&&dns_query(host,&ip)<=0)return -1; return tcps_open(ip,80)?0:-2; }
static int wst_status(const u8* b,int n){ if(n<12||b[0]!='H'||b[1]!='T')return -1; return (b[9]-'0')*100+(b[10]-'0')*10+(b[11]-'0'); }
int wifi_speedtest(char* o,int cap,const char* host,const char* path,int up){ struct wifi_stats s; char q[1024]; int r;
    u32 tx0=g_wnd_tx_frames, rt0=g_wst_retry; wifi_stats_get(&s); u32 link=s.conn?s.rate_kbps/1000u:0;
    if(!up){ if((r=wst_open(host))<0)return (int)(scopy(o,r==-1?"speedtest: DNS lookup failed\n":"speedtest: could not connect to the server\n")-o);
        int ql=wst_req(q,"GET",host,path,0); tcps_wr(0,(const u8*)q,ql);
        u64 body=0; int hdr=1, st=-1, hb=0; u64 t0=0, t1=g_ticks, lim=g_ticks+1500;
        while(g_ticks<lim){ int n=tcps_rd(0,g_wst_buf+hb,(int)sizeof g_wst_buf-hb); if(n<=0)break;
            if(hdr){ hb+=n; for(int i=0;i+3<hb;i++)if(g_wst_buf[i]=='\r'&&g_wst_buf[i+1]=='\n'&&g_wst_buf[i+2]=='\r'&&g_wst_buf[i+3]=='\n'){ st=wst_status(g_wst_buf,hb); body=(u64)(hb-i-4); hdr=0; t0=g_ticks; break; } if(hdr&&hb>=(int)sizeof g_wst_buf-1500)break; if(hdr)continue; hb=0; continue; }
            body+=(u64)n; t1=g_ticks; }
        if(hdr||st!=200){ char* p=scopy(o,"speedtest: server answered with HTTP "); p=udec(p,(u32)(st<0?0:st)); p=scopy(p,hdr?" (no complete reply)\n":"\n"); return (int)(p-o); }
        u32 ms=(u32)((t1>t0?t1-t0:1)*10ULL); return wifi_speed_fmt(o,cap,g_ticks>=lim?"Download (stopped after 15 s)":"Download",body,ms,link,s.rssi,wst_retry_pct(g_wst_retry-rt0,g_wnd_tx_frames-tx0)); }
    u32 len=49152; for(u32 i=0;i<len;i++)g_wst_buf[i]=(u8)(i*31u+7u);
    if((r=wst_open(host))<0)return (int)(scopy(o,r==-1?"speedtest: DNS lookup failed\n":"speedtest: could not connect to the server\n")-o);
    int ql=wst_req(q,"POST",host,path,len); u64 t0=g_ticks; tcps_wr(0,(const u8*)q,ql); tcps_wr(0,g_wst_buf,(int)len);
    u8 rb[256]; int n=tcps_rd(0,rb,sizeof rb); u64 t1=g_ticks; int st=wst_status(rb,n);
    if(st<200||st>=400){ char* p=scopy(o,"speedtest upload: no usable reply (HTTP "); p=udec(p,(u32)(st<0?0:st)); p=scopy(p,") - the server may not accept uploads\n"); return (int)(p-o); }
    return wifi_speed_fmt(o,cap,"Upload (48 KB, rough)",len,(u32)((t1>t0?t1-t0:1)*10ULL),link,s.rssi,wst_retry_pct(g_wst_retry-rt0,g_wnd_tx_frames-tx0)); }
