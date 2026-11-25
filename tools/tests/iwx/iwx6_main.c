#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#define NVX_BASE_H
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int8_t s8;
#define NVX_CORE_KLOG_H
#define NVX_NET_WIFI_WIFI_HW_H
static void klog(const char* m){ printf("klog: %s\n",m); }
static char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
static char* udec(char* p,u32 v){ char t[12]; int n=0; do t[n++]=(char)('0'+v%10); while(v/=10); while(n)*p++=t[--n]; *p=0; return p; }
#define NVX_LIB_FMT_H
#include "drivers/net/wifi/intel/iwx/iwx_cmd.h"
static int g_ax=1,g_uhb=1,g_parsed;
static u8 g_last_ch;
int g_wifi_scanning=1;
struct iwx_nvm g_nvm;
int iwx_is_ax210(struct iwx_softc* sc){ (void)sc; return g_ax; }
int iwx_fw_capa(struct iwx_softc* sc,int b){ (void)sc; return b==48?g_uhb:b==89; }
const struct iwx_nvm* iwx_nvm_info(void){ return &g_nvm; }
static u8 g_resp[600]; static u32 g_rlen;
int iwx_send_cmd_sync(struct iwx_softc* sc,u32 id,const void* d,u32 len,void* resp,u32 max,u32* rlen,u32 t){ (void)sc;(void)id;(void)d;(void)len;(void)t; u32 n=g_rlen<max?g_rlen:max; memcpy(resp,g_resp,n); *rlen=g_rlen; return 0; }
int wifi_scan_parse(const u8* fr,int flen,u8 ch,int rssi,int onch){ (void)fr;(void)flen;(void)rssi;(void)onch; g_parsed++; g_last_ch=ch; return 1; }
#include "drivers/net/wifi/intel/iwx/mvm/iwx_6g.c"
static int fails;
#define CK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fails++; } }while(0)
int main(void){
    CK(iwx6_vch(1)==178&&iwx6_vch(233)==236&&iwx6_vch(2)==0&&iwx6_vch(237)==0);
    for(int c=1;c<=233;c+=4){ CK(iwx6_ch(iwx6_vch(c))==c); CK(iwx6_is_vch(iwx6_vch(c))); }
    CK(!iwx6_is_vch(165)&&!iwx6_is_vch(177)&&!iwx6_is_vch(237));
    int np=0; for(int c=1;c<=233;c++)if(iwx6_psc(c))np++; CK(np==15&&iwx6_psc(37)&&!iwx6_psc(33));
    CK(iwx6_rx_chan(1,0x80,37)==iwx6_vch(37)&&iwx6_rx_chan(1,0x40,6)==6&&iwx6_rx_chan(0,0x80,37)==37&&iwx6_rx_chan(1,0x00,36)==36);
    u32 prof[110]; for(int i=0;i<110;i++)prof[i]=1|(1u<<3);
    g_nvm.band24=g_nvm.band5=1;
    iwx6_chmap(&g_nvm,prof,4,110,1);
    CK(g_nvm.n_channels==14+33); CK(iwx6_count()==59);
    int has100=0,has68=0,hasu4=0; for(int i=0;i<g_nvm.n_channels;i++){ if(g_nvm.chan[i]==100)has100=1; if(g_nvm.chan[i]==68)has68=1; if(g_nvm.chan[i]>=169)hasu4=1; }
    CK(has100&&has68&&!hasu4);
    prof[22]=0; prof[30]=0; iwx6_chmap(&g_nvm,prof,4,110,1);
    has100=has68=0; for(int i=0;i<g_nvm.n_channels;i++){ if(g_nvm.chan[i]==100)has100=1; if(g_nvm.chan[i]==68)has68=1; } CK(!has100&&!has68);
    u16 p16[51]; for(int i=0;i<51;i++)p16[i]=1; iwx6_chmap(&g_nvm,p16,2,51,0); CK(g_nvm.n_channels==47&&iwx6_count()==0);
    iwx6_chmap(&g_nvm,prof,4,110,0); CK(iwx6_count()==0);
    iwx6_chmap(&g_nvm,prof,4,110,1);
    struct iwx_scan_channel_cfg_umac e[67]; memset(e,0,sizeof e);
    int k=iwx6_scan_fill(e,67); CK(k==15); CK(e[0].channel_num==5&&e[0].b1==2&&e[14].channel_num==229);
    CK(iwx6_scan_fill(e,3)==3);
    g_iwx_6g=0; CK(iwx6_scan_fill(e,67)==0); g_iwx_6g=1;
    u8 bc[128]; memset(bc,0,sizeof bc); bc[0]=0x80; u32 n=36;
    bc[n++]=0; bc[n++]=3; bc[n++]='a'; bc[n++]='b'; bc[n++]='c';
    u8 rsn_psk[]={48,20,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,2,0xC0,0};
    u8 rsn_sae[]={48,20,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,4,1,0,0,0x0F,0xAC,8,0xC0,0};
    memcpy(bc+n,rsn_psk,sizeof rsn_psk);
    g_parsed=0; iwx6_rx_mgmt(bc,n+sizeof rsn_psk,-50,(u8)iwx6_vch(37)); CK(g_parsed==0);
    memcpy(bc+n,rsn_sae,sizeof rsn_sae);
    iwx6_rx_mgmt(bc,n+sizeof rsn_sae,-50,(u8)iwx6_vch(37)); CK(g_parsed==1&&g_last_ch==iwx6_vch(37));
    iwx6_rx_mgmt(bc,n-3,-50,(u8)iwx6_vch(37)); CK(g_parsed==1);
    bc[n+1]=200; iwx6_rx_mgmt(bc,n+sizeof rsn_sae,-50,(u8)iwx6_vch(37)); CK(g_parsed==1); bc[n+1]=20;
    rsn_sae[19]=6; memcpy(bc+n,rsn_sae,sizeof rsn_sae); iwx6_rx_mgmt(bc,n+sizeof rsn_sae,-50,(u8)iwx6_vch(37)); CK(g_parsed==1);
    u8 ies[64]; u32 il=iwx6_assoc_ies(ies,sizeof ies,iwx6_vch(37));
    CK(il==29&&ies[0]==255&&ies[1]==22&&ies[2]==35&&ies[24]==255&&ies[25]==3&&ies[26]==59);
    CK(iwx6_assoc_ies(ies,10,iwx6_vch(37))==0);
    CK(iwx6_join_ok(36,0)&&iwx6_join_ok(iwx6_vch(37),2)&&!iwx6_join_ok(iwx6_vch(37),0));
    g_iwx_6g=0; CK(!iwx6_join_ok(iwx6_vch(37),2)); g_iwx_6g=1;
    g_nvm.lar_enabled=1;
    memset(g_resp,0,sizeof g_resp); g_resp[4]='E'; g_resp[5]='S'; u32 nch=110; memcpy(g_resp+16,&nch,4); for(int i=0;i<110;i++){ u32 v=(i<14||i>=51)?1:0; memcpy(g_resp+20+i*4,&v,4); }
    g_rlen=20+110*4; CK(iwx6_mcc_update((struct iwx_softc*)1,1)==0); CK(g_nvm.n_channels==14&&iwx6_count()==59);
    g_rlen=20+109*4; CK(iwx6_mcc_update((struct iwx_softc*)1,1)<0); CK(g_nvm.n_channels==14);
    nch=500; memcpy(g_resp+16,&nch,4); g_rlen=600; CK(iwx6_mcc_update((struct iwx_softc*)1,1)<0);
    CK(iwx6_mcc_update((struct iwx_softc*)1,0)==0);
    printf(fails?"iwx6: %d FAILED\n":"iwx6: all passed\n",fails);
    return fails?1:0;
}
