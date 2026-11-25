#include "include/nvx/net/wifi/wifi_stats.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/net/wifi/wifi_ps.h"
#include "include/nvx/net/wifi/wifi_roam.h"
#include "include/nvx/net/wifi/regdb.h"
#include "include/nvx/net/wifi/wifi_settings.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/net/wifi/wpa2.h"
int mt7925_present(void); void mt7925_status(char* out,int cap);
int mt7921_present(void); extern int mt7921_has_6g,mt7921_6g_on,mt7925_has_6g,mt7925_6g_on,mt7925_eht_on,mt7925_eht_fail;
static const char* const WA_NAME[WSET_COUNT]={"Receive frames in bundles","Send frames in bundles","Power saving","Roaming","Background scans","Fast reconnect","Transmit power","Speed selection","Radio calibration","Country","Follow country rules","WPA3 to WPA2 fallback","WPA3 on Intel cards","Channel width","Bluetooth sharing","Realtek RTL8852 (Wi-Fi 6)","MediaTek MT7925 (Wi-Fi 7)","Realtek RTL8723DE","Use 5 GHz","Connect automatically","MediaTek MT7921AU (USB Wi-Fi 6)"};
static const char* wa_choice(int f,int v){ static const char* PS[3]={"Off","On","On battery"}; static const char* TP[2]={"Simple","With limits"}; static const char* RA[3]={"Fixed","Automatic","Basic"}; static const char* BW[4]={"Auto","20 MHz","40 MHz","80 MHz"}; if(f==WSET_PS&&v>=0&&v<3)return PS[v]; if(f==WSET_TXPWR&&v>=0&&v<2)return TP[v]; if(f==WSET_RA&&v>=0&&v<3)return RA[v]; if(f==WSET_BW&&v>=0&&v<4)return BW[v]; return v?"On":"Off"; }
static int st_wa_tab=0; static char st_wa_speed[512];
static const char* const WA_CC[16]={0,"SE","NO","DK","FI","DE","GB","FR","NL","ES","IT","PL","US","CA","JP","AU"};
static const char* const WA_REGD[14]={"auto","fcc","mkk","etsi","ic","kcc","acma","chile","ukraine","mexico","cn","qatar","uk","ww"};
static void st_wa_cut(char* s,int px){ int n=0; while(s[n])n++; while(n>0&&pw_str(s,1)>px)s[--n]=0; }
static const char* st_wa_detail(int f,char* t,int cap){ char b[640]; char* p=b; b[0]=0;
    switch(f){
    case WSET_BW: if(g_wm_state==WM_CONNECTED&&g_wst_bw){ p=scopy(p,"Now "); p=udec(p,(u32)g_wst_bw); p=scopy(p," MHz - "); } p=scopy(p,"changes apply from the next connection"); break;
    case WSET_RA: if(g_wm_state==WM_CONNECTED){ p=scopy(p,"Now "); p=udec(p,(u32)wifi_rate_mbps()); p=scopy(p," Mbit/s - "); } p=scopy(p,"changes apply from the next connection"); break;
    case WSET_COEX: p=st_coex_plain(b); break;
    case WSET_ROAM: p=scopy(p,"Move to a stronger access point of the same network"); if(g_wifi_roams){ p=scopy(p," - "); p=udec(p,(u32)g_wifi_roams); p=scopy(p," moves so far"); } break;
    case WSET_PS: p=scopy(p,"Now "); p=scopy(p,wps_mode_name()); p=scopy(p,g_wm_ps?(g_wm_lps?", sleeping":", in use"):", not in use"); break;
    case WSET_IQK: p=scopy(p,!g_rtw_iqk_en&&g_rtw_iqk_undone?"Turned itself off after link problems":g_rtw_iqk_last>0?"Corrects the radio at each connection; last run done":g_rtw_iqk_last<0?"Last run failed":"Corrects the radio at each connection"); break;
    case WSET_COUNTRY: { const char* s=regdb_setting(); const char* c=regdb_country(); p=scopy(p,s[0]=='-'?"Automatic, now ":"Set to "); *p++=c[0]; *p++=c[1]; break; }
    case WSET_TXPWR: p=scopy(p,"With limits follows the country's power rules for each speed"); break;
    case WSET_AMPDU_TX: p=scopy(p,"Faster uploads; turns itself off if the access point has trouble"); break;
    case WSET_AMPDU_RX: p=scopy(p,"Faster downloads when the access point asks for it"); break;
    case WSET_BGSCAN: p=scopy(p,"Look for better access points while connected"); break;
    case WSET_FAST: p=scopy(p,"Try the last network, channel and key first"); break;
    case WSET_SAE_FB: p=scopy(p,"On mixed networks, use WPA2 after 3 failed WPA3 tries"); break;
    case WSET_SAE: p=scopy(p,"Lets Intel cards join WPA3 networks"); break;
    case WSET_REGDB: p=scopy(p,"Skip channels the country does not allow, only listen on radar channels"); break;
    case WSET_RTW89: p=scopy(p,"RTL8852BE and RTL8852CE cards"); break;
    case WSET_MT7925: p=scopy(p,"Filogic 360 cards"); break;
    case WSET_RTW8723: p=scopy(p,"Older Wi-Fi 4 cards with Bluetooth"); break;
    case WSET_MT7921U: p=scopy(p,"USB Wi-Fi 6 dongles (Netgear A8000, Comfast CF-952AX, TP-Link TXE50UH)"); break;
    default: break; }
    *p=0; int n=0; for(int i=0;b[i]&&n<cap-1;i++){ char c=b[i]; if(c=='\n')c=' '; if(c==' '&&(!n||t[n-1]==' '))continue; t[n++]=c; } while(n&&t[n-1]==' ')n--; t[n]=0; return t; }
static void st_wa_row(int y,int f){
    int X1=st_x0+st_w0-24, st=wifi_set_state(f), k=wifi_set_kind(f), v=wifi_set_get(f); char t[200];
    if(st==WSET_ST_FAILED){ const char* m="Turned itself off after problems - turn on to try again"; int i=0; while(m[i]){ t[i]=m[i]; i++; } t[i]=0; }
    else if(st==WSET_ST_UNSUPPORTED&&v>=0){ const char* m=wifi_set_text(f); int i=0; while(m[i]&&i<199){ t[i]=m[i]; i++; } t[i]=0; }
    else st_wa_detail(f,t,(int)sizeof t);
    const char* l[8]; int n=wifi_set_max(f)+1; if(n>8)n=8; if(n<1)n=1; for(int j=0;j<n;j++)l[j]=wa_choice(f,j);
    const char* na="Not available in this build"; int cw=v<0?pw_str(na,1):k==WSET_KIND_CHOICE?st_segw(l,n):k==WSET_KIND_COUNTRY?86:44;
    st_rowtext_w(y,WA_NAME[f],t[0]?t:0,X1-cw-16-(st_x0+20));
    if(v<0){ pdstr(X1-pw_str(na,1),y+24,na,ST_DIM,1); return; }
    if(k==WSET_KIND_CHOICE){ int w=st_segw(l,n); st_seg(X1-w,y+17,l,n,v,3100+f*8); return; }
    if(k==WSET_KIND_COUNTRY){ st_btn(X1-40,y+16,40,">",3021,0); st_btn(X1-86,y+16,40,"<",3020,0); return; }
    st_toggle(X1-44,y+20,v>0,3100+f*8+1); }
static void st_wa_card(int* cy,const char* title,const int* f,int n){ int R=64, y=st_card(*cy,R*n,title); for(int i=0;i<n;i++){ if(i)st_sep(y+R*i); st_wa_row(y+R*i,f[i]); } *cy=y+R*n+24; }
static void st_wa_page(int cy){
    static const char* TABS[4]={"Radio","Connection","Region and chips","Status"}; int X1=st_x0+st_w0-24;
    st_seg(st_x0,cy,TABS,4,st_wa_tab,3000); cy+=52;
    if(!rtw_find_mmio()&&!wifi_hw_ops_get()){ int y=st_card(cy,64,0); st_rowtext(y,"No Wi-Fi adapter found","These settings are kept and used once a supported Wi-Fi card is found"); cy=y+64+24; }
    if(st_wa_tab==0){ static const int F[4]={WSET_BW,WSET_PS,WSET_TXPWR,WSET_IQK}; st_wa_card(&cy,"Radio",F,4);
        int y=st_card(cy,192,"Bluetooth and tools"); st_wa_row(y,WSET_COEX); st_sep(y+64);
        st_rowtext(y+64,"Antenna for Wi-Fi only","Gives Wi-Fi the whole antenna when Bluetooth is not needed"); st_btn(X1-130,y+80,130,"Wi-Fi only",3090,0); st_sep(y+128);
        st_rowtext(y+128,"Calibrate the radio now","Measures and corrects the radio; takes a moment"); st_btn(X1-130,y+144,130,"Calibrate",3091,0); }
    else if(st_wa_tab==1){ static const int F[8]={WSET_RA,WSET_AMPDU_TX,WSET_AMPDU_RX,WSET_ROAM,WSET_BGSCAN,WSET_FAST,WSET_SAE_FB,WSET_SAE}; st_wa_card(&cy,"Connection",F,8);
        int y=st_card(cy,64,"WPA3"); st_rowtext(y,"WPA3 hash-to-element",!g_wifi_h2e?"Off - used only when the network requires it":g_wifi_h2e_fail>=3?"Turned itself off after 3 failures - turn on to try again":"Faster, safer WPA3 key setup; needed for 6 GHz networks");
        st_toggle(X1-44,y+20,g_wifi_h2e&&g_wifi_h2e_fail<3,3024); cy=y+64+24; }
    else if(st_wa_tab==2){ static const int F[2]={WSET_COUNTRY,WSET_REGDB}; int y; st_wa_card(&cy,"Region",F,2); cy-=24;
        { extern int g_rtw_txpwr_regd; extern const char* rtw_txpwr_regd_name(void); char t[48],*p=t; const char* n=rtw_txpwr_regd_name(); if(g_rtw_txpwr_regd<0)p=scopy(p,"Automatic, now "); for(int i=0;n[i]&&i<16;i++)*p++=(char)(n[i]>='a'&&n[i]<='z'?n[i]-32:n[i]); *p=0;
          y=st_card(cy+8,64,0); st_rowtext(y,"Power limit region",t); st_btn(X1-40,y+16,40,">",3023,0); st_btn(X1-86,y+16,40,"<",3022,0); cy=y+64+24; }
        static const int C[4]={WSET_RTW89,WSET_MT7925,WSET_RTW8723,WSET_MT7921U}; st_wa_card(&cy,"Extra Wi-Fi chips (experimental)",C,4);
        if(mt7921_present()||mt7925_present()){ int b7=mt7925_present(),six=b7?mt7925_has_6g:mt7921_has_6g,on6=b7?mt7925_6g_on:mt7921_6g_on; y=st_card(cy,b7?128:64,"MediaTek");
            st_rowtext(y,"6 GHz band",!six?"This card has no 6 GHz radio":on6?"Scans and joins 6 GHz networks (WPA3 only)":"Off - only 2.4 and 5 GHz are used"); if(six)st_toggle(X1-44,y+20,on6,3025);
            if(b7){ st_sep(y+64); st_rowtext(y+64,"Wi-Fi 7 (EHT)",!mt7925_eht_on?"Off - Wi-Fi 6 is used":mt7925_eht_fail>=3?"Turned itself off after 3 failures - turn on to try again":"Used when the access point supports it"); st_toggle(X1-44,y+84,mt7925_eht_on&&mt7925_eht_fail<3,3026); }
            cy=y+(b7?128:64)+24; } }
    else { struct wifi_stats ws; wifi_stats_get(&ws); const char* a[7]={"Chip","Firmware","Channel","Width","Speed","Signal","Traffic"}; char v[7][80]; char* q;
        { extern int wifi_info_text(char*,int); static char it[1024]; wifi_info_text(it,sizeof it); q=v[0]; const char* s=it; int n=0; if(s[0]=='A'&&s[7]=='='){ s+=8; while(*s&&*s!='\n'&&n<70){ *q++=*s++; n++; } } if(!n)q=scopy(q,ws.drv?ws.drv:"None"); *q=0; st_wa_cut(v[0],st_w0-220); }
        q=v[1]; q=scopy(q,(g_rxnum||g_wm_up)?"Loaded and running":"Not started"); *q=0;
        q=v[2]; if(ws.conn&&ws.ch){ q=udec(q,(u32)ws.ch); q=scopy(q,ws.ch>14?"  (5 GHz)":"  (2.4 GHz)"); } else q=scopy(q,"-"); *q=0;
        q=v[3]; if(ws.conn&&ws.bw){ q=udec(q,(u32)ws.bw); q=scopy(q," MHz"); } else q=scopy(q,"-"); *q=0;
        q=v[4]; if(ws.conn){ q=udec(q,ws.rate_kbps?ws.rate_kbps/1000:(u32)wifi_rate_mbps()); q=scopy(q," Mbit/s"); } else q=scopy(q,"-"); *q=0;
        q=v[5]; if(ws.conn&&ws.rssi>-127){ *q++='-'; q=udec(q,(u32)(-ws.rssi)); q=scopy(q," dBm"); if(ws.noise<0&&ws.noise>-127){ q=scopy(q,", noise -"); q=udec(q,(u32)(-ws.noise)); q=scopy(q," dBm"); } } else q=scopy(q,"-"); *q=0;
        q=v[6]; q=udec(q,ws.rx); q=scopy(q," in, "); q=udec(q,ws.tx); q=scopy(q," out, "); q=udec(q,ws.rxdrop+ws.txdrop); q=scopy(q," lost, "); q=udec(q,(u32)wst_retry_pct(ws.retry,ws.tx)); q=scopy(q,"% sent again"); *q=0;
        st_infocard(&cy,"Wi-Fi status",a,v,7);
        if(mt7925_present()){ static char mt[768]; const char* mk[8]; char mv[8][80]; static char mkb[8][24]; int n=0; mt7925_status(mt,sizeof mt); const char* s=mt;
            while(*s&&n<8){ int i=0,j=0; const char* l=s; while(l[i]&&l[i]!='\n'&&l[i]!='=')i++; if(l[i]=='='){ for(j=0;j<i&&j<23;j++)mkb[n][j]=l[j]; mkb[n][j]=0; l+=i+1; } else { const char* d="Note"; for(j=0;d[j];j++)mkb[n][j]=d[j]; mkb[n][j]=0; }
                for(i=0;l[i]&&l[i]!='\n'&&i<79;i++){ mv[n][i]=l[i]; } mv[n][i]=0; while(*l&&*l!='\n')l++; s=*l?l+1:l; if(mv[n][0]){ st_wa_cut(mv[n],st_w0-260); mk[n]=mkb[n]; n++; } }
            if(n)st_infocard(&cy,"MediaTek MT7925",mk,mv,n); }
        int y=st_card(cy,64,"Speed test"); { char b[128]; int n=0; while(st_wa_speed[n]&&st_wa_speed[n]!='\n'&&n<127){ b[n]=st_wa_speed[n]; n++; } b[n]=0; st_rowtext_w(y,"Download speed",b[0]?b:"Downloads a test file for up to 15 seconds",X1-136-(st_x0+20)); }
        st_btn(X1-120,y+16,120,"Start",3092,1); }
}
static void st_wa_country(int d){ int v=wifi_set_get(WSET_COUNTRY), cur=0; for(int i=1;i<16;i++) if(v==WSET_CC(WA_CC[i][0],WA_CC[i][1])){ cur=i; break; }
    cur=(cur+d+16)%16; if(wifi_set_set(WSET_COUNTRY,cur?WSET_CC(WA_CC[cur][0],WA_CC[cur][1]):0)<0)toast("The country could not be changed"); }
static void st_wa_regd(int d){ extern int g_rtw_txpwr_regd; extern int rtw_txpwr_set_regd(const char*); extern const char* rtw_txpwr_regd_name(void); int cur=0;
    if(g_rtw_txpwr_regd>=0){ const char* n=rtw_txpwr_regd_name(); for(int i=1;i<14;i++){ int j=0; while(n[j]&&WA_REGD[i][j]==n[j])j++; if(!n[j]&&!WA_REGD[i][j]){ cur=i; break; } } }
    cur=(cur+d+14)%14; if(rtw_txpwr_set_regd(WA_REGD[cur])<0)toast("The region could not be changed"); }
static int st_wa_click(int id){
    if(id>=3000&&id<3004){ st_wa_tab=id-3000; return 1; }
    if(id==3025){ int v=mt7925_present()?!mt7925_6g_on:!mt7921_6g_on; mt7921_6g_on=mt7925_6g_on=v; return 1; }
    if(id==3026){ if(mt7925_eht_on&&mt7925_eht_fail<3)mt7925_eht_on=0; else { mt7925_eht_on=1; mt7925_eht_fail=0; } return 1; }
    if(id==3024){ if(g_wifi_h2e&&g_wifi_h2e_fail<3)g_wifi_h2e=0; else { g_wifi_h2e=1; g_wifi_h2e_fail=0; } return 1; }
    if(id>=3020&&id<3024){ if(id<3022)st_wa_country(id==3020?-1:1); else st_wa_regd(id==3022?-1:1); return 1; }
    if(id>=3100&&id<3100+WSET_COUNT*8){ int f=(id-3100)/8, o=(id-3100)%8;
        if(wifi_set_kind(f)==WSET_KIND_BOOL){ int v=wifi_set_get(f); if(v>=0&&wifi_set_set(f,v>0?0:1)<0)toast("This setting could not be changed"); }
        else if(wifi_set_set(f,o)<0)toast("This setting could not be changed");
        else if(f==WSET_BW||f==WSET_RA)toast("Used from the next connection");
        return 1; }
    return 0; }
static void st_wa_heavy(int id){
    if(id==3090){ u32 m=rtw_find_mmio(); if(!m||!g_rxnum){ toast("Turn Wi-Fi on first"); return; } toast(rtw_coex_wifi_only(m)?"The antenna is now used for Wi-Fi only":"The antenna was set, but the chip did not confirm"); }
    else if(id==3091){ u32 m=rtw_find_mmio(); if(!m||!g_rxnum){ toast("Turn Wi-Fi on first"); return; } rtw_chip_calibrate(m); toast(g_rtw_iqk_last>0?"Calibration done":"Calibration did not finish"); }
    else if(id==3092){ if(g_wm_state!=WM_CONNECTED&&!net_ok){ toast("Connect to a network first"); return; } wifi_speedtest(st_wa_speed,sizeof st_wa_speed,"speedtest.tele2.net","/10MB.zip",0); toast("Speed test finished"); }
}
