#ifndef NVX_NET_WIFI_WIFI_SETTINGS_H
#define NVX_NET_WIFI_WIFI_SETTINGS_H
enum { WSET_AMPDU_RX=0, WSET_AMPDU_TX, WSET_PS, WSET_ROAM, WSET_BGSCAN, WSET_FAST, WSET_TXPWR, WSET_RA, WSET_IQK, WSET_COUNTRY, WSET_REGDB, WSET_SAE_FB, WSET_SAE, WSET_BW, WSET_COEX, WSET_RTW89, WSET_MT7925, WSET_RTW8723, WSET_5G, WSET_AUTO, WSET_MT7921U, WSET_COUNT };
enum { WSET_ST_OFF=0, WSET_ST_ON, WSET_ST_FAILED, WSET_ST_UNSUPPORTED, WSET_ST_NOHW };
enum { WSET_KIND_BOOL=0, WSET_KIND_CHOICE, WSET_KIND_COUNTRY };
#define WSET_CC(a,b) ((((int)(unsigned char)(a))<<8)|(int)(unsigned char)(b))
int wifi_set_get(int f);
int wifi_set_set(int f,int v);
const char* wifi_set_text(int f);
const char* wifi_set_name(int f);
int wifi_set_kind(int f);
int wifi_set_max(int f);
const char* wifi_set_choice(int f,int v);
int wifi_set_state(int f);
int wifi_set_default(int f);
extern int g_wifi_bgscan, g_rtw_coex_en, g_rtw_bw_dead;
int wifi_feat_begin(int f);
void wifi_feat_ok(int f);
void wifi_feat_fail(int f,const char* why);
void wifi_feat_unsup(int f,const char* why);
int wifi_feat_on(int f);
int wifi_feat_report(char* o,int cap);
#endif
