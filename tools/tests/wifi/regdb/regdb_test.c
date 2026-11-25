#include <stdio.h>
#include <string.h>
#include "net/wifi/regdb.c"
int g_wm_state=0; static u8 g_bs[6]; static int nlog=0;
void klog(const char* s){ nlog++; printf("log: %s\n",s); }
const u8* wnd_bssid(void){ return g_bs; }
static int bad=0;
#define T(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); bad++; } }while(0)
int main(void){
    T(!strcmp(regdb_country(),"SE"));
    for(int c=1;c<=13;c++){ T(regdb_ch_allowed((u8)c)); T(regdb_probe_ok((u8)c)); T(regdb_max_dbm((u8)c)==20); }
    T(!regdb_ch_allowed(14));
    T(regdb_ch_allowed(36)&&!regdb_is_dfs(36)&&regdb_max_dbm(36)==23&&regdb_probe_ok(36));
    T(regdb_ch_allowed(52)&&regdb_is_dfs(52)&&!regdb_probe_ok(52)&&regdb_max_dbm(52)==20);
    T(regdb_ch_allowed(100)&&regdb_is_dfs(100)&&regdb_max_dbm(100)==27);
    T(!regdb_ch_allowed(144));
    T(regdb_ch_allowed(149)&&!regdb_is_dfs(149)&&regdb_max_dbm(149)==14);
    T(regdb_dfs_region()==2);
    T(regdb_freq_flags(5955,20)==REGDB_NOOUTDOOR&&regdb_freq_max_dbm(5955)==23);
    T(regdb_freq_flags(6435,20)<0);
    T(regdb_set_country("us")==0&&!strcmp(regdb_country(),"US"));
    T(regdb_ch_allowed(11)&&!regdb_ch_allowed(12)&&!regdb_ch_allowed(13)&&regdb_dfs_region()==1);
    T(regdb_set_country("XQ")<0&&regdb_set_country("S")<0&&!strcmp(regdb_country(),"US"));
    T(regdb_set_country("00")==0);
    T(regdb_set_country("AU")==0&&!strcmp(regdb_country(),"AU")&&!strcmp(regdb_setting(),"AU"));
    T(regdb_set_country("--")==0&&!strcmp(regdb_setting(),"--")&&!strcmp(regdb_country(),"AU"));
    T(regdb_set_country("00")==0);
    T(regdb_ch_allowed(12)&&regdb_no_ir(12)&&!regdb_probe_ok(12)&&!regdb_no_ir(6));
    T(regdb_set_country("auto")==0&&!strcmp(regdb_country(),"00"));
    regdb_set_country("SE"); regdb_set_country("auto");
    u8 ap[6]={2,0,0,0,0,1}; regdb_note_ie(ap,(const u8*)"JP ");
    T(!strcmp(regdb_country(),"SE"));
    memcpy(g_bs,ap,6); g_wm_state=3; int l0=nlog;
    T(!strcmp(regdb_country(),"JP")&&regdb_ch_allowed(14)&&nlog==l0+1);
    T(!strcmp(regdb_country(),"JP")&&nlog==l0+1);
    regdb_note_ie(ap,(const u8*)"z9"); T(!strcmp(regdb_country(),"JP"));
    regdb_set_country("DE"); T(!strcmp(regdb_country(),"DE"));
    g_wm_state=0; regdb_set_country("SE");
    for(int i=0;i<20;i++){ u8 b[6]={9,9,9,9,9,(u8)i}; regdb_note_ie(b,(const u8*)"US"); }
    u8 l[40]; int n=0; for(int c=1;c<=14;c++)l[n++]=(u8)c; u8 c5[]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165};
    for(unsigned i=0;i<sizeof(c5);i++)l[n++]=c5[i];
    int m=regdb_filter(l,n); T(m==n-2); for(int i=0;i<m;i++)T(l[i]!=14&&l[i]!=144);
    regdb_scan_result(2,0); regdb_scan_result(2,5); regdb_scan_result(2,0); regdb_scan_result(2,0); T(regdb_enabled());
    regdb_scan_result(0,0); T(regdb_enabled());
    regdb_scan_result(2,0); T(!regdb_enabled()&&regdb_ch_allowed(14)&&regdb_probe_ok(52));
    T(regdb_filter(l,n)==n);
    regdb_enable(1); T(regdb_enabled()&&!regdb_ch_allowed(14));
    T(regdb_ch_freq(1)==2412&&regdb_ch_freq(14)==2484&&regdb_ch_freq(36)==5180&&regdb_ch_freq(0)==0&&!regdb_ch_allowed(0)&&!regdb_probe_ok(0));
    printf(bad?"regdb: %d failed\n":"regdb: all ok\n",bad); return bad!=0; }
