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
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_coex.h"
#include "include/nvx/net/stack/tcpip.h"
#include "include/nvx/net/wifi/wifi_netdev.h"
#include "include/nvx/net/wifi/wpa2.h"
#include "include/nvx/net/wifi/wifi_mgr.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/eap.h"
#include "include/nvx/net/wifi/wpa_sm.h"
#include "include/nvx/graphics/ui/wm.h"
#include "include/nvx/apps/system/settings.h"
#include "include/nvx/arch/x86_64/cpu/percpu.h"
#include "include/nvx/drivers/bus/pci.h"
#include "include/nvx/drivers/iommu/vtd.h"
#include "include/nvx/apps/nwe_api.h"
#include "include/nvx/net/wifi/regdb.h"
#include "include/nvx/net/wifi/wifi_ps.h"
#include "include/nvx/net/wifi/wifi_ampdu.h"
extern int bat_present, bat_charging;

extern volatile u64 g_ticks;
extern int g_airplane;
u32 rtw_find_mmio(void);
int rtw_wpa2_join(u32 m,const scan_ap* ap,const char* ssid,const char* pass);
int wifi_eap_join(u32 m,const scan_ap* ap,const char* ssid,const char* user,const char* pass);
void run_cmd(char* line);
void net_idle_poll(void);
void ui_pump(void);

wnet g_wnets[WIFI_MAXNET]; int g_wnet_n=0;
int g_wm_state=WM_OFF, g_wm_auto=1, g_wm_5g=1, g_wm_ps=0, g_wm_lps=0, g_wm_portal=0, g_wm_cur=-1, g_wm_reason=0, g_wm_up=0;
u8 g_wm_bssid[6]; u8 g_wm_ch=0; u64 g_wm_since=0;
static u64 wm_next_try=0, wm_backoff=500, wm_last_ok=0;
static int wm_fails=0, wm_loaded=0, wm_pending=0, wm_useroff=0, wm_autoj=0, wm_bootk=0; static u64 wm_failt[WIFI_MAXNET]; static u8 wm_pwbad[WIFI_MAXNET]; static char wm_noauto[33], wm_last[33];
#define WM_STABLE 12000
#define WM_MAXFAIL 8
extern int g_wifi_entry;
static int wm_lps(int on);
static int sc_on; static void scan_finish(void);
int g_wm_fast=1; static int fr_ok=0, fr_arm=0, fr_on=0, fr_go=0, fr_fail=0, fr_dis=0, fr_np=0, fr_wk=0; static scan_ap fr_ap; static u64 fr_t0=0, fr_wake=0;

static void wm_cpy(char* d,const char* s,int cap){ int i=0; while(s&&s[i]&&i<cap-1){ d[i]=s[i]; i++; } d[i]=0; }
static int wm_eq(const char* a,const char* b){ while(*a&&*a==*b){ a++; b++; } return *a==*b; }
static void wm_status(const char* a,const char* b){ char* p=g_wifi_status; char* e=g_wifi_status+90; for(int i=0;a&&a[i]&&p<e;i++)*p++=a[i]; for(int i=0;b&&b[i]&&p<e;i++)*p++=b[i]; *p=0; }
void wm_status_set(const char* a){ wm_status(a,0); }

#define WDIR "/etc/noovex/wifi"
#define WFILE WDIR "/networks"
static char* wm_hex(char* p,const char* s){ static const char* H="0123456789abcdef"; for(int i=0;s[i];i++){ u8 c=(u8)s[i]; *p++=H[c>>4]; *p++=H[c&15]; } if(!s[0])*p++='-'; return p; }
static int wm_unhex(const char* s,int n,char* o,int cap){ int k=0; if(n==1&&s[0]=='-'){ o[0]=0; return 0; } for(int i=0;i+1<n&&k<cap-1;i+=2){ int a=s[i],b=s[i+1]; a=a<='9'?a-'0':(a|32)-'a'+10; b=b<='9'?b-'0':(b|32)-'a'+10; if(a<0||a>15||b<0||b>15)break; o[k++]=(char)(a*16+b); } o[k]=0; return k; }
static void wm_mkdir(const char* parent,const char* name,unsigned mode){ dt_init(); int d=dt_resolve_dir(parent); if(d<0)return; int c=dt_child(d,name); if(c<0)c=dt_mkdir_at(d,name); if(c>=0){ DT[c].uid=0; DT[c].gid=0; DT[c].mode=mode; } }

void wifi_saved_load(void);
void wifi_saved_save(void){ if(!wm_loaded){ int a=g_wm_auto, f=g_wm_5g, o=wm_useroff; char rc[3]; const char* rs=regdb_setting(); rc[0]=rs[0]; rc[1]=rs[1]; rc[2]=0; wifi_saved_load(); g_wm_auto=a; g_wm_5g=f; wm_useroff=o; regdb_set_country(rc); }
    int su=g_uid; g_uid=0; wm_mkdir("/etc","noovex",0040755u); wm_mkdir("/etc/noovex","wifi",0040700u);
    static char b[8192]; char* p=b; p=scopy(p,"NVWIFI1\n"); p=scopy(p,"auto="); *p++=g_wm_auto?'1':'0'; p=scopy(p," 5g="); *p++=g_wm_5g?'1':'0'; p=scopy(p," off="); *p++=wm_useroff?'1':'0'; if(wm_last[0]){ p=scopy(p," last="); p=wm_hex(p,wm_last); } p=scopy(p," cc="); p=scopy(p,regdb_setting()); *p++='\n';
    for(int i=0;i<g_wnet_n&&p<b+7600;i++){ wnet* w=&g_wnets[i]; p=scopy(p,"net "); p=udec(p,(u32)w->prio); *p++=' '; *p++=(char)('0'+w->hidden); *p++=' '; *p++=(char)('0'+w->autoc); *p++=' '; *p++=(char)('0'+w->eap); *p++=' ';
        p=wm_hex(p,w->ssid); *p++=' '; p=wm_hex(p,w->pass); *p++=' '; p=wm_hex(p,w->user); *p++=' '; if(w->pinset){ static const char* H="0123456789abcdef"; for(int z=0;z<32;z++){ *p++=H[w->pin[z]>>4]; *p++=H[w->pin[z]&15]; } } else *p++='-'; *p++='\n'; }
    ux_write_text(WFILE,b,(int)(p-b),0100600u); int isd=0; int k=ux_lookup(WFILE,&isd,0); if(k>=0&&!isd){ FS[k].uid=0; FS[k].gid=0; FS[k].mode=0100600u; }
    for(int z=0;z<(int)sizeof(b);z++)b[z]=0; dt_save(); g_uid=su; }

void wifi_saved_load(void){
    wm_loaded=1; int su=g_uid; g_uid=0; static char b[8192]; int n=ux_read_file(WFILE,b,(int)sizeof(b)-1); g_uid=su; g_wnet_n=0; if(n<=0)return; b[n]=0;
    int i=0; while(i<n){ int e=i; while(e<n&&b[e]!='\n')e++;
        if(e-i>5&&b[i]=='a'&&b[i+1]=='u'){ for(int k=i;k<e;k++){ if(b[k]=='='&&k>i+3&&b[k-1]=='o'&&b[k-2]=='t')g_wm_auto=b[k+1]=='1'; if(b[k]=='='&&b[k-1]=='g'&&b[k-2]=='5')g_wm_5g=b[k+1]=='1'; if(b[k]=='='&&k>i+3&&b[k-1]=='f'&&b[k-2]=='f'&&b[k-3]=='o')wm_useroff=b[k+1]=='1'; if(b[k]=='='&&b[k-1]=='c'&&b[k-2]=='c'&&k+2<e){ char rc[3]={b[k+1],b[k+2],0}; regdb_set_country(rc); }
            if(b[k]=='='&&k>i+4&&b[k-1]=='t'&&b[k-2]=='s'&&b[k-3]=='a'&&b[k-4]=='l'){ int s=k+1, t=s; while(t<e&&b[t]!=' ')t++; wm_unhex(b+s,t-s,wm_last,33); } } }
        else if(e-i>8&&b[i]=='n'&&b[i+1]=='e'&&b[i+2]=='t'&&b[i+3]==' '&&g_wnet_n<WIFI_MAXNET){
            const char* f[8]; int fl[8]; int nf=0; int k=i+4; while(k<e&&nf<8){ while(k<e&&b[k]==' ')k++; f[nf]=b+k; int s=k; while(k<e&&b[k]!=' ')k++; fl[nf++]=k-s; }
            if(nf>=7){ wnet* w=&g_wnets[g_wnet_n]; for(int z=0;z<(int)sizeof(*w);z++)((u8*)w)[z]=0;
                int pr=0; for(int z=0;z<fl[0]&&z<3;z++){ if(f[0][z]<'0'||f[0][z]>'9')break; pr=pr*10+(f[0][z]-'0'); } if(pr>99)pr=99; w->prio=pr; w->hidden=f[1][0]=='1'; w->autoc=f[2][0]=='0'?0:f[2][0]=='2'?2:1; w->eap=f[3][0]=='1';
                wm_unhex(f[4],fl[4],w->ssid,33); wm_unhex(f[5],fl[5],w->pass,65); wm_unhex(f[6],fl[6],w->user,65);
                if(nf>=8&&fl[7]==64){ char t[33]; if(wm_unhex(f[7],64,t,33)==32){ for(int z=0;z<32;z++)w->pin[z]=(u8)t[z]; w->pinset=1; } }
                if(w->ssid[0])g_wnet_n++; } }
        i=e+1; } for(int z=0;z<(int)sizeof(b);z++)b[z]=0; }

static int wm_eq_old(const char* sv,const char* sc){ while(*sv&&*sc){ u8 a=(u8)*sv, b=(u8)*sc; if(a!=b&&!(a=='.'&&(b>=0x80||b<32)))return 0; sv++; sc++; } return *sv==*sc; }
int wifi_saved_find(const char* ssid){ if(!wm_loaded)wifi_saved_load(); for(int i=0;i<g_wnet_n;i++) if(wm_eq(g_wnets[i].ssid,ssid))return i; for(int i=0;i<g_wnet_n;i++) if(wm_eq_old(g_wnets[i].ssid,ssid))return i; return -1; }
int wifi_saved_add(const char* ssid,const char* pass,const char* user,int eap,int hidden){
    int k=wifi_saved_find(ssid); if(k<0){ if(g_wnet_n>=WIFI_MAXNET){ int lo=0; for(int i=1;i<g_wnet_n;i++) if(g_wnets[i].prio<g_wnets[lo].prio)lo=i; k=lo; } else k=g_wnet_n++; for(int z=0;z<(int)sizeof(wnet);z++)((u8*)&g_wnets[k])[z]=0; g_wnets[k].autoc=1; g_wnets[k].prio=10; wm_failt[k]=0; wm_pwbad[k]=0; }
    wnet* w=&g_wnets[k]; wm_cpy(w->ssid,ssid,33); wm_cpy(w->pass,pass?pass:"",65); wm_cpy(w->user,user?user:"",65); w->eap=eap; if(hidden)w->hidden=1; wifi_saved_save(); return k; }
int wifi_saved_forget(const char* ssid){ int k=wifi_saved_find(ssid); if(k<0)return -1; for(int i=k;i<g_wnet_n-1;i++){ g_wnets[i]=g_wnets[i+1]; wm_failt[i]=wm_failt[i+1]; wm_pwbad[i]=wm_pwbad[i+1]; } g_wnet_n--; wm_failt[g_wnet_n]=0; wm_pwbad[g_wnet_n]=0; for(int z=0;z<(int)sizeof(wnet);z++)((u8*)&g_wnets[g_wnet_n])[z]=0; wifi_saved_save(); if(g_wm_cur==k)g_wm_cur=-1; else if(g_wm_cur>k)g_wm_cur--; return 0; }
void wifi_saved_prio(const char* ssid,int delta){ int k=wifi_saved_find(ssid); if(k<0)return; int p=g_wnets[k].prio+delta; g_wnets[k].prio=p<0?0:p>99?99:p; wifi_saved_save(); }

static void wm_coex(u32 m){
    int ok=rtw_coex_wifi_only(m);
    char b[160],*p=b; p=scopy(p,"wifi: antenna set for Wi-Fi only, RFE type "); p=udec(p,(u32)rtw_rfe_type()); p=scopy(p,ok?", coex 0x38=0x":", coex register did not answer, 0x38=0x"); p=hx(p,rtw_lte_read(m,0x38),8);
    p=scopy(p,", 0x4C=0x"); p=hx(p,rtw_r32(m,0x004C),8); p=scopy(p,", 0xCB4=0x"); p=hx(p,rtw_r32(m,0x0CB4),8); *p=0; klog(b); }
int nic_index_wifi(void);
int g_wpci_snap=0, g_wpci_mps=0; u32 g_wpci_st0=0, g_wpci_bst0=0, g_wpci_bsec0=0, g_wpci_bcmd0=0;
int g_wpci_bus=-1, g_wpci_slot=-1, g_wpci_br=0, g_wpci_bb=-1, g_wpci_bs=-1, g_wpci_bf=-1, g_wpci_aspm_dev=-2, g_wpci_aspm_br=-2, g_wpci_mdio=-2, g_wpci_d0=0;
int wifi_pci_prepare(u32 m){
    int wi=nic_index_wifi(); if(wi<0)return -1;
    int bus=pci_bus[wi], sl=pci_slot[wi]; g_wpci_bus=bus; g_wpci_slot=sl;
    g_wpci_bb=-1; pci_parent_bridge(bus,&g_wpci_bb,&g_wpci_bs,&g_wpci_bf);
    if(!g_wpci_snap){ g_wpci_snap=1; g_wpci_st0=pci_cfg(bus,sl,0,0x04)>>16;
        if(g_wpci_bb>=0){ g_wpci_bst0=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x04)>>16; g_wpci_bsec0=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x1C)>>16; g_wpci_bcmd0=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x04)&0xFFFFu; } }
    int pm=pci_find_cap(bus,sl,0,0x01); g_wpci_d0=0;
    if(pm){ u32 cs=pci_cfg(bus,sl,0,pm+4); if(cs&3u){ u32 bars[6]; for(int k=0;k<6;k++)bars[k]=pci_cfg(bus,sl,0,0x10+k*4); u32 cmd0=pci_cfg(bus,sl,0,0x04)&0xFFFFu;
        pci_cfgw(bus,sl,0,pm+4,(cs&0xFFFF7FFCu)); rtw_delay_us(10000); g_wpci_d0=1;
        if(!(cs&(1u<<3))){ for(int k=0;k<6;k++)pci_cfgw(bus,sl,0,0x10+k*4,bars[k]); pci_cfgw(bus,sl,0,0x04,cmd0|0x06u); } } }
    u32 c=pci_cfg(bus,sl,0,0x04)&0xFFFFu; if((c&0x06u)!=0x06u)pci_cfgw(bus,sl,0,0x04,c|0x06u);
    g_wpci_br=pci_enable_upstream(bus);
    { int pe=pci_find_cap(bus,sl,0,0x10);
      if(pe){ u32 d2=pci_cfg(bus,sl,0,pe+0x28)&0xFFFFu; if(!(d2&0x10u))pci_cfgw(bus,sl,0,pe+0x28,d2|0x10u);
        int be=(g_wpci_bb>=0)?pci_find_cap(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x10):0;
        if(be){ u32 dc=pci_cfg(bus,sl,0,pe+0x08)&0xFFFFu, bc=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,be+0x08)&0xFFFFu; u32 dm=(dc>>5)&7u, bm=(bc>>5)&7u;
          if(dm>bm){ pci_cfgw(bus,sl,0,pe+0x08,(dc&~0xE0u)|(bm<<5)); g_wpci_mps=1; } } } }
    g_wpci_aspm_dev=pci_aspm_off(bus,sl,0);
    g_wpci_aspm_br=(g_wpci_bb>=0)?pci_aspm_off(g_wpci_bb,g_wpci_bs,g_wpci_bf):-1;
    pci_cfgw(bus,sl,0,0x04,(pci_cfg(bus,sl,0,0x04)&0xFFFFu)|0xF9000000u);
    if(g_wpci_bb>=0){ pci_cfgw(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x04,(pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x04)&0xFFFFu)|0xF9000000u); u32 r1c=pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x1C); pci_cfgw(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x1C,(r1c&0xFFFFu)|0xF9000000u); }
    vtd_init();
    g_wpci_mdio=!m?-1:g_rtw_chip->pcie_phy?g_rtw_chip->pcie_phy(m):1;
    char b[480],*p=b; p=scopy(p,"wifi: PCIe prepared - card "); p=udec(p,(u32)bus); *p++=':'; p=udec(p,(u32)sl);
    p=scopy(p,g_wpci_d0?" woken to D0":" in D0"); p=scopy(p,", command 0x"); p=hx(p,pci_cfg(bus,sl,0,0x04)&0xFFFFu,4);
    if(g_wpci_bb>=0){ p=scopy(p,", bridge "); p=udec(p,(u32)g_wpci_bb); *p++=':'; p=udec(p,(u32)g_wpci_bs); *p++='.'; p=udec(p,(u32)g_wpci_bf); p=scopy(p," command 0x"); p=hx(p,pci_cfg(g_wpci_bb,g_wpci_bs,g_wpci_bf,0x04)&0xFFFFu,4); }
    else p=scopy(p,", no bridge found");
    p=scopy(p,", bridges switched on "); p=udec(p,(u32)g_wpci_br);
    p=scopy(p,", link power saving "); p=scopy(p,(g_wpci_aspm_dev==1||g_wpci_aspm_br==1)?"was on, now off":"off");
    p=scopy(p,", PCIe PHY setting "); p=scopy(p,g_wpci_mdio==0?"written":g_wpci_mdio==1?"not needed for this chip":"NOT written"); if(g_wpci_mps)p=scopy(p,", payload size lowered to the bridge"); *p=0; klog(b);
    p=b; p=scopy(p,"wifi: PCIe before - card status 0x"); p=hx(p,g_wpci_st0,4); p=scopy(p,", bridge command 0x"); p=hx(p,g_wpci_bcmd0,4); p=scopy(p," status 0x"); p=hx(p,g_wpci_bst0,4); p=scopy(p," secondary status 0x"); p=hx(p,g_wpci_bsec0,4); *p=0; klog(b);
    return 0;
}
static int g_wm_fwok=0, g_wm_dead=0, g_wm_retries=0; static u64 g_wm_retry=0;
static void wm_init_step(int k,int aut){ static const char* const S[5]={"wifi init","wifi fw","wifi macinit","wifi allbuilt","wifi rings"};
    char c[24]; wm_cpy(c,S[k],24); char st[64],*p=st; p=scopy(p,aut?"Starting Wi-Fi to reconnect (":"Starting Wi-Fi ("); p=udec(p,(u32)(k+1)); p=scopy(p," of 5)"); *p=0; wm_status(st,0); g_chquiet=1; run_cmd(c); g_chquiet=0; }
static int wm_gen_up(void){ if(g_wm_up)return 0; if(g_wm_dead)return -1;
    if(wifi_hw_init()<0){ char b[96],*p=b; p=scopy(p,"wifi: the "); p=scopy(p,wifi_hw_ops_get()->name); p=scopy(p," adapter did not start"); *p=0; klog(b); wm_status("Wi-Fi did not start",0); return -2; }
    g_wm_up=1; if(g_wm_state==WM_OFF)g_wm_state=WM_IDLE; wm_status("Wi-Fi is on",0); klog("wifi: radio started"); return 0; }
static int wm_gen_join(const scan_ap* ap,const char* ssid,const char* user,const char* pass){
    if(ap->sec!=0)return wifi_gen_join_secure(ap->bssid,ap->ch,ssid,ap->sec,ap->grp,ap->akm,ap->pmf,ap->ht,user,pass?pass:"");
    int r=wifi_hw_join(ap->bssid,ap->ch,ssid,ap->akm,ap->ht); if(r)return r<0?3:r;
    u8 mac[6]; if(wifi_hw_mac(mac)<0)return 3; wnd_setup_open(0,ap->bssid,mac,0); g_wpa_ok=1; return 0; }
int wifi_up(void){
    if(g_wm_up&&g_rxnum&&g_wm_fwok)return 0;
    u32 m=rtw_find_mmio(); if(!m){ if(wifi_hw_generic())return wm_gen_up(); wm_status("No Wi-Fi adapter",0); return -1; }
    if(g_wm_dead||rtw_r32(m,0x0000)==0xFFFFFFFFu){ g_wm_dead=1; wm_status("Wi-Fi card not responding - restart the computer",0); return -1; }
    if(g_wm_up&&g_rxnum&&!g_wm_fwok){ if(g_ticks-g_wm_retry<6000||g_wm_retries>=3||g_wm_state==WM_CONNECTED)return 0; g_wm_retries++; g_wm_retry=g_ticks; g_rxnum=0; klog("wifi: firmware was not running - starting the card again"); }
    if(!g_rxnum){ wm_bootk=0; for(int k=0;k<5;k++){ ui_pump(); wm_init_step(k,0); } }
    if(!g_rxnum){ wm_status("Wi-Fi did not start - open the Terminal and run: wifi init",0); return -2; }
    { extern int g_dmem_ck,g_imem_ck,g_dmem_sf,g_dmem_if,g_imem_sf,g_imem_if; char b[300],*p=b; p=scopy(p,"wifi: firmware parts: data memory checksum "); p=scopy(p,g_dmem_ck?"ok":"BAD"); if(g_dmem_sf)p=scopy(p," (page not staged)"); if(g_dmem_if)p=scopy(p," (copy timed out)");
      p=scopy(p,", code memory checksum "); p=scopy(p,g_imem_ck?"ok":"BAD"); if(g_imem_sf)p=scopy(p," (page not staged)"); if(g_imem_if)p=scopy(p," (copy timed out)"); *p=0; klog(b); }
    { u16 fc=rtw_r16(m,0x0080); u32 cr=rtw_r32(m,0x0100); char b[96],*p=b; p=scopy(p,"wifi: started, firmware control 0x"); p=hx(p,fc,4); p=scopy(p,((fc&(3u<<14))==(3u<<14))?" (firmware running)":" (firmware NOT running)"); p=scopy(p,", CR 0x"); p=hx(p,cr,8); *p=0; klog(b);
      g_wm_fwok=((fc&(3u<<14))==(3u<<14)); g_wm_retry=g_ticks;
      if((fc&(3u<<14))!=(3u<<14))klog("wifi: firmware did not report ready - scanning may still work, connecting will not"); }
    wm_coex(m);
    rtw_rx_ring_up(m);
    rtw_chip_calibrate(m);
    g_wm_up=1; if(g_wm_state==WM_OFF)g_wm_state=WM_IDLE; wm_status("Wi-Fi is on",0); klog("wifi: radio started"); return 0; }

static void wm_deauth(u32 m);
void wifi_radio_off(void){
    if(g_wm_lps)wm_lps(0); wm_pending=0; fr_arm=0; fr_go=0; if(sc_on)scan_finish();
    u32 m=rtw_find_mmio(); if(m&&g_wm_state==WM_CONNECTED){ wm_deauth(m); rtw_ra_disconnect(m); } else if(!m&&g_wm_state==WM_CONNECTED)wifi_hw_leave();
    if(wnd_is_up())wnd_down(); if(g_netdev_wifi)netdev_set(NETDEV_ETH);
    if(m&&g_rxnum){ rtw_w16(m,0x0522,0xFFFF); u32 cr=rtw_r32(m,0x0100); cr&=~((1u<<2)|(1u<<3)); rtw_w32(m,0x0100,cr); }
    g_wm_state=WM_OFF; g_wifi_conn[0]=0; wm_status("Wi-Fi is off",0); }
void wifi_user_power(int on){ if(!wm_loaded)wifi_saved_load(); wm_bootk=0; if(on){ wm_fails=0; wm_backoff=500; wm_next_try=g_ticks; } if(wm_useroff!=!on){ wm_useroff=!on; wifi_saved_save(); } }
void wifi_radio_on(void){ u32 m=rtw_find_mmio(); if(m&&g_rxnum){ rtw_w16(m,0x0522,0); u32 cr=rtw_r32(m,0x0100); cr|=0x0Fu; rtw_w32(m,0x0100,cr); } if(g_wm_state==WM_OFF)g_wm_state=WM_IDLE; wm_next_try=g_ticks; }

static const u8 CH2[13]={1,6,11,2,3,4,5,7,8,9,10,12,13};
static const u8 CH5[24]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,149,153,157,161,165};
static int sc_slice=0, sc_ovn=0; static const u8* sc_ov=0;
static int sc_on=0, sc_i=0, sc_n=0, sc_reps=0, sc_filt=0; static u64 sc_t=0, sc_start=0; static u8 sc_list[40]; static u32 sc_rx0, sc_bcn0, sc_crc0;
int wifi_scan_busy(void){ return sc_on; }
/* v10.99: like rtw88 (configure_filter without FIF_BCN_PRBRESP_PROMISC) the card keeps only our network's beacons
   (RCR bit 7 CBSSID_BCN) while connected and not scanning, so the CPU does not parse every neighbour's beacons. */
static void wm_bcn_filter(int on){ u32 m=rtw_find_mmio(); if(!m)return; u32 r=rtw_r32(m,0x0608); if(r==0xFFFFFFFFu)return; u32 n=on?(r|(1u<<7)):(r&~(1u<<7)); if(n!=r)rtw_w32(m,0x0608,n); }
int wifi_scan_start(int full){
    if(sc_on)return 0; if(g_airplane){ wm_status("Airplane mode is on",0); return -1; }
    if(g_wm_state==WM_OFF&&g_wm_up){ if(!full)return -1; wifi_user_power(1); wifi_radio_on(); }
    else if(g_wm_state==WM_OFF&&full)wifi_user_power(1);
    if(wifi_up()<0)return -1;
    u32 m=rtw_find_mmio(); sc_n=0; for(int i=0;i<(full?13:3);i++)sc_list[sc_n++]=CH2[i]; if(g_wm_5g)for(int i=0;i<24;i++)sc_list[sc_n++]=CH5[i];
    { int n0=sc_n, n1=regdb_filter(sc_list,sc_n); if(n1>0)sc_n=n1; else { sc_n=0; for(int i=0;i<(full?13:3);i++)sc_list[sc_n++]=CH2[i]; if(g_wm_5g)for(int i=0;i<24;i++)sc_list[sc_n++]=CH5[i]; } sc_filt=n0-sc_n; }
    if(sc_ovn){ sc_n=0; sc_filt=0; for(int i=0;i<sc_ovn&&i<40;i++) if(fr_on||regdb_ch_allowed(sc_ov[i]))sc_list[sc_n++]=sc_ov[i]; if(!sc_n)return -1; }
    { extern u32 g_rtw_rx_seen, g_rtw_rx_crc, g_rtw_rx_bcn; sc_rx0=g_rtw_rx_seen; sc_bcn0=g_rtw_rx_bcn; sc_crc0=g_rtw_rx_crc; }
    if(!m){ extern u8 g_scan_ch; g_scan_ch=0; sc_on=1; sc_i=0; sc_reps=0; sc_start=g_ticks; g_wifi_scanning=1; if(wifi_hw_scan_start(sc_list,sc_n)<0){ sc_on=0; g_wifi_scanning=0; wm_status("Wi-Fi scan did not start",0); return -1; } wm_status("Looking for networks...",0); return 0; }
    if(!g_rtw_coex_done)wm_coex(m); if(g_wm_lps)wm_lps(0); if(g_wm_state==WM_CONNECTED&&m){ wnd_tx_null(1); rtw_tx_mgmt_drain(m); } wm_bcn_filter(0); sc_on=1; sc_i=0; sc_reps=0; sc_start=g_ticks; g_wifi_scanning=1; g_chquiet=1; if(g_wm_state!=WM_CONNECTED)rtw_rx_ring_up(m); rtw_set_channel_2g(m,sc_list[0]); sc_t=g_ticks; wm_status("Looking for networks...",0); return 0; }
int wifi_scan_slice(const u8* ch,int n){ if(sc_on||g_wm_state!=WM_CONNECTED||n<=0)return -1; char st[96]; for(int i=0;i<96;i++)st[i]=g_wifi_status[i];
    sc_ov=ch; sc_ovn=n; sc_slice=1; int r=wifi_scan_start(0); sc_ovn=0; if(r<0)sc_slice=0; for(int i=0;i<96;i++)g_wifi_status[i]=st[i]; return r; }
static u32 g_txw_lf=0, g_txw_lt=0; static u64 g_txw_st=0;
static void scan_finish(void){
    u32 m=rtw_find_mmio(); sc_on=0; g_wifi_scanning=0; { extern u8 g_scan_ch; g_scan_ch=0; } if(g_wm_state==WM_CONNECTED){ extern u64 g_wnd_beacon_t; g_wnd_beacon_t=g_ticks; extern u32 g_wnd_txfull; g_txw_lf=g_wnd_txfull; g_txw_st=g_ticks; }
    for(int i=0;i<g_apn;){ int keep=sc_slice||fr_on||g_aps[i].last>=sc_start||(g_wm_state==WM_CONNECTED&&g_aps[i].ch==g_wm_ch); if(keep)i++; else g_aps[i]=g_aps[--g_apn]; }
    regdb_scan_result(sc_filt,g_apn);
    for(int i=0;i<g_apn;i++)for(int j=i+1;j<g_apn;j++) if(g_aps[j].rssi>g_aps[i].rssi){ scan_ap t=g_aps[i]; g_aps[i]=g_aps[j]; g_aps[j]=t; }
    if(g_wm_state==WM_CONNECTED&&m){ rtw_set_channel_2g(m,g_wm_ch); wm_bcn_filter(1); wnd_tx_null(0); }
    sc_slice=0;
    g_chquiet=0;
    extern u32 g_rtw_rx_seen, g_rtw_rx_crc, g_rtw_rx_bcn;
    char b[96],*p=b; p=scopy(p,"Found "); p=udec(p,(u32)g_apn); p=scopy(p,g_apn==1?" network":" networks");
    if(!g_apn){ p=scopy(p," - radio received "); p=udec(p,g_rtw_rx_seen-sc_rx0); p=scopy(p," frames"); if(g_rtw_rx_seen==sc_rx0)p=scopy(p," (receiver not working)"); }
    *p=0; if(g_wm_state!=WM_CONNECTED)wm_status(b,0);
    { char k[160],*q=k; q=scopy(q,"wifi: scan done, "); q=udec(q,(u32)g_apn); q=scopy(q," networks, frames "); q=udec(q,g_rtw_rx_seen-sc_rx0); q=scopy(q,", beacons "); q=udec(q,g_rtw_rx_bcn-sc_bcn0); q=scopy(q,", CRC errors "); q=udec(q,g_rtw_rx_crc-sc_crc0);
      if(m){ q=scopy(q,", CR 0x"); q=hx(q,rtw_r32(m,0x0100),8); q=scopy(q,", RCR 0x"); q=hx(q,rtw_r32(m,0x0608),8); q=scopy(q,", RX ptr 0x"); q=hx(q,rtw_r32(m,0x03B4),8); } *q=0; klog(k); }
    if(fr_on){ fr_on=0; fr_go=1; } }
static int wm_fr_seen(void){ for(int i=0;i<g_apn;i++){ if(g_aps[i].last<sc_start)continue; int eq=1; for(int b=0;b<6;b++) if(g_aps[i].bssid[b]!=fr_ap.bssid[b])eq=0; if(eq)return i; } return -1; }
static void wm_fr_probe(u32 m){ u8 f[80]; int n=0; f[n++]=0x40; f[n++]=0; f[n++]=0; f[n++]=0; for(int i=0;i<6;i++)f[n++]=fr_ap.bssid[i]; for(int i=0;i<6;i++)f[n++]=rtw_r8(m,0x0610+i); for(int i=0;i<6;i++)f[n++]=fr_ap.bssid[i]; f[n++]=0; f[n++]=0;
    int sl=0; while(sl<32&&fr_ap.ssid[sl])sl++; f[n++]=0; f[n++]=(u8)sl; for(int i=0;i<sl;i++)f[n++]=(u8)fr_ap.ssid[i];
    static const u8 R2[8]={0x82,0x84,0x8B,0x96,0x0C,0x12,0x18,0x24}, R5[8]={0x8C,0x12,0x98,0x24,0xB0,0x48,0x60,0x6C}; f[n++]=1; f[n++]=8; for(int i=0;i<8;i++)f[n++]=fr_ap.ch>14?R5[i]:R2[i]; rtw_tx_mgmt(m,f,n); }
static int wm_card_dead(u32 m){ if(!m||rtw_r32(m,0x0000)!=0xFFFFFFFFu)return 0; g_wm_dead=1; wm_next_try=g_ticks+0x7FFFFFFFULL; static int said=0; if(!said){ said=1; klog("wifi: the card does not answer any more (registers read 0xFFFFFFFF)"); } g_wm_up=0; g_rxnum=0; g_wm_state=WM_OFF; wm_status("Wi-Fi card not responding - restart the computer",0); return 1; }
static void scan_step(void){
    u32 m=rtw_find_mmio(); if(!m){ if(wifi_hw_generic()){ wifi_hw_poll(); if(sc_on&&(wifi_hw_scan_done()||g_ticks-sc_start>800||(fr_on&&wm_fr_seen()>=0)))scan_finish(); return; } sc_on=0; g_wifi_scanning=0; return; }
    if(wm_card_dead(m)){ sc_on=0; g_wifi_scanning=0; g_chquiet=0; return; }
    if(g_wm_state==WM_CONNECTED)net_idle_poll();
    extern u8 g_scan_ch; u8 cur=g_ticks-sc_t<2?0:sc_list[sc_i]; g_scan_ch=cur;
    if(fr_on){ u64 d=g_ticks-sc_t; if((d>=2&&fr_np==0)||(d>=8&&fr_np==1)){ fr_np++; wm_fr_probe(m); } }
    rtw_scan_drain(m,cur); if(fr_on&&cur&&wm_fr_seen()>=0){ scan_finish(); return; }
    if(g_ticks-sc_t<15)return;
    sc_i++; if(sc_i>=sc_n){ scan_finish(); return; }
    rtw_set_channel_2g(m,sc_list[sc_i]); sc_t=g_ticks; }
void wifi_scan_blocking(int full){ if(wifi_scan_start(full)<0)return; while(sc_on){ scan_step(); ui_pump(); } }

/* v10.99: the address is fetched in the background (dhcp_async_step, one step per tick) so the screen and touchpad never wait */
static int g_wm_dsafe=0; static u64 g_wm_dgap=3000, g_wm_dlast=0;
#define WM_PORTAL_OWNER 0x57504F52L
static int wm_portal_id=0; static u64 wm_portal_t=0;
static void wm_portal_stop(void){ if(wm_portal_id)nwk_net_abort(WM_PORTAL_OWNER,wm_portal_id); nwk_net_cancel_owner(WM_PORTAL_OWNER); wm_portal_id=0; }
static void wm_portal_start(void){ wm_portal_stop(); char u[96],*p=u; p=scopy(p,"http://connectivitycheck.gstatic.com/generate_204?n="); p=udec(p,(u32)g_ticks); *p=0;
    wm_portal_id=nwk_net_start(WM_PORTAL_OWNER,u,"GET",0,0,0,"",0); wm_portal_t=g_ticks; if(wm_portal_id<=0)wm_portal_id=0; }
static void wm_portal_tick(void){ if(!wm_portal_id)return; nwk_netev ev;
    while(nwk_net_event(WM_PORTAL_OWNER,&ev)){ if(ev.id!=wm_portal_id)continue;
        if(ev.type==1){ int code=ev.status; if(code!=204&&code>=200&&code<400){ g_wm_portal=1; notify_from("Wi-Fi","Sign in to the network","Open the browser to finish signing in"); wm_status("Sign-in needed: ",g_wifi_conn); } }
        if(ev.type==3){ wm_portal_id=0; return; } }
    if(g_ticks-wm_portal_t>3000)wm_portal_stop(); }
static void wm_dhcp_tick(void){ extern int dhcp_async_step(void); extern void dhcp_async_start(void); int r=dhcp_async_step();
    if(r>0){ wm_status("Connected to ",g_wifi_conn); rtw_jlog("address: got one (DHCP)","",-1); if(g_wm_dsafe)klog("wifi: fast rates did not work here - using safe rates");
        wm_portal_start();
        return; }
    if(r<0){ if(g_wm_ps&&!g_wps_off){ g_wps_off=1; g_wm_ps=0; if(g_wm_lps)wm_lps(0); klog("wifi: power saving off for this session (no address)"); }
        if(g_wifi_ra&&!g_wm_dsafe){ g_wm_dsafe=1; g_wifi_ra=0; { u32 mm=rtw_find_mmio(); if(mm)rtw_ra_connect(mm,0,0,0); } dhcp_async_start(); return; }
        if(g_wm_dsafe){ g_wifi_ra=1; g_wm_dsafe=0; } wm_status("Connected, but no address from ",g_wifi_conn); rtw_jlog("address: none (the router did not answer DHCP)","",-1); g_wm_dlast=g_ticks; } }
static const char* wm_err(int r){ switch(r){ case 1:return "the network did not answer"; case 2:return "the network refused the connection"; case 3:return "no answer when joining"; case 4:return "the network rejected this device (security type)";
    case 5:return "the network did not start the secure handshake"; case 6:return "wrong password"; case 7:return "sign-in failed (user name or password)"; case 8:return "the network needs a certificate we do not support"; case 10:return "the network certificate changed (forget the network to trust it again)"; case 9:return "the network uses old TKIP or WPA security (not supported - switch the router to WPA2-AES)"; case 11:return "the network did not send a usable group key (try again, or restart the router)"; case 12:return "the handshake did not match the network's beacon (possible downgrade attack) - not connected"; default:return "the handshake failed"; } }

static void wm_deauth(u32 m){ if(!m||g_wm_state!=WM_CONNECTED)return; { extern void wnd_delba_all(void); wnd_delba_all(); } u8 f[26]; int n=0; f[n++]=0xC0; f[n++]=0; f[n++]=0; f[n++]=0; for(int i=0;i<6;i++)f[n++]=g_wm_bssid[i]; for(int i=0;i<6;i++)f[n++]=rtw_r8(m,0x0610+i); for(int i=0;i<6;i++)f[n++]=g_wm_bssid[i]; f[n++]=0; f[n++]=0; f[n++]=3; f[n++]=0; rtw_tx_mgmt(m,f,n); rtw_tx_mgmt_drain(m); }
static int wm_connect(const scan_ap* apin,const char* pass,const char* user,int save);
int wifi_connect_ap(int idx,const char* pass,const char* user,int save){ if(idx<0||idx>=g_apn)return -1; scan_ap ap=g_aps[idx]; return wm_connect(&ap,pass,user,save); }
static int wm_connect(const scan_ap* apin,const char* pass,const char* user,int save){
    if(g_airplane||(g_wm_up&&g_wm_state==WM_OFF)){ wm_status(g_airplane?"Airplane mode is on":"Wi-Fi is off",0); return -1; }
    if(!wm_autoj&&g_wm_state==WM_OFF)wifi_user_power(1);
    u32 m=rtw_find_mmio(); int gen=!m&&wifi_hw_generic(); if((!m&&!gen)||wifi_up()<0)return -1;
    if(sc_on)scan_finish(); wm_noauto[0]=0; if(g_wm_lps)wm_lps(0); wm_pending=0; fr_arm=0; fr_go=0;
    scan_ap ap=*apin; char ssid[33]; wm_cpy(ssid,ap.ssid,33);
    if(g_wm_state==WM_CONNECTED){ wm_deauth(m); if(m)rtw_ra_disconnect(m); else wifi_hw_leave(); wnd_down(); }
    if(g_netdev_wifi)netdev_set(NETDEV_ETH);
    g_wm_state=WM_CONNECTING; if(wm_autoj){ char st[64],*q=st; q=scopy(q,"Connecting to "); for(int i=0;ssid[i]&&i<32;i++)*q++=ssid[i]; q=scopy(q," automatically"); *q=0; wm_status(st,0); klog(st); } else wm_status("Connecting to ",ssid); ui_pump();
    g_chquiet=1; int r;
    rtw_jlog_reset(); rtw_jlog("join ",ssid,-1); rtw_jlog("  channel","",(int)ap.ch); rtw_jlog("  management frames via the ",g_mg_via_be?"data queue":"mgmt queue (checked each send)",-1);
    if(gen){ if(ap.akm&AKM_8021X&&!(ap.akm&(AKM_PSK|AKM_SAE))){ int sv=wifi_saved_find(ssid); g_eap_pin_set=0; if(sv>=0&&g_wnets[sv].pinset){ for(int z=0;z<32;z++)g_eap_pin[z]=g_wnets[sv].pin[z]; g_eap_pin_set=1; } } r=wm_gen_join(&ap,ssid,user,pass); }
    else if(ap.akm&AKM_8021X&&!(ap.akm&(AKM_PSK|AKM_SAE))){ int sv=wifi_saved_find(ssid); g_eap_pin_set=0; if(sv>=0&&g_wnets[sv].pinset){ for(int z=0;z<32;z++)g_eap_pin[z]=g_wnets[sv].pin[z]; g_eap_pin_set=1; } r=wifi_eap_join(m,&ap,ssid,user?user:"",pass?pass:""); }
    else if(ap.sec==0){ r=rtw_assoc(m,&ap,ssid); if(r==0){ u8 mac[6]; for(int i=0;i<6;i++)mac[i]=rtw_r8(m,0x0610+i); wnd_setup_open(m,ap.bssid,mac,g_wifi_assoc_qos); g_wpa_ok=1; } }
    else r=rtw_wpa2_join(m,&ap,ssid,pass?pass:"");
    g_chquiet=0;
    rtw_jlog("result: ",r?wm_err(r):"joined",r?r:-1);
    if(m){ extern void rtw_txpwr_join(int r); rtw_txpwr_join(r); }
    if(r){ g_wm_state=WM_IDLE; { extern const char* g_eap_why; wm_status("Could not connect: ",(ap.akm&AKM_8021X)&&!(ap.akm&(AKM_PSK|AKM_SAE))&&g_eap_why&&g_eap_why[0]?g_eap_why:wm_err(r)); } g_wm_reason=r; return r; }
    if(m){ u8 rates[12]={2,4,11,22,12,18,24,36,48,72,96,108}; rtw_ra_connect(m,g_wifi_assoc_ht?ap.ht:0,ap.ch>14?rates+4:rates,ap.ch>14?8:12); }
    for(int i=0;i<6;i++)g_wm_bssid[i]=ap.bssid[i]; g_wm_ch=ap.ch; fr_ap=ap; wm_cpy(fr_ap.ssid,ssid,33); fr_ok=1;
    netdev_set(NETDEV_WIFI); wm_status("Getting an address from ",ssid); ui_pump();
    { extern void dhcp_async_start(void); g_dhcp_done=0; dhcp_async_start(); g_wm_dsafe=0; g_wm_dgap=3000; g_wm_dlast=g_ticks; }
    rtw_jlog("address: asking the router (DHCP, in the background)","",-1);
    g_wm_state=WM_CONNECTED; g_wm_since=g_ticks; wm_last_ok=g_ticks; wm_bcn_filter(1); if(!wm_autoj){ wm_fails=0; wm_backoff=500; } g_wm_reason=0;
    wm_cpy(g_wifi_conn,ssid,33);
    { int eapn=(ap.akm&AKM_8021X)&&!(ap.akm&(AKM_PSK|AKM_SAE));
      if(save){ int k=wifi_saved_add(ssid,pass,user,eapn,ap.hidden); g_wm_cur=k; } else g_wm_cur=wifi_saved_find(ssid);
      if(g_wm_cur>=0){ int ch=0; wm_failt[g_wm_cur]=0; wm_pwbad[g_wm_cur]=0; if(!wm_autoj&&g_wnets[g_wm_cur].autoc==2){ g_wnets[g_wm_cur].autoc=1; ch=1; } if(!wm_eq(wm_last,g_wnets[g_wm_cur].ssid)){ wm_cpy(wm_last,g_wnets[g_wm_cur].ssid,33); ch=1; } if(ch)wifi_saved_save(); }
      if(eapn&&g_wm_cur>=0&&!g_wnets[g_wm_cur].pinset){ for(int z=0;z<32;z++)g_wnets[g_wm_cur].pin[z]=g_eap_pin_new[z]; g_wnets[g_wm_cur].pinset=1; wifi_saved_save(); { char fp[96],*q=fp; q=scopy(q,"wifi: trusting the network certificate SHA-256 "); for(int z=0;z<8;z++)q=hx(q,g_eap_pin_new[z],2); q=scopy(q,"..."); *q=0; klog(fp); } } }
    notify_from("Wi-Fi","Wi-Fi",g_wifi_conn);
    g_wm_portal=0;
    klog("wifi: associated, getting an address in the background"); return 0; }

int wifi_roam_join(const scan_ap* ap){ int k=wifi_saved_find(ap->ssid); if(k<0||ap->hidden||g_wm_state!=WM_CONNECTED)return -1; wm_autoj=1; int r=wm_connect(ap,g_wnets[k].pass,g_wnets[k].user,0); wm_autoj=0; return r; }

int wifi_connect_ssid(const char* ssid,const char* pass,const char* user,int hidden){
    int k=-1; for(int i=0;i<g_apn;i++) if(wm_eq(g_aps[i].ssid,ssid)){ if(k<0||g_aps[i].rssi>g_aps[k].rssi)k=i; }
    if(k<0){ wifi_scan_blocking(1); for(int i=0;i<g_apn;i++) if(wm_eq(g_aps[i].ssid,ssid)){ if(k<0||g_aps[i].rssi>g_aps[k].rssi)k=i; } }
    if(k<0&&hidden){ for(int i=0;i<g_apn;i++) if(g_aps[i].hidden&&!g_aps[i].ssid[0]){ scan_ap t=g_aps[i]; wm_cpy(t.ssid,ssid,33); int hr=wm_connect(&t,pass,user,1); if(hr>0)return hr; if(hr==0){ for(int z=0;z<g_apn;z++) if(!g_aps[z].ssid[0]&&g_aps[z].hidden){ int eq=1; for(int b=0;b<6;b++) if(g_aps[z].bssid[b]!=t.bssid[b])eq=0; if(eq)wm_cpy(g_aps[z].ssid,ssid,33); } return 0; } } }
    if(k<0){ wm_status("Network not found: ",ssid); return -1; }
    return wifi_connect_ap(k,pass,user,1); }

void wifi_disconnect(void){ { extern void dhcp_async_stop(void); dhcp_async_stop(); } wm_portal_stop(); wm_bcn_filter(0); if(g_wm_lps)wm_lps(0); wm_pending=0; fr_arm=0; fr_go=0; wm_cpy(wm_noauto,g_wifi_conn,33); u32 m=rtw_find_mmio(); wm_deauth(m); if(m&&g_wm_state==WM_CONNECTED)rtw_ra_disconnect(m); else if(!m&&g_wm_state==WM_CONNECTED)wifi_hw_leave(); if(wnd_is_up())wnd_down(); if(g_netdev_wifi)netdev_set(NETDEV_ETH); g_wm_state=g_wm_up?WM_IDLE:WM_OFF; g_wifi_conn[0]=0; g_wm_cur=-1; wm_next_try=g_ticks+6000; for(int z=0;z<32;z++)g_pmk[z]=0; for(int z=0;z<48;z++)g_ptk[z]=0; wm_status("Disconnected",0); }

static u64 wm_lps_off_t=0;
static int wm_lps(int on){ u32 m=rtw_find_mmio(); if(!m)return -1; extern int rtw_h2c_cmd(u32 m,u32 w0,u32 w1); if(!on&&g_wm_lps)wm_lps_off_t=g_ticks;
    if(on){ g_wps_aid=rtw_r16(m,0x06A8)&0x7FF; g_wps_tim=0; wnd_tx_null(1); rtw_delay_us(1000); }
    u32 w0=on?(0x20u|(1u<<8)|(1u<<16)|(2u<<20)|(1u<<24)):(0x20u|(1u<<24)); u32 w1=on?0x00u:(0x0Cu<<8);
    int r=rtw_h2c_cmd(m,w0,w1); if(r==0||!on)g_wm_lps=on&&r==0; if(!on){ for(int t=0;t<5&&(rtw_r8(m,0x0604)&0x80);t++)rtw_delay_us(20000); rtw_w8(m,0x0604,rtw_r8(m,0x0604)&~0x80); wnd_tx_null(0); } else if(r==0)g_wps_enter++; return r; }
static void wm_pre_tx(void){ if(g_wm_lps){ wm_lps(0); rtw_delay_us(2000); } }
int wifi_powersave_mode(int mode){ g_wps_mode=mode; if(mode==WPS_ON){ g_wps_off=0; g_wps_fail=0; } g_wm_ps=wps_allowed(bat_present,bat_charging); if(!g_wm_ps&&g_wm_lps)wm_lps(0); return 0; }
int wifi_powersave(int on){ return wifi_powersave_mode(on?WPS_ON:WPS_OFF); }
static int wm_ps_busy(u64 lrx){ extern int dhcp_async_running(void); extern u64 g_wnd_eapol_t; return sc_on||dhcp_async_running()||g_ticks-g_wnd_eapol_t<300||g_ticks-lrx<100; }
static void wm_ps_tick(void){ static u32 ltx=0, lrxn=0; static u64 lt=0, wk=0, lrx=0; static int chk=0;
    if(g_wnd_rx_frames!=lrxn){ lrxn=g_wnd_rx_frames; lrx=g_ticks; }
    g_wm_ps=wps_allowed(bat_present,bat_charging); if((!g_wm_ps||wm_ps_busy(lrx))&&g_wm_lps){ wm_lps(0); lt=g_ticks; }
    if(chk&&g_wnd_beacon_t>wk){ chk=0; if(wps_miss())klog("wifi: power saving turned off for this session (beacons missed while the chip slept)"); else klog("wifi: beacons missed while the chip slept - woke up"); }
    if(chk&&g_ticks-wk>300)chk=0;
    if(g_wm_lps){ u64 bt=(u64)g_wnd_bcn_int*8/10; if(bt<150)bt=150;
        if(g_ticks-g_wnd_beacon_t>bt&&g_wnd_beacons>0){ wm_lps(0); wk=g_ticks; chk=1; lt=g_ticks; return; }
        if(g_wps_tim){ g_wps_tim=0; g_wps_wake_tim++; wm_lps(0); lt=g_ticks; return; } }
    if(g_wnd_tx_frames!=ltx){ ltx=g_wnd_tx_frames; lt=g_ticks; return; }
    if(g_wm_ps&&!g_wm_lps&&!wm_ps_busy(lrx)&&!chk&&g_ticks-lt>500&&g_ticks-g_wm_since>1000)wm_lps(1); }

static int wm_pick(void){
    int best=-1, bs=-100000;
    for(int i=0;i<g_apn;i++){ int k=wifi_saved_find(g_aps[i].ssid); if(k<0||g_wnets[k].autoc!=1)continue; if(wm_noauto[0]&&wm_eq(wm_noauto,g_aps[i].ssid))continue; if(wm_failt[k]&&g_ticks-wm_failt[k]<18000&&g_wnet_n>1)continue; int s=g_wnets[k].prio*1000+g_aps[i].rssi+(g_aps[i].ch>14&&g_aps[i].rssi>-70?15:0)+(wm_last[0]&&wm_eq(wm_last,g_wnets[k].ssid)?500:0); if(s>bs){ bs=s; best=i; } }
    return best; }

static u64 wm_fail_delay(void){ if(wm_fails>=WM_MAXFAIL)return 60000; int sh=wm_fails<1?0:wm_fails-1; if(sh>4)sh=4; u64 d=3000ULL<<sh; return d>30000?30000:d; }
static void wm_retry_note(void){ u64 d=(wm_next_try-g_ticks)/100; char st[96],*p=st; int L=0; while(g_wifi_status[L]&&L<60){ st[L]=g_wifi_status[L]; L++; } p=st+L; p=scopy(p," - next try in "); if(d>=120){ p=udec(p,(u32)(d/60)); p=scopy(p," min"); } else { p=udec(p,(u32)d); p=scopy(p," s"); } *p=0; wm_status(st,0);
    char k[96],*q=k; q=scopy(q,"wifi: automatic connection failed "); q=udec(q,(u32)wm_fails); q=scopy(q," time(s) in a row, next try in "); q=udec(q,(u32)d); q=scopy(q," s"); *q=0; klog(k); }
static void wm_lost(const char* why){ { extern void dhcp_async_stop(void); dhcp_async_stop(); } wm_portal_stop(); wm_bcn_filter(0);
    if(g_wm_lps||(wm_lps_off_t&&g_ticks-wm_lps_off_t<200)){ if(wps_miss())klog("wifi: power saving turned off for this session (link lost while saving power)"); }
    { extern void wnd_delba_tx(void); if(ampdu_tx_early(g_ticks)){ wnd_delba_tx(); ampdu_tx_bad(g_ticks); klog("wifi: link lost soon after aggregation started - counted as an aggregation failure"); } }
    if(g_wm_lps)wm_lps(0); g_wm_lps=0; u32 m=rtw_find_mmio(); if(m)rtw_ra_disconnect(m); else wifi_hw_leave(); if(wnd_is_up())wnd_down(); if(g_netdev_wifi)netdev_set(NETDEV_ETH);
    g_wm_state=WM_IDLE; wm_status("Connection lost (",why); { int L=0; while(g_wifi_status[L])L++; if(L<90){ g_wifi_status[L]=')'; g_wifi_status[L+1]=0; } }
    notify_from("Wi-Fi","Wi-Fi connection lost","Reconnecting..."); klog("wifi: connection lost - reconnecting"); wm_backoff=300;
    if(g_ticks-g_wm_since<WM_STABLE&&wm_fails){ wm_fails++; wm_next_try=g_ticks+wm_fail_delay(); wm_retry_note(); } else { if(g_ticks-g_wm_since<WM_STABLE)wm_fails=1; wm_next_try=g_ticks+100; }
    if(g_wm_fast&&!fr_dis&&fr_ok){ fr_arm=1; fr_t0=g_ticks; if(wm_next_try<=g_ticks+100)wm_next_try=g_ticks+10; } }
void wifi_fast_set(int on){ g_wm_fast=on; fr_dis=0; fr_fail=0; if(!on){ fr_arm=0; fr_go=0; } }
int wifi_fast_state(void){ return !g_wm_fast?0:fr_dis?2:1; }
static void wm_fr_bad(const char* why){ fr_fail++; char b[160],*p=b; p=scopy(p,"wifi: fast reconnect did not work ("); p=scopy(p,why); p=scopy(p,") - looking for the network with a full scan"); *p=0; klog(b);
    if(fr_fail>=3){ fr_dis=1; klog("wifi: fast reconnect failed 3 times in a row - turned off until restart (wifi fast on to try again)"); } wm_next_try=g_ticks; }
static int wm_fr_start(void){ fr_arm=0; if(!g_wm_fast||fr_dis||!fr_ok)return -1; int s=wifi_saved_find(fr_ap.ssid); if(s<0||g_wnets[s].autoc!=1||(wm_noauto[0]&&wm_eq(wm_noauto,fr_ap.ssid)))return -1;
    static u8 fch[1]; fch[0]=fr_ap.ch; sc_ov=fch; sc_ovn=1; fr_on=1; fr_np=0; int r=wifi_scan_start(0); sc_ovn=0; if(r<0){ fr_on=0; return -1; }
    char b[96],*p=b; p=scopy(p,"wifi: fast reconnect - asking "); p=scopy(p,fr_ap.ssid); p=scopy(p," on channel "); p=udec(p,fr_ap.ch); p=scopy(p," directly"); *p=0; klog(b); wm_status("Reconnecting to ",fr_ap.ssid); return 0; }
static void wm_fr_join(void){ int k=wm_fr_seen(), s=wifi_saved_find(fr_ap.ssid); if(s<0)return; if(k<0){ wm_fr_bad("no answer on the last channel"); return; }
    extern int g_wpa_pmk_hit; g_wpa_pmk_hit=0; scan_ap t=g_aps[k]; wm_cpy(t.ssid,fr_ap.ssid,33); wm_autoj=1; int r=wm_connect(&t,g_wnets[s].pass,g_wnets[s].user,0); wm_autoj=0;
    if(r){ wm_fr_bad(wm_err(r)); return; } fr_fail=0; u64 ms=(g_ticks-fr_t0)*10; char b[192],*p=b; p=scopy(p,"wifi: fast reconnect to "); p=scopy(p,fr_ap.ssid); p=scopy(p," on channel "); p=udec(p,fr_ap.ch); p=scopy(p," in "); p=udec(p,(u32)ms); p=scopy(p," ms (");
    p=scopy(p,g_wnd_open?"open network":g_wpa_pmk_hit?"saved key reused":"key computed"); p=scopy(p,ms<=1500?", within the 1.5 s target)":", slower than the 1.5 s target)"); *p=0; klog(b); }

void wifi_mgr_tick(void){
    static u64 last=0; { extern void wifi_stats_sample(void); wifi_stats_sample(); }
    { u32 cm=g_rxnum?rtw_find_mmio():0; rtw_coex_tick(cm,g_wm_state==WM_CONNECTED,g_wm_state==WM_CONNECTING,sc_on,g_wm_state==WM_CONNECTED?g_wm_ch:0); if(g_rtw_coex_nolps&&g_wm_lps)wm_lps(0); }
    if(!wm_loaded&&g_ticks>300)wifi_saved_load();
    if(g_airplane&&(sc_on||wm_pending)){ if(sc_on)scan_finish(); wm_pending=0; }
    if(sc_on){ scan_step(); return; }
    if(fr_go){ fr_go=0; if(g_wm_state==WM_IDLE&&!g_airplane&&!g_wifi_entry){ wm_fr_join(); return; } }
    { extern u64 g_wake_t; if(g_wake_t!=fr_wake){ fr_wake=g_wake_t; fr_wk=1; } if(fr_wk&&g_ticks-g_wake_t>=100){ fr_wk=0; if(g_wm_state==WM_CONNECTED&&g_wnd_beacons>0&&g_wnd_beacon_t<g_wake_t){ wm_lost("no signal after sleep"); return; } if(g_wm_state==WM_IDLE&&g_wm_fast&&!fr_dis&&fr_ok&&!wm_useroff){ fr_arm=1; fr_t0=g_wake_t; wm_next_try=g_ticks; } } }
    if(wm_pending&&g_wifi_entry){ wm_pending=0; wm_next_try=g_ticks+1000; }
    if(wm_pending&&g_wm_state==WM_IDLE&&g_ticks>=wm_next_try){ wm_pending=0; int k=wm_pick();
        int tried=0;
        if(k>=0){ int s=wifi_saved_find(g_aps[k].ssid); wm_autoj=1; int r=wifi_connect_ap(k,g_wnets[s].pass,g_wnets[s].user,0); wm_autoj=0; if(r==0)return; tried=1; wm_fails++; s=wifi_saved_find(g_aps[k].ssid); if(s>=0){ wm_failt[s]=g_ticks; if(r==6||r==7)wm_pwbad[s]++; else wm_pwbad[s]=0; if(r==10||((r==6||r==7)&&wm_pwbad[s]>=3)){ wm_pwbad[s]=0; if(g_wnets[s].autoc==1)g_wnets[s].autoc=2; wifi_saved_save(); notify_from("Wi-Fi","Wi-Fi password not accepted",g_wnets[s].ssid); } } }
        else for(int s=0;s<g_wnet_n;s++) if(g_wnets[s].hidden&&g_wnets[s].autoc==1){ for(int i=0;i<g_apn;i++) if(g_aps[i].hidden&&!g_aps[i].ssid[0]){ scan_ap t=g_aps[i]; wm_cpy(t.ssid,g_wnets[s].ssid,33); wm_autoj=1; int r=wm_connect(&t,g_wnets[s].pass,g_wnets[s].user,0); wm_autoj=0; if(r==0)return; tried=1; wm_fails++; goto wm_hid_done; } } wm_hid_done:;
        if(tried){ wm_next_try=g_ticks+wm_fail_delay(); wm_retry_note(); return; }
        wm_backoff=wm_backoff*2; if(wm_backoff>6000)wm_backoff=6000; wm_next_try=g_ticks+wm_backoff; return; }
    if(g_ticks-last<20)return; last=g_ticks;
    if(g_wm_up&&!g_wm_dead&&wm_card_dead(rtw_find_mmio()))return;
    { static int air=0, airon=0; if(g_airplane){ if(!air){ airon=g_wm_up&&g_wm_state!=WM_OFF; wm_bootk=0; } if(g_wm_state!=WM_OFF&&g_wm_up)wifi_radio_off(); air=1; return; } if(air){ air=0; if(g_wm_up&&airon&&!wm_useroff){ wifi_radio_on(); wm_next_try=g_ticks; } airon=0; } }
    if(g_wm_state==WM_CONNECTED){
        wm_portal_tick();
        { extern void (*g_wnd_pre_tx)(void); g_wnd_pre_tx=wm_pre_tx; }
        { extern int dhcp_async_running(void); if(dhcp_async_running())wm_dhcp_tick(); else net_idle_poll(); } wm_ps_tick(); { static u64 rr=0; if(g_ticks-rr>=200){ rr=g_ticks; extern void rtw_ra_rssi(u32,int); u32 mm=rtw_find_mmio(); if(mm&&g_wifi_rssi>-127&&!g_wm_lps)rtw_ra_rssi(mm,g_wifi_rssi); } }
        { extern void rtw_iqk_guard(u32,int,u32,u64); u32 mm=rtw_find_mmio(); if(mm)rtw_iqk_guard(mm,g_wm_lps?-1:g_wnd_beacons,g_wnd_bcn_int,g_ticks); }
        { static u64 pt=0; u32 mm=rtw_find_mmio(); if(mm&&!g_wm_lps&&!g_wifi_scanning&&g_rtw_chip&&g_rtw_chip->pwr_track&&g_ticks-pt>=2000){ pt=g_ticks; g_rtw_chip->pwr_track(mm); } }
        { extern u8 g_rtw_bw_drop; extern int rtw_bw_tick(u32,u64,u32); u32 mm=rtw_find_mmio(); if(mm&&g_rtw_bw_drop){ if(g_wm_lps)wm_lps(0); if(!g_wm_lps&&rtw_bw_tick(mm,g_ticks,g_wnd_rx_uc)<0){ wm_lost("switching to 20 MHz"); return; } } }
        if(wm_fails&&g_ticks-g_wm_since>=WM_STABLE){ wm_fails=0; wm_backoff=500; }
        { extern int g_wifi_ptk_rekey; if(g_wifi_ptk_rekey){ g_wifi_ptk_rekey=0; wm_lost("new keys"); return; } }
        if(!wnd_is_up()){ wm_lost(g_wnd_lost?"the network disconnected us":"link down"); return; }
        { extern u32 g_wnd_txfull, g_wnd_tx_frames; if(g_wnd_tx_frames!=g_txw_lt){ g_txw_lt=g_wnd_tx_frames; g_txw_lf=g_wnd_txfull; g_txw_st=g_ticks; } else if(g_wnd_txfull-g_txw_lf>=20&&g_ticks-g_txw_st>=500){ g_txw_lf=g_wnd_txfull; g_txw_st=g_ticks; klog("wifi: transmit queue stuck - reconnecting"); wm_lost("transmit stuck"); return; } }
        u64 lim=(u64)g_wnd_bcn_int*12/10; if(lim<500)lim=500; if(lim>1500)lim=1500;
        if(g_ticks-g_wnd_beacon_t>lim&&g_wnd_beacons>0){ wm_lost("no signal"); return; }
        { extern int dhcp_async_running(void); extern void dhcp_async_start(void); if(!g_dhcp_ok&&!dhcp_async_running()&&g_ticks-g_wm_dlast>g_wm_dgap){ g_wm_dlast=g_ticks; if(g_wm_dgap<48000)g_wm_dgap*=2; dhcp_async_start(); } }
        { extern void dhcp_lease_tick(void); dhcp_lease_tick(); }
        { extern void wifi_roam_tick(void); wifi_roam_tick(); }
        return; }
    if(g_wm_state==WM_OFF&&!g_wm_up){ if(wm_useroff||g_wm_dead||!g_wm_auto||!g_wnet_n||g_ticks<1500)return; if(g_ticks<wm_next_try)return; u32 m=rtw_find_mmio(); if(!m&&!wifi_hw_generic()){ wm_next_try=g_ticks+60000; return; } if(!m){ if(wifi_up()<0){ wm_next_try=g_ticks+30000; return; } wifi_radio_on(); goto wm_off_done; } if(wm_card_dead(m))return;
        if(!g_rxnum&&wm_bootk<5){ if(!wm_bootk)klog("wifi: starting the radio to reconnect to a saved network"); wm_init_step(wm_bootk++,1); return; }
        wm_bootk=0; if(!g_rxnum){ wm_status("Wi-Fi did not start - open the Terminal and run: wifi init",0); wm_next_try=g_ticks+30000; return; }
        if(wifi_up()<0){ wm_next_try=g_ticks+30000; return; } wifi_radio_on(); wm_off_done:; }
    if(g_wm_state==WM_IDLE&&g_wm_auto&&g_wnet_n&&g_ticks>=wm_next_try&&!g_wifi_entry){
        if(fr_arm&&wm_fr_start()==0)return;
        if(wifi_scan_start(1)==0){ wm_pending=1; int k=wm_last[0]?wifi_saved_find(wm_last):-1; if(k>=0&&g_wnets[k].autoc!=1)k=-1; if(k<0)for(int i=0;i<g_wnet_n;i++) if(g_wnets[i].autoc==1){ k=i; break; } if(k>=0){ char st[80],*p=st; p=scopy(p,"Looking for "); for(int i=0;g_wnets[k].ssid[i]&&i<32;i++)*p++=g_wnets[k].ssid[i]; p=scopy(p," to connect automatically"); *p=0; wm_status(st,0); } }
        else wm_next_try=g_ticks+3000; } }

const char* wifi_state_name(void){ switch(g_wm_state){ case WM_OFF:return "Off"; case WM_IDLE:return "Not connected"; case WM_CONNECTING:return "Connecting"; case WM_CONNECTED:return g_wm_portal?"Sign-in needed":"Connected"; default:return "?"; } }
int wifi_signal_bars(int rssi){ if(rssi<=-127)return 0; if(rssi>=-55)return 4; if(rssi>=-65)return 3; if(rssi>=-75)return 2; return 1; }
int wifi_rate_mbps(void){ if(g_wm_state!=WM_CONNECTED)return 0; if(!g_wifi_ra)return g_wifi_band5?6:1; { extern int rtw_ra_fw_mbps(void); int fr=rtw_ra_fw_mbps(); if(fr>0)return fr; } return g_wifi_assoc_ht?72:54; }

int wifi_info_text(char* o,int cap){ char* p=o; char* e=o+cap-200; extern u32 g_wnd_rx_frames, g_wnd_tx_frames, g_wnd_rx_drop; extern int g_wifi_rekeys; extern int g_wpa_akm;
    u32 m=rtw_find_mmio(); p=scopy(p,"Adapter="); if(!m)p=scopy(p,wifi_hw_generic()?wifi_hw_ops_get()->name:"none found"); else if(g_rtw_chip==rtw_chip_at(0))p=scopy(p,"Realtek RTL8821CE (802.11ac 1x1, 2.4 + 5 GHz)"); else { p=scopy(p,"Realtek "); p=scopy(p,g_rtw_chip->name); } *p++='\n';
    p=scopy(p,"Radio="); p=scopy(p,(g_rxnum||(!m&&g_wm_up))?(g_wm_state==WM_OFF?"off":"on"):"not started"); *p++='\n';
    p=scopy(p,"State="); p=scopy(p,wifi_state_name()); *p++='\n';
    if(rtw_efuse_valid){ p=scopy(p,"MAC="); for(int k=0;k<6;k++){ p=hx(p,g_rtw_efuse.mac[k],2); if(k<5)*p++=':'; } *p++='\n'; }
    if(g_wm_state==WM_CONNECTED){ p=scopy(p,"Network="); p=scopy(p,g_wifi_conn); p=scopy(p,"\nChannel="); p=udec(p,g_wm_ch); p=scopy(p,g_wm_ch>14?" (5 GHz)":" (2.4 GHz)");
        p=scopy(p,"\nSignal="); if(g_wifi_rssi>-127){ *p++='-'; p=udec(p,(u32)(-g_wifi_rssi)); p=scopy(p," dBm"); } else p=scopy(p,"unknown");
        p=scopy(p,"\nSpeed=up to "); p=udec(p,(u32)wifi_rate_mbps()); p=scopy(p," Mbit/s\nSecurity="); p=scopy(p,g_wnd_open?"open":g_wpa_akm==8?"WPA3-Personal (SAE)":g_wpa_akm==1?"WPA2-Enterprise (PEAP-MSCHAPv2)":"WPA2-Personal");
        p=scopy(p,"\nAddress="); p=ipstr(p,my_ip); p=scopy(p,"\nGroupKeyRenewals="); p=udec(p,(u32)g_wifi_rekeys); p=scopy(p,"\nFramesIn="); p=udec(p,g_wnd_rx_frames); p=scopy(p,"\nFramesOut="); p=udec(p,g_wnd_tx_frames); p=scopy(p,"\nDropped="); p=udec(p,g_wnd_rx_drop); *p++='\n'; }
    p=scopy(p,"AutoConnect="); p=scopy(p,g_wm_auto?"on":"off"); p=scopy(p,"\n5GHz="); p=scopy(p,g_wm_5g?"on":"off"); p=scopy(p,"\nPowerSaving="); p=scopy(p,wps_mode_name()); p=scopy(p,g_wm_ps?(g_wm_lps?", sleeping now":", active"):""); p=scopy(p,"\nRates="); p=scopy(p,g_wifi_ra?"automatic":"safe fixed"); p=scopy(p,"\nFastReconnect="); p=scopy(p,!g_wm_fast?"off":fr_dis?"off until restart (failed 3 times)":"on"); *p++='\n';
    p=scopy(p,"SavedNetworks="); p=udec(p,(u32)g_wnet_n); p=scopy(p," (names only here - passwords stay in /etc/noovex/wifi, root only)\n");
    for(int i=0;i<g_wnet_n&&p<e;i++){ p=scopy(p,"  "); p=scopy(p,g_wnets[i].ssid); *p++='\n'; }
    p=scopy(p,"Seen=\n"); for(int i=0;i<g_apn&&p<e;i++){ p=scopy(p,"  "); p=scopy(p,g_aps[i].ssid[0]?g_aps[i].ssid:"<hidden>"); p=scopy(p,"  ch "); p=udec(p,g_aps[i].ch); if(g_aps[i].rssi>-127){ p=scopy(p,"  -"); p=udec(p,(u32)(-g_aps[i].rssi)); p=scopy(p," dBm"); } *p++='\n'; }
    *p=0; return (int)(p-o); }
