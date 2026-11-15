static u64 g_ticks=100000; static int g_uid=1000; static char g_file[8192]; static int g_flen=-1, g_writes=0;
static char* scopy(char* d,const char* s){ while(*s)*d++=*s++; return d; }
static char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; return p; }
static void wm_mkdir(const char* a,const char* b,unsigned m){ (void)a; (void)b; (void)m; }
static int ux_write_text(const char* p,const char* b,int n,unsigned m){ (void)p; (void)m; if(g_uid!=0)return -1; memcpy(g_file,b,n); g_flen=n; g_writes++; return n; }
static int ux_read_file(const char* p,char* o,int cap){ (void)p; if(g_uid!=0||g_flen<0)return -1; int n=g_flen<cap-1?g_flen:cap-1; memcpy(o,g_file,n); o[n]=0; return n; }
typedef struct { int uid,gid; unsigned mode; } fsent; static fsent FS[1];
static int ux_lookup(const char* p,int* isd,int f){ (void)p; (void)f; *isd=0; return 0; }
static void dt_save(void){}
static void notify_from(const char* a,const char* b,const char* c){ (void)a; (void)b; (void)c; }
scan_ap g_aps[24]; int g_apn=0;
/*MAIN*/
static int fails=0;
#define CHECK(c,m) do{ if(c)printf("  %s ok\n",m); else { printf("  %s FAIL\n",m); fails++; } }while(0)
static void ap(const char* s,int rssi,int ch){ scan_ap* a=&g_aps[g_apn++]; memset(a,0,sizeof(*a)); strcpy(a->ssid,s); a->rssi=(signed char)rssi; a->ch=(u8)ch; }
static void reboot(void){ memset(g_wnets,0,sizeof g_wnets); g_wnet_n=0; wm_loaded=0; wm_useroff=0; wm_last[0]=0; g_wm_auto=1; g_wm_5g=1; wm_fails=0; }
int main(void){
    const char* old="NVWIFI1\nauto=1 5g=0\nnet 10 0 1 0 486f6d65 7365637265743132 - -\nnet 10 0 0 0 43616665 - - -\n";
    memcpy(g_file,old,strlen(old)); g_flen=(int)strlen(old);
    wifi_saved_load();
    CHECK(g_wnet_n==2&&!strcmp(g_wnets[0].ssid,"Home")&&!strcmp(g_wnets[0].pass,"secret12")&&g_wnets[0].autoc&&!g_wnets[1].autoc,"old file format still loads");
    CHECK(g_wm_auto==1&&g_wm_5g==0&&wm_useroff==0&&!wm_last[0],"old header: auto/5g read, Wi-Fi not off, no last network");
    wm_cpy(wm_last,"Cafe",33); wifi_user_power(0);
    CHECK(wm_useroff==1&&g_writes==1&&strstr(g_file," off=1 last=43616665\n"),"wifi off is written to the file with the last network");
    CHECK(g_uid==1000,"user id restored after saving");
    reboot(); wifi_saved_load();
    CHECK(wm_useroff==1&&!strcmp(wm_last,"Cafe")&&g_wnet_n==2&&g_wm_5g==0,"after reboot: Wi-Fi stays off, last network remembered");
    int w0=g_writes; wifi_user_power(0); CHECK(g_writes==w0,"repeating wifi off does not rewrite the file");
    g_wm_auto=0; wifi_user_power(1); CHECK(wm_useroff==0&&wm_fails==0&&wm_next_try==g_ticks,"wifi on clears off and retries now");
    reboot(); wifi_saved_load(); CHECK(wm_useroff==0&&g_wm_auto==0,"after reboot: Wi-Fi on, auto setting kept");
    reboot(); g_flen=-1; g_wm_auto=0; wm_useroff=1; wifi_saved_save();
    CHECK(wm_loaded&&g_wm_auto==0&&wm_useroff==1&&strstr(g_file,"auto=0 5g=1 off=1"),"saving before the file was loaded keeps the new settings");
    reboot(); g_flen=-1; wifi_saved_load(); wifi_saved_add("Home","pw123456","",0,0); wifi_saved_add("Office","pw654321","",0,0); wifi_saved_add("Cafe","","",0,0); g_wnets[2].autoc=0;
    g_apn=0; ap("Office",-50,6); ap("Home",-70,1); ap("Cafe",-30,11); ap("Other",-20,1);
    CHECK(wm_pick()==0,"equal priority: strongest saved auto network wins");
    wm_cpy(wm_last,"Home",33); CHECK(wm_pick()==1,"last connected network is the default even when weaker");
    wm_failt[0]=g_ticks; CHECK(wm_pick()==0,"a network that just failed is skipped when others exist");
    wm_failt[0]=0; g_apn=0; ap("Cafe",-30,11); CHECK(wm_pick()==-1,"a manual-only network is never auto-joined");
    wm_fails=1; u64 d1=wm_fail_delay(); wm_fails=2; u64 d2=wm_fail_delay(); wm_fails=5; u64 d5=wm_fail_delay(); wm_fails=7; u64 d7=wm_fail_delay(); wm_fails=WM_MAXFAIL; u64 d8=wm_fail_delay();
    CHECK(d1>=3000&&d2==2*d1&&d5<=30000&&d7==30000&&d8==60000,"failed auto-joins back off: 30 s, 60 s ... 5 min, then 10 min");
    u64 tot=0; for(wm_fails=1;wm_fails<=20;wm_fails++)tot+=wm_fail_delay();
    CHECK(tot>=20ULL*3000,"never more than one blocking join per 30 s");
    printf(fails?"mgr FAILED (%d)\n":"mgr ok\n",fails); return fails!=0; }
