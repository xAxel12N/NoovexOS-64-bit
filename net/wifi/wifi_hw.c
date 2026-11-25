#include "include/nvx/base.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wifi_netdev.h"

void rtw_wifi_hw_probe(void);
void iwx_wifi_hw_probe(void);
void mt7921_wifi_hw_probe(void);
void mt7921u_wifi_hw_probe(void);
void rtw89_wifi_hw_probe(void);
void mt7925_wifi_hw_probe(void);
void rtwusb_wifi_hw_probe(void);
static const struct wifi_hw_ops* whw_ops[WIFI_HW_MAX]; static void* whw_ctx[WIFI_HW_MAX]; static int whw_n=0, whw_probed=0;
extern volatile u64 g_ticks; static u64 whw_last=0;
static void whw_probe(void){ if(whw_n)return; if(whw_probed&&g_ticks-whw_last<3000)return; whw_probed=1; whw_last=g_ticks; rtw_wifi_hw_probe(); if(!whw_n)iwx_wifi_hw_probe(); if(!whw_n)mt7921_wifi_hw_probe(); if(!whw_n)mt7921u_wifi_hw_probe(); if(!whw_n)mt7925_wifi_hw_probe(); if(!whw_n)rtw89_wifi_hw_probe(); if(!whw_n)rtwusb_wifi_hw_probe(); }
int wifi_hw_register(const struct wifi_hw_ops* ops,void* ctx){
    if(!ops||!ops->name)return -1; for(int i=0;i<whw_n;i++) if(whw_ops[i]==ops){ whw_ctx[i]=ctx; return i; }
    if(whw_n>=WIFI_HW_MAX)return -1; whw_ops[whw_n]=ops; whw_ctx[whw_n]=ctx;
    char b[80],*p=b; p=scopy(p,"wifi: adapter driver registered: "); p=scopy(p,ops->name); *p=0; klog(b); return whw_n++; }
const struct wifi_hw_ops* wifi_hw_ops_get(void){ whw_probe(); return whw_n?whw_ops[0]:0; }
void* wifi_hw_ctx(void){ return whw_n?whw_ctx[0]:0; }
int wifi_hw_generic(void){ const struct wifi_hw_ops* o=wifi_hw_ops_get(); return o&&!(o->flags&WIFI_HW_NATIVE); }
#define WHW(f) const struct wifi_hw_ops* o=wifi_hw_ops_get(); if(!o||!o->f)
int wifi_hw_init(void){ WHW(init)return -1; return o->init(whw_ctx[0]); }
int wifi_hw_scan_start(const u8* ch,int n){ WHW(scan_start)return -1; return o->scan_start(whw_ctx[0],ch,n); }
int wifi_hw_scan_done(void){ WHW(scan_done)return 0; return o->scan_done(whw_ctx[0]); }
int wifi_hw_join(const u8* bssid,u8 ch,const char* ssid,u8 akm,u8 ht){ WHW(join)return 3; return o->join(whw_ctx[0],bssid,ch,ssid,akm,ht); }
void wifi_hw_leave(void){ WHW(leave)return; o->leave(whw_ctx[0]); }
#define WHT_N 4
#define WHT_SZ 256
static int whw_in_rx=0, whw_txn=0, whw_txl[WHT_N]; static u8 whw_txq[WHT_N][WHT_SZ];
static void whw_flush(const struct wifi_hw_ops* o){ int n=whw_txn; whw_txn=0; for(int i=0;i<n;i++)o->tx(whw_ctx[0],whw_txq[i],whw_txl[i]); }
int wifi_hw_tx(const u8* frame,int len){ WHW(tx)return -1; if(!frame||len<10||len>2304)return -1;
    if(whw_in_rx){ if(len>WHT_SZ||whw_txn>=WHT_N)return -1; for(int i=0;i<len;i++)whw_txq[whw_txn][i]=frame[i]; whw_txl[whw_txn++]=len; return 0; }
    return o->tx(whw_ctx[0],frame,len); }
int wifi_hw_set_key(int idx,int type,const u8* key,int len,int pairwise){ WHW(set_key)return 0; return o->set_key(whw_ctx[0],idx,type,key,len,pairwise); }
int wifi_hw_rssi(void){ WHW(get_rssi)return -127; return o->get_rssi(whw_ctx[0]); }
void wifi_hw_poll(void){ WHW(poll)return; if(whw_in_rx)return; if(o->tx&&whw_txn)whw_flush(o); o->poll(whw_ctx[0]); if(o->tx&&whw_txn)whw_flush(o); }
int wifi_hw_mac(u8* mac){ WHW(get_mac)return -1; return o->get_mac(whw_ctx[0],mac); }

#define WHQ_N 16
#define WHQ_SZ 2400
static u8 whq[WHQ_N][WHQ_SZ]; static int whq_len[WHQ_N], whq_rssi[WHQ_N], whq_dec[WHQ_N], whq_h=0, whq_c=0;
void wnd_rx_mgmt_hw(const u8* fr,int len,int rssi);
void wnd_rx_mgmt_hw2(const u8* fr,int len,int rssi,int dec);
int wifi_hw_eapol_rx(const u8* fr,int len);
void wifi_hw_rx(const u8* fr,int len,int rssi){ wifi_hw_rx_dec(fr,len,rssi,0); }
void wifi_hw_rx_dec(const u8* fr,int len,int rssi,int dec){
    extern u32 g_rtw_rx_seen, g_rtw_rx_bcn; extern int g_wifi_scanning; extern u8 g_scan_ch;
    if(!fr||len<24)return; g_rtw_rx_seen++;
    u8 t=fr[0]&0x0C;
    if(t==0x00){ if(g_wifi_scanning&&wifi_scan_parse(fr,len,g_scan_ch,rssi,1))g_rtw_rx_bcn++; whw_in_rx++; wnd_rx_mgmt_hw2(fr,len,rssi,dec); whw_in_rx--; return; }
    if(t!=0x08)return; if(!wnd_is_up()){ wifi_hw_eapol_rx(fr,len); return; }
    if(whq_c>=WHQ_N||len>WHQ_SZ){ g_wnd_rx_drop++; return; }
    int s=(whq_h+whq_c)%WHQ_N; for(int i=0;i<len;i++)whq[s][i]=fr[i]; whq_len[s]=len; whq_rssi[s]=rssi; whq_dec[s]=dec; whq_c++; }
int wifi_hw_rx_pop(u8* out,int max,int* rssi){ return wifi_hw_rx_pop_dec(out,max,rssi,0); }
int wifi_hw_rx_pop_dec(u8* out,int max,int* rssi,int* dec){
    if(!whq_c)return 0; int s=whq_h, L=whq_len[s]; if(dec)*dec=whq_dec[s]; whq_h=(whq_h+1)%WHQ_N; whq_c--;
    if(L>max){ g_wnd_rx_drop++; return -1; } for(int i=0;i<L;i++)out[i]=whq[s][i]; if(rssi)*rssi=whq_rssi[s]; return L; }
