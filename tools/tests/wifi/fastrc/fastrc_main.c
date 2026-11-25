typedef struct { char ssid[33]; char pass[65]; char user[65]; int autoc; } wnet;
static u64 g_ticks=1000, sc_start=0, wm_next_try=0; static int g_apn=0, g_wnd_open=0, wm_autoj=0, g_wpa_pmk_hit=0, sc_ovn=0, scan_rc=0, conn_rc=0, conn_n=0; static const u8* sc_ov=0; static scan_ap g_aps[8]; static wnet g_wnets[2]; static char wm_noauto[33];
static u8 txf[128]; static int txn=0; static char lastlog[256]; static scan_ap conn_ap;
static void klog(const char* s){ strncpy(lastlog,s,255); }
static void wm_status(const char* a,const char* b){ (void)a; (void)b; }
static char* scopy(char* p,const char* s){ while(*s)*p++=*s++; return p; }
static char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
static u8 rtw_r8(u32 m,u32 r){ (void)m; return (u8)(0xA0+(r-0x0610)); }
static int rtw_tx_mgmt(u32 m,const u8* f,int n){ (void)m; memcpy(txf,f,n); txn=n; return 0; }
static int wifi_saved_find(const char* s){ return strcmp(s,g_wnets[0].ssid)==0?0:-1; }
static const char* wm_err(int r){ return r==6?"wrong password":"no answer"; }
static int wifi_scan_start(int full){ (void)full; if(scan_rc)return -1; if(sc_ovn!=1||!sc_ov)return -1; sc_start=g_ticks; return 0; }
static int wm_connect(const scan_ap* ap,const char* p,const char* u,int sv){ (void)p; (void)u; (void)sv; conn_ap=*ap; conn_n++; if(!conn_rc)g_wpa_pmk_hit=1; return conn_rc; }
/*MAIN*/
static int fails=0;
#define CK(c,msg) do{ if(!(c)){ printf("FAIL %s\n",msg); fails++; } }while(0)
int main(void){
    strcpy(g_wnets[0].ssid,"HomeNet"); strcpy(g_wnets[0].pass,"password1"); g_wnets[0].autoc=1;
    memset(&fr_ap,0,sizeof fr_ap); strcpy(fr_ap.ssid,"HomeNet"); fr_ap.ch=36; for(int i=0;i<6;i++)fr_ap.bssid[i]=(u8)(0x10+i); fr_ok=1;
    wm_fr_probe(1);
    CK(txn==24+2+7+2+8,"probe length"); CK(txf[0]==0x40,"probe subtype"); CK(txf[4]==0x10&&txf[9]==0x15,"probe DA = cached BSSID"); CK(txf[10]==0xA0&&txf[15]==0xA5,"probe SA = our MAC"); CK(txf[16]==0x10,"probe BSSID");
    CK(txf[24]==0&&txf[25]==7&&!memcmp(txf+26,"HomeNet",7),"directed SSID IE"); CK(txf[33]==1&&txf[34]==8&&txf[35]==0x8C,"5 GHz rates (no 11b)");
    fr_ap.ch=6; wm_fr_probe(1); CK(txf[35]==0x82,"2.4 GHz rates"); fr_ap.ch=36;
    CK(wm_fr_start()==0&&fr_on==1&&sc_ovn==0&&fr_arm==0,"fast start scans one channel");
    g_apn=1; g_aps[0]=fr_ap; g_aps[0].ssid[0]=0; g_aps[0].last=sc_start-1; CK(wm_fr_seen()<0,"stale entry not seen"); g_aps[0].last=sc_start; CK(wm_fr_seen()==0,"fresh entry seen by BSSID");
    g_ticks+=40; fr_t0=g_ticks-90; wm_fr_join(); CK(conn_n==1&&!strcmp(conn_ap.ssid,"HomeNet"),"hidden SSID filled from cache"); CK(strstr(lastlog,"in 900 ms")&&strstr(lastlog,"saved key reused")&&strstr(lastlog,"within"),"timing log");
    CK(fr_fail==0&&wifi_fast_state()==1,"success resets failures");
    conn_rc=6; for(int i=0;i<3;i++){ fr_ok=1; CK(wm_fr_start()==0||i==3,"start"); wm_fr_join(); }
    CK(fr_fail==3&&fr_dis==1&&wifi_fast_state()==2,"auto-off after 3 failures"); CK(wm_next_try==g_ticks,"falls back to full scan now"); CK(wm_fr_start()<0,"disabled path refuses");
    wifi_fast_set(1); CK(fr_dis==0&&wifi_fast_state()==1,"wifi fast on re-enables"); wifi_fast_set(0); CK(wifi_fast_state()==0&&wm_fr_start()<0,"wifi fast off");
    wifi_fast_set(1); conn_rc=0; g_apn=0; wm_fr_start(); wm_fr_join(); CK(strstr(lastlog,"no answer on the last channel")!=0,"no answer -> fallback");
    strcpy(wm_noauto,"HomeNet"); CK(wm_fr_start()<0,"user-disconnected network not fast-joined"); wm_noauto[0]=0;
    g_wnets[0].autoc=2; CK(wm_fr_start()<0,"blocked network (bad password) not fast-joined"); g_wnets[0].autoc=1;
    scan_rc=1; CK(wm_fr_start()<0&&fr_on==0,"scan refused"); scan_rc=0;
    printf(fails?"fast reconnect: %d FAILED\n":"fast reconnect: all checks passed\n",fails); return fails!=0; }
