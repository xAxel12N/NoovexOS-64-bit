#include "include/nvx/base.h"
#include "include/drivers.h"
#include "include/nvx/lib/base/fmt.h"
#include "include/nvx/fs/vfs/ramfs.h"
#include "include/nvx/fs/vfs/dirtree.h"
#include "include/nvx/fs/vfs/unixfs.h"
#include "include/nvx/core/klog.h"
#include "include/nvx/drivers/net/ethernet/nic.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wpa2.h"
#include "include/nvx/net/wifi/wifi_mgr.h"
#include "include/nvx/net/wifi/eap.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"

extern volatile u64 g_ticks;
extern int g_airplane;
u32 rtw_find_mmio(void);
int rtw_wpa2_join(u32 m,const scan_ap* ap,const char* ssid,const char* pass);
int wifi_eap_join(u32 m,const scan_ap* ap,const char* ssid,const char* user,const char* pass);
void run_cmd(char* line);
void net_idle_poll(void);
int http_fetch_to(const char*host,const char*path,u8*buf,int cap);
void ui_pump(void);

wnet g_wnets[WIFI_MAXNET]; int g_wnet_n=0;
int g_wm_state=WM_OFF, g_wm_auto=1, g_wm_5g=1, g_wm_ps=0, g_wm_lps=0, g_wm_portal=0, g_wm_cur=-1, g_wm_reason=0, g_wm_up=0;
u8 g_wm_bssid[6]; u8 g_wm_ch=0; u64 g_wm_since=0;
static u64 wm_next_try=0, wm_backoff=500, wm_last_ok=0;
static int wm_fails=0, wm_loaded=0, wm_pending=0, wm_useroff=0; static u64 wm_failt[WIFI_MAXNET]; static char wm_noauto[33];
extern int g_wifi_entry;
static int wm_lps(int on);
static int sc_on; static void scan_finish(void);

static void wm_cpy(char* d,const char* s,int cap){ int i=0; while(s&&s[i]&&i<cap-1){ d[i]=s[i]; i++; } d[i]=0; }
static int wm_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static void wm_status(const char* a,const char* b){ char* p=g_wifi_status; char* e=g_wifi_status+90; for(int i=0;a&&a[i]&&p<e;i++)*p++=a[i]; for(int i=0;b&&b[i]&&p<e;i++)*p++=b[i]; *p=0; }
void wm_status_set(const char* a){ wm_status(a,0); }

#define WDIR "/etc/noovex/wifi"
#define WFILE WDIR "/networks"
static char* wm_hex(char* p,const char* s){ static const char* H="0123456789abcdef"; for(int i=0;s[i];i++){ u8 c=(u8)s[i]; *p++=H[c>>4]; *p++=H[c&15]; } if(!s[0])*p++='-'; return p; }
static int wm_unhex(const char* s,int n,char* o,int cap){ int k=0; if(n==1&&s[0]=='-'){ o[0]=0; return 0; } for(int i=0;i+1<n&&k<cap-1;i+=2){ int a=s[i],b=s[i+1]; a=a<='9'?a-'0':(a|32)-'a'+10; b=b<='9'?b-'0':(b|32)-'a'+10; if(a<0||a>15||b<0||b>15)break; o[k++]=(char)(a*16+b); } o[k]=0; return k; }
static void wm_mkdir(const char* parent,const char* name,unsigned mode){ dt_init(); int d=dt_resolve_dir(parent); if(d<0)return; int c=dt_child(d,name); if(c<0)c=dt_mkdir_at(d,name); if(c>=0){ DT[c].uid=0; DT[c].gid=0; DT[c].mode=mode; } }

void wifi_saved_save(void){
    int su=g_uid; g_uid=0; wm_mkdir("/etc","noovex",0040755u); wm_mkdir("/etc/noovex","wifi",0040700u);
    static char b[8192]; char* p=b; p=scopy(p,"NVWIFI1\n"); p=scopy(p,"auto="); *p++=g_wm_auto?'1':'0'; p=scopy(p," 5g="); *p++=g_wm_5g?'1':'0'; *p++='\n';
    for(int i=0;i<g_wnet_n&&p<b+7600;i++){ wnet* w=&g_wnets[i]; p=scopy(p,"net "); p=udec(p,(u32)w->prio); *p++=' '; *p++=(char)('0'+w->hidden); *p++=' '; *p++=(char)('0'+w->autoc); *p++=' '; *p++=(char)('0'+w->eap); *p++=' ';
        p=wm_hex(p,w->ssid); *p++=' '; p=wm_hex(p,w->pass); *p++=' '; p=wm_hex(p,w->user); *p++=' '; if(w->pinset){ static const char* H="0123456789abcdef"; for(int z=0;z<32;z++){ *p++=H[w->pin[z]>>4]; *p++=H[w->pin[z]&15]; } } else *p++='-'; *p++='\n'; }
    ux_write_text(WFILE,b,(int)(p-b),0100600u); int isd=0; int k=ux_lookup(WFILE,&isd,0); if(k>=0&&!isd){ FS[k].uid=0; FS[k].gid=0; FS[k].mode=0100600u; }
    for(int z=0;z<(int)sizeof(b);z++)b[z]=0; dt_save(); g_uid=su; }

void wifi_saved_load(void){
    wm_loaded=1; int su=g_uid; g_uid=0; static char b[8192]; int n=ux_read_file(WFILE,b,(int)sizeof(b)-1); g_uid=su; g_wnet_n=0; if(n<=0)return; b[n]=0;
    int i=0; while(i<n){ int e=i; while(e<n&&b[e]!='\n')e++;
        if(e-i>5&&b[i]=='a'&&b[i+1]=='u'){ for(int k=i;k<e;k++){ if(b[k]=='='&&k>i+3&&b[k-1]=='o'&&b[k-2]=='t')g_wm_auto=b[k+1]=='1'; if(b[k]=='='&&b[k-1]=='g'&&b[k-2]=='5')g_wm_5g=b[k+1]=='1'; } }
        else if(e-i>8&&b[i]=='n'&&b[i+1]=='e'&&b[i+2]=='t'&&b[i+3]==' '&&g_wnet_n<WIFI_MAXNET){
            const char* f[8]; int fl[8]; int nf=0; int k=i+4; while(k<e&&nf<8){ while(k<e&&b[k]==' ')k++; f[nf]=b+k; int s=k; while(k<e&&b[k]!=' ')k++; fl[nf++]=k-s; }
            if(nf>=7){ wnet* w=&g_wnets[g_wnet_n]; for(int z=0;z<(int)sizeof(*w);z++)((u8*)w)[z]=0;
                int pr=0; for(int z=0;z<fl[0]&&z<3;z++){ if(f[0][z]<'0'||f[0][z]>'9')break; pr=pr*10+(f[0][z]-'0'); } if(pr>99)pr=99; w->prio=pr; w->hidden=f[1][0]=='1'; w->autoc=f[2][0]!='0'; w->eap=f[3][0]=='1';
                wm_unhex(f[4],fl[4],w->ssid,33); wm_unhex(f[5],fl[5],w->pass,65); wm_unhex(f[6],fl[6],w->user,65);
                if(nf>=8&&fl[7]==64){ char t[33]; if(wm_unhex(f[7],64,t,33)==32){ for(int z=0;z<32;z++)w->pin[z]=(u8)t[z]; w->pinset=1; } }
                if(w->ssid[0])g_wnet_n++; } }
        i=e+1; } for(int z=0;z<(int)sizeof(b);z++)b[z]=0; }

int wifi_saved_find(const char* ssid){ for(int i=0;i<g_wnet_n;i++) if(wm_eq(g_wnets[i].ssid,ssid))return i; return -1; }
int wifi_saved_add(const char* ssid,const char* pass,const char* user,int eap,int hidden){
    int k=wifi_saved_find(ssid); if(k<0){ if(g_wnet_n>=WIFI_MAXNET){ int lo=0; for(int i=1;i<g_wnet_n;i++) if(g_wnets[i].prio<g_wnets[lo].prio)lo=i; k=lo; } else k=g_wnet_n++; for(int z=0;z<(int)sizeof(wnet);z++)((u8*)&g_wnets[k])[z]=0; g_wnets[k].autoc=1; g_wnets[k].prio=10; }
    wnet* w=&g_wnets[k]; wm_cpy(w->ssid,ssid,33); wm_cpy(w->pass,pass?pass:"",65); wm_cpy(w->user,user?user:"",65); w->eap=eap; if(hidden)w->hidden=1; wifi_saved_save(); return k; }
int wifi_saved_forget(const char* ssid){ int k=wifi_saved_find(ssid); if(k<0)return -1; for(int i=k;i<g_wnet_n-1;i++)g_wnets[i]=g_wnets[i+1]; g_wnet_n--; for(int z=0;z<(int)sizeof(wnet);z++)((u8*)&g_wnets[g_wnet_n])[z]=0; wifi_saved_save(); if(g_wm_cur==k)g_wm_cur=-1; else if(g_wm_cur>k)g_wm_cur--; return 0; }
void wifi_saved_prio(const char* ssid,int delta){ int k=wifi_saved_find(ssid); if(k<0)return; int p=g_wnets[k].prio+delta; g_wnets[k].prio=p<0?0:p>99?99:p; wifi_saved_save(); }

int wifi_up(void){
    if(g_wm_up&&g_rxnum)return 0;
    u32 m=rtw_find_mmio(); if(!m){ wm_status("No Wi-Fi adapter",0); return -1; }
    if(!g_rxnum){ static const char* const S[5]={"wifi init","wifi allbuilt","wifi macinit","wifi fw","wifi rings"};
        for(int k=0;k<5;k++){ char c[24]; wm_cpy(c,S[k],24); char st[40],*p=st; p=scopy(p,"Starting Wi-Fi ("); p=udec(p,(u32)(k+1)); p=scopy(p," of 5)"); *p=0; wm_status(st,0); ui_pump(); g_chquiet=1; run_cmd(c); g_chquiet=0; } }
    if(!g_rxnum){ wm_status("Wi-Fi did not start - open the Terminal and run: wifi init",0); return -2; }
    g_wm_up=1; if(g_wm_state==WM_OFF)g_wm_state=WM_IDLE; wm_status("Wi-Fi is on",0); klog("wifi: radio started"); return 0; }

void wifi_radio_off(void){
    if(g_wm_lps)wm_lps(0); wm_pending=0; if(sc_on)scan_finish();
    u32 m=rtw_find_mmio(); if(m&&g_wm_state==WM_CONNECTED){ rtw_ra_disconnect(m); }
    if(wnd_is_up())wnd_down(); if(g_netdev_wifi)netdev_set(NETDEV_ETH);
    if(m&&g_rxnum){ rtw_w16(m,0x0522,0xFFFF); u32 cr=rtw_r32(m,0x0100); cr&=~((1u<<2)|(1u<<3)); rtw_w32(m,0x0100,cr); }
    g_wm_state=WM_OFF; g_wifi_conn[0]=0; wm_status("Wi-Fi is off",0); }
void wifi_user_power(int on){ wm_useroff=!on; }
void wifi_radio_on(void){ u32 m=rtw_find_mmio(); if(m&&g_rxnum){ rtw_w16(m,0x0522,0); u32 cr=rtw_r32(m,0x0100); cr|=(1u<<2)|(1u<<3); rtw_w32(m,0x0100,cr); } if(g_wm_state==WM_OFF)g_wm_state=WM_IDLE; wm_next_try=g_ticks; }

static const u8 CH2[13]={1,6,11,2,3,4,5,7,8,9,10,12,13};
static const u8 CH5[24]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,149,153,157,161,165};
static int sc_on=0, sc_i=0, sc_n=0, sc_reps=0; static u64 sc_t=0, sc_start=0; static u8 sc_list[40];
int wifi_scan_busy(void){ return sc_on; }
int wifi_scan_start(int full){
    if(sc_on)return 0; if(wifi_up()<0)return -1; if(g_airplane){ wm_status("Airplane mode is on",0); return -1; }
    u32 m=rtw_find_mmio(); sc_n=0; for(int i=0;i<(full?13:3);i++)sc_list[sc_n++]=CH2[i]; if(g_wm_5g)for(int i=0;i<24;i++)sc_list[sc_n++]=CH5[i];
    if(g_wm_lps)wm_lps(0); sc_on=1; sc_i=0; sc_reps=0; sc_start=g_ticks; g_wifi_scanning=1; g_chquiet=1; if(g_wm_state!=WM_CONNECTED)rtw_rx_ring_up(m); rtw_set_channel_2g(m,sc_list[0]); sc_t=g_ticks; wm_status("Looking for networks...",0); return 0; }
static void scan_finish(void){
    u32 m=rtw_find_mmio(); sc_on=0; g_wifi_scanning=0; g_chquiet=0;
    for(int i=0;i<g_apn;){ int keep=g_aps[i].last>=sc_start||(g_wm_state==WM_CONNECTED&&g_aps[i].ch==g_wm_ch); if(keep)i++; else g_aps[i]=g_aps[--g_apn]; }
    for(int i=0;i<g_apn;i++)for(int j=i+1;j<g_apn;j++) if(g_aps[j].rssi>g_aps[i].rssi){ scan_ap t=g_aps[i]; g_aps[i]=g_aps[j]; g_aps[j]=t; }
    if(g_wm_state==WM_CONNECTED&&m)rtw_set_channel_2g(m,g_wm_ch);
    char b[48],*p=b; p=scopy(p,"Found "); p=udec(p,(u32)g_apn); p=scopy(p,g_apn==1?" network":" networks"); *p=0; if(g_wm_state!=WM_CONNECTED)wm_status(b,0); }
static void scan_step(void){
    u32 m=rtw_find_mmio(); if(!m){ sc_on=0; g_wifi_scanning=0; return; }
    rtw_scan_drain(m,sc_list[sc_i]);
    if(g_ticks-sc_t<11)return;
    sc_i++; if(sc_i>=sc_n){ scan_finish(); return; }
    rtw_set_channel_2g(m,sc_list[sc_i]); sc_t=g_ticks; }
void wifi_scan_blocking(int full){ if(wifi_scan_start(full)<0)return; while(sc_on){ scan_step(); ui_pump(); } }

static int wm_dhcp(void){ g_dhcp_done=0; g_dhcp_ok=0; return dhcp_config(); }
static const char* wm_err(int r){ switch(r){ case 1:return "the network did not answer"; case 2:return "the network refused the connection"; case 3:return "no answer when joining"; case 4:return "the network rejected this device (security type)";
    case 5:return "the network did not start the secure handshake"; case 6:return "wrong password"; case 7:return "sign-in failed (user name or password)"; case 8:return "the network needs a certificate we do not support"; case 10:return "the network certificate changed (forget the network to trust it again)"; case 9:return "the network uses old TKIP security (not supported - switch the router to WPA2-AES)"; default:return "the handshake failed"; } }

static void wm_deauth(u32 m){ if(!m||g_wm_state!=WM_CONNECTED)return; u8 f[26]; int n=0; f[n++]=0xC0; f[n++]=0; f[n++]=0; f[n++]=0; for(int i=0;i<6;i++)f[n++]=g_wm_bssid[i]; for(int i=0;i<6;i++)f[n++]=rtw_r8(m,0x0610+i); for(int i=0;i<6;i++)f[n++]=g_wm_bssid[i]; f[n++]=0; f[n++]=0; f[n++]=3; f[n++]=0; rtw_tx_mgmt(m,f,n); rtw_delay_us(5000); }
static int wm_connect(const scan_ap* apin,const char* pass,const char* user,int save);
int wifi_connect_ap(int idx,const char* pass,const char* user,int save){ if(idx<0||idx>=g_apn)return -1; scan_ap ap=g_aps[idx]; return wm_connect(&ap,pass,user,save); }
static int wm_connect(const scan_ap* apin,const char* pass,const char* user,int save){
    if(g_airplane||(g_wm_up&&g_wm_state==WM_OFF)){ wm_status(g_airplane?"Airplane mode is on":"Wi-Fi is off",0); return -1; }
    u32 m=rtw_find_mmio(); if(!m||wifi_up()<0)return -1;
    if(sc_on)scan_finish(); wm_noauto[0]=0; if(g_wm_lps)wm_lps(0); wm_pending=0;
    scan_ap ap=*apin; char ssid[33]; wm_cpy(ssid,ap.ssid,33);
    if(g_wm_state==WM_CONNECTED){ wm_deauth(m); rtw_ra_disconnect(m); wnd_down(); }
    if(g_netdev_wifi)netdev_set(NETDEV_ETH);
    g_wm_state=WM_CONNECTING; wm_status("Connecting to ",ssid); ui_pump();
    g_chquiet=1; int r;
    if(ap.akm&AKM_8021X&&!(ap.akm&(AKM_PSK|AKM_SAE))){ int sv=wifi_saved_find(ssid); g_eap_pin_set=0; if(sv>=0&&g_wnets[sv].pinset){ for(int z=0;z<32;z++)g_eap_pin[z]=g_wnets[sv].pin[z]; g_eap_pin_set=1; } r=wifi_eap_join(m,&ap,ssid,user?user:"",pass?pass:""); }
    else if(ap.sec==0){ r=rtw_assoc(m,&ap,ssid); if(r==0){ u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i); wnd_setup_open(m,ap.bssid,mac,g_wifi_assoc_qos); g_wpa_ok=1; } }
    else r=rtw_wpa2_join(m,&ap,ssid,pass?pass:"");
    g_chquiet=0;
    if(r){ g_wm_state=WM_IDLE; { extern const char* g_eap_why; wm_status("Could not connect: ",(ap.akm&AKM_8021X)&&!(ap.akm&(AKM_PSK|AKM_SAE))&&g_eap_why&&g_eap_why[0]?g_eap_why:wm_err(r)); } g_wm_reason=r; return r; }
    { u8 rates[12]={2,4,11,22,12,18,24,36,48,72,96,108}; rtw_ra_connect(m,g_wifi_assoc_ht?ap.ht:0,ap.ch>14?rates+4:rates,ap.ch>14?8:12); }
    for(int i=0;i<6;i++)g_wm_bssid[i]=ap.bssid[i]; g_wm_ch=ap.ch;
    netdev_set(NETDEV_WIFI); wm_status("Getting an address from ",ssid); ui_pump();
    int ok=wm_dhcp();
    if(!ok&&g_wifi_ra){ g_wifi_ra=0; rtw_ra_connect(m,0,0,0); ok=wm_dhcp(); if(!ok)g_wifi_ra=1; else klog("wifi: fast rates did not work here - using safe rates"); }
    if(!ok){ wm_status("Connected, but no address from ",ssid); }
    g_wm_state=WM_CONNECTED; g_wm_since=g_ticks; wm_last_ok=g_ticks; wm_fails=0; wm_backoff=500; g_wm_reason=0;
    wm_cpy(g_wifi_conn,ssid,33);
    { int eapn=(ap.akm&AKM_8021X)&&!(ap.akm&(AKM_PSK|AKM_SAE));
      if(save){ int k=wifi_saved_add(ssid,pass,user,eapn,ap.hidden); g_wm_cur=k; } else g_wm_cur=wifi_saved_find(ssid);
      if(eapn&&g_wm_cur>=0&&!g_wnets[g_wm_cur].pinset){ for(int z=0;z<32;z++)g_wnets[g_wm_cur].pin[z]=g_eap_pin_new[z]; g_wnets[g_wm_cur].pinset=1; wifi_saved_save(); { char fp[96],*q=fp; q=scopy(q,"wifi: trusting the network certificate SHA-256 "); for(int z=0;z<8;z++)q=hx(q,g_eap_pin_new[z],2); q=scopy(q,"..."); *q=0; klog(fp); } } }
    if(ok){ wm_status("Connected to ",ssid); notify_from("Wi-Fi","Wi-Fi",g_wifi_conn); }
    g_wm_portal=0; if(ok){ static u8 hb[2048]; int n=http_fetch_to("connectivitycheck.gstatic.com","/generate_204",hb,(int)sizeof(hb)); if(n>12){ int code=(hb[9]-'0')*100+(hb[10]-'0')*10+(hb[11]-'0'); if(code!=204&&code>=200&&code<400){ g_wm_portal=1; notify_from("Wi-Fi","Sign in to the network","Open the browser to finish signing in"); wm_status("Sign-in needed: ",ssid); } } }
    klog(ok?"wifi: connected":"wifi: associated without address"); return 0; }

int wifi_connect_ssid(const char* ssid,const char* pass,const char* user,int hidden){
    int k=-1; for(int i=0;i<g_apn;i++) if(wm_eq(g_aps[i].ssid,ssid)){ if(k<0||g_aps[i].rssi>g_aps[k].rssi)k=i; }
    if(k<0){ wifi_scan_blocking(1); for(int i=0;i<g_apn;i++) if(wm_eq(g_aps[i].ssid,ssid)){ if(k<0||g_aps[i].rssi>g_aps[k].rssi)k=i; } }
    if(k<0&&hidden){ for(int i=0;i<g_apn;i++) if(g_aps[i].hidden&&!g_aps[i].ssid[0]){ scan_ap t=g_aps[i]; wm_cpy(t.ssid,ssid,33); int hr=wm_connect(&t,pass,user,1); if(hr>0)return hr; if(hr==0){ for(int z=0;z<g_apn;z++) if(!g_aps[z].ssid[0]&&g_aps[z].hidden){ int eq=1; for(int b=0;b<6;b++) if(g_aps[z].bssid[b]!=t.bssid[b])eq=0; if(eq)wm_cpy(g_aps[z].ssid,ssid,33); } return 0; } } }
    if(k<0){ wm_status("Network not found: ",ssid); return -1; }
    return wifi_connect_ap(k,pass,user,1); }

void wifi_disconnect(void){ if(g_wm_lps)wm_lps(0); wm_pending=0; wm_cpy(wm_noauto,g_wifi_conn,33); u32 m=rtw_find_mmio(); wm_deauth(m); if(m&&g_wm_state==WM_CONNECTED)rtw_ra_disconnect(m); if(wnd_is_up())wnd_down(); if(g_netdev_wifi)netdev_set(NETDEV_ETH); g_wm_state=g_wm_up?WM_IDLE:WM_OFF; g_wifi_conn[0]=0; g_wm_cur=-1; wm_next_try=g_ticks+6000; for(int z=0;z<32;z++)g_pmk[z]=0; for(int z=0;z<48;z++)g_ptk[z]=0; wm_status("Disconnected",0); }

static int wm_lps(int on){ u32 m=rtw_find_mmio(); if(!m)return -1; extern int rtw_h2c_cmd(u32 m,u32 w0,u32 w1);
    u32 w0=on?(0x20u|(1u<<8)|(1u<<16)|(2u<<20)|(1u<<24)):(0x20u|(1u<<24)); u32 w1=on?0x00u:(0x0Cu<<8);
    int r=rtw_h2c_cmd(m,w0,w1); if(r==0||!on)g_wm_lps=on&&r==0; if(!on){ for(int t=0;t<5&&(rtw_r8(m,0x0604)&0x80);t++)rtw_delay_us(20000); rtw_w8(m,0x0604,rtw_r8(m,0x0604)&~0x80); } return r; }
static void wm_pre_tx(void){ if(g_wm_lps){ wm_lps(0); rtw_delay_us(2000); } }
int wifi_powersave(int on){ g_wm_ps=on; if(!on&&g_wm_lps)wm_lps(0); return 0; }
static void wm_ps_tick(void){ static u32 ltx=0; static u64 lt=0; if(g_wnd_tx_frames!=ltx){ ltx=g_wnd_tx_frames; lt=g_ticks; return; }
    if(g_wm_ps&&!g_wm_lps&&!sc_on&&g_ticks-lt>500&&g_ticks-g_wm_since>1000)wm_lps(1); }

static int wm_pick(void){
    int best=-1, bs=-100000;
    for(int i=0;i<g_apn;i++){ int k=wifi_saved_find(g_aps[i].ssid); if(k<0||!g_wnets[k].autoc)continue; if(wm_noauto[0]&&wm_eq(wm_noauto,g_aps[i].ssid))continue; if(wm_failt[k]&&g_ticks-wm_failt[k]<18000&&g_wnet_n>1)continue; int s=g_wnets[k].prio*1000+g_aps[i].rssi+(g_aps[i].ch>14&&g_aps[i].rssi>-70?15:0); if(s>bs){ bs=s; best=i; } }
    return best; }

static void wm_lost(const char* why){ if(g_wm_lps)wm_lps(0); g_wm_lps=0; u32 m=rtw_find_mmio(); if(m)rtw_ra_disconnect(m); if(wnd_is_up())wnd_down(); if(g_netdev_wifi)netdev_set(NETDEV_ETH);
    g_wm_state=WM_IDLE; wm_status("Connection lost (",why); { int L=0; while(g_wifi_status[L])L++; if(L<90){ g_wifi_status[L]=')'; g_wifi_status[L+1]=0; } }
    notify_from("Wi-Fi","Wi-Fi connection lost","Reconnecting..."); klog("wifi: connection lost - reconnecting"); wm_next_try=g_ticks+100; wm_backoff=300; }

void wifi_mgr_tick(void){
    static u64 last=0;
    if(!wm_loaded&&g_ticks>300)wifi_saved_load();
    if(g_airplane&&(sc_on||wm_pending)){ if(sc_on)scan_finish(); wm_pending=0; }
    if(sc_on){ scan_step(); return; }
    if(wm_pending&&g_wifi_entry){ wm_pending=0; wm_next_try=g_ticks+1000; }
    if(wm_pending&&g_wm_state==WM_IDLE&&g_ticks>=wm_next_try){ wm_pending=0; int k=wm_pick();
        if(k>=0){ int s=wifi_saved_find(g_aps[k].ssid); int r=wifi_connect_ap(k,g_wnets[s].pass,g_wnets[s].user,0); if(r==0)return; wm_fails++; s=wifi_saved_find(g_aps[k].ssid); if(s>=0){ wm_failt[s]=g_ticks; if(r==6||r==7||r==10){ g_wnets[s].autoc=0; wifi_saved_save(); notify_from("Wi-Fi","Wi-Fi password not accepted",g_wnets[s].ssid); } } }
        else for(int s=0;s<g_wnet_n;s++) if(g_wnets[s].hidden&&g_wnets[s].autoc){ for(int i=0;i<g_apn;i++) if(g_aps[i].hidden&&!g_aps[i].ssid[0]){ scan_ap t=g_aps[i]; wm_cpy(t.ssid,g_wnets[s].ssid,33); int r=wm_connect(&t,g_wnets[s].pass,g_wnets[s].user,0); if(r==0)return; goto wm_hid_done; } } wm_hid_done:;
        wm_backoff=wm_backoff*2; if(wm_backoff>6000)wm_backoff=6000; wm_next_try=g_ticks+wm_backoff; return; }
    if(g_ticks-last<20)return; last=g_ticks;
    { static int air=0; if(g_airplane){ if(g_wm_state!=WM_OFF&&g_wm_up)wifi_radio_off(); air=1; return; } if(air){ air=0; if(g_wm_up&&!wm_useroff){ wifi_radio_on(); wm_next_try=g_ticks; } } }
    if(g_wm_state==WM_CONNECTED){
        { extern void (*g_wnd_pre_tx)(void); g_wnd_pre_tx=wm_pre_tx; }
        net_idle_poll(); wm_ps_tick(); { static u64 rr=0; if(g_ticks-rr>=200){ rr=g_ticks; extern void rtw_ra_rssi(u32,int); if(g_wifi_rssi>-127&&!g_wm_lps)rtw_ra_rssi(rtw_find_mmio(),g_wifi_rssi); } }
        { extern int g_wifi_ptk_rekey; if(g_wifi_ptk_rekey){ g_wifi_ptk_rekey=0; wm_lost("new keys"); return; } }
        if(!wnd_is_up()){ wm_lost(g_wnd_lost?"the network disconnected us":"link down"); return; }
        u64 lim=(u64)g_wnd_bcn_int*12/10; if(lim<300)lim=300; if(lim>1500)lim=1500;
        if(g_ticks-g_wnd_beacon_t>lim&&g_wnd_beacons>0){ wm_lost("no signal"); return; }
        if(g_ticks-g_wm_since>6000&&!g_dhcp_ok){ static u64 lr=0; if(g_ticks-lr>3000){ lr=g_ticks; if(wm_dhcp())wm_status("Connected to ",g_wifi_conn); } }
        return; }
    if(g_wm_state==WM_OFF&&!g_wm_up){ if(!g_wm_auto||!g_wnet_n||g_ticks<1500)return; if(g_ticks<wm_next_try)return; if(rtw_find_mmio()==0){ wm_next_try=g_ticks+60000; return; } if(wifi_up()<0){ wm_next_try=g_ticks+30000; return; } }
    if(g_wm_state==WM_IDLE&&g_wm_auto&&g_wnet_n&&g_ticks>=wm_next_try&&!g_wifi_entry){
        if(wifi_scan_start(1)==0)wm_pending=1; else wm_next_try=g_ticks+3000; } }

const char* wifi_state_name(void){ switch(g_wm_state){ case WM_OFF:return "Off"; case WM_IDLE:return "Not connected"; case WM_CONNECTING:return "Connecting"; case WM_CONNECTED:return g_wm_portal?"Sign-in needed":"Connected"; default:return "?"; } }
int wifi_signal_bars(int rssi){ if(rssi<=-127)return 0; if(rssi>=-55)return 4; if(rssi>=-65)return 3; if(rssi>=-75)return 2; return 1; }
int wifi_rate_mbps(void){ if(g_wm_state!=WM_CONNECTED)return 0; if(!g_wifi_ra)return g_wifi_band5?6:1; return g_wifi_assoc_ht?72:54; }

int wifi_info_text(char* o,int cap){ char* p=o; char* e=o+cap-200; extern u32 g_wnd_rx_frames, g_wnd_tx_frames, g_wnd_rx_drop; extern int g_wifi_rekeys; extern int g_wpa_akm;
    u32 m=rtw_find_mmio(); p=scopy(p,"Adapter="); p=scopy(p,m?"Realtek RTL8821CE (802.11ac 1x1, 2.4 + 5 GHz)":"none found"); *p++='\n';
    p=scopy(p,"Radio="); p=scopy(p,g_rxnum?(g_wm_state==WM_OFF?"off":"on"):"not started"); *p++='\n';
    p=scopy(p,"State="); p=scopy(p,wifi_state_name()); *p++='\n';
    if(rtw_efuse_valid){ p=scopy(p,"MAC="); for(int k=0;k<6;k++){ p=hx(p,rtw_logmap[0xD0+k],2); if(k<5)*p++=':'; } *p++='\n'; }
    if(g_wm_state==WM_CONNECTED){ p=scopy(p,"Network="); p=scopy(p,g_wifi_conn); p=scopy(p,"\nChannel="); p=udec(p,g_wm_ch); p=scopy(p,g_wm_ch>14?" (5 GHz)":" (2.4 GHz)");
        p=scopy(p,"\nSignal="); if(g_wifi_rssi>-127){ *p++='-'; p=udec(p,(u32)(-g_wifi_rssi)); p=scopy(p," dBm"); } else p=scopy(p,"unknown");
        p=scopy(p,"\nSpeed=up to "); p=udec(p,(u32)wifi_rate_mbps()); p=scopy(p," Mbit/s\nSecurity="); p=scopy(p,g_wnd_open?"open":g_wpa_akm==8?"WPA3-Personal (SAE)":g_wpa_akm==1?"WPA2-Enterprise (PEAP-MSCHAPv2)":"WPA2-Personal");
        p=scopy(p,"\nAddress="); p=ipstr(p,my_ip); p=scopy(p,"\nGroupKeyRenewals="); p=udec(p,(u32)g_wifi_rekeys); p=scopy(p,"\nFramesIn="); p=udec(p,g_wnd_rx_frames); p=scopy(p,"\nFramesOut="); p=udec(p,g_wnd_tx_frames); p=scopy(p,"\nDropped="); p=udec(p,g_wnd_rx_drop); *p++='\n'; }
    p=scopy(p,"AutoConnect="); p=scopy(p,g_wm_auto?"on":"off"); p=scopy(p,"\n5GHz="); p=scopy(p,g_wm_5g?"on":"off"); p=scopy(p,"\nPowerSaving="); p=scopy(p,g_wm_ps?(g_wm_lps?"on (sleeping now)":"on"):"off"); p=scopy(p,"\nRates="); p=scopy(p,g_wifi_ra?"automatic":"safe fixed"); *p++='\n';
    p=scopy(p,"SavedNetworks="); p=udec(p,(u32)g_wnet_n); p=scopy(p," (names only here - passwords stay in /etc/noovex/wifi, root only)\n");
    for(int i=0;i<g_wnet_n&&p<e;i++){ p=scopy(p,"  "); p=scopy(p,g_wnets[i].ssid); *p++='\n'; }
    p=scopy(p,"Seen=\n"); for(int i=0;i<g_apn&&p<e;i++){ p=scopy(p,"  "); p=scopy(p,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>"); p=scopy(p,"  ch "); p=udec(p,g_aps[i].ch); if(g_aps[i].rssi>-127){ p=scopy(p,"  -"); p=udec(p,(u32)(-g_aps[i].rssi)); p=scopy(p," dBm"); } *p++='\n'; }
    *p=0; return (int)(p-o); }
