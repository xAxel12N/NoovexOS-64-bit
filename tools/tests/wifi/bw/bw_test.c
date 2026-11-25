#include <stdio.h>
#include <string.h>
#include "include/nvx/base.h"
#include "drivers/net/wifi/realtek/rtw88/rtw_bw.c"
#include "include/nvx/net/wifi/wifi_netdev.h"
static int nfeatfail, featon=1; void wifi_feat_fail(int f,const char* why){ (void)f; (void)why; nfeatfail++; } int wifi_feat_on(int f){ (void)f; return featon; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ char t[12]; int n=0; do{ t[n++]=(char)('0'+v%10); v/=10; }while(v); while(n)*p++=t[--n]; *p=0; return p; }
static int bad=0;
#define CK(c,...) do{ if(!(c)){ bad++; printf("FAIL %d: ",__LINE__); printf(__VA_ARGS__); puts(""); } }while(0)
static int freq(int ch){ return ch<=14?(ch==14?2484:2407+5*ch):5000+5*ch; }
static void ref_update(int cch,int pri,int bw,u8* pidx,u8* cch40){
    int cf=freq(cch), pf=freq(pri); *cch40=(u8)pri;
    if(bw==1){ *pidx=pf>cf?1:2; *cch40=(u8)cch; }
    else if(bw==2){ if(pf>cf){ *pidx=pf-cf==10?1:3; *cch40=(u8)(cch+4); } else { *pidx=cf-pf==10?2:4; *cch40=(u8)(cch-4); } }
    else *pidx=0; }
static u32 ref_adc(u32 v,int bw,int p){
    if(bw==0){ v&=0xffcffc00; v|=0x10010000; }
    else if(bw==1){ v&=0xff3ff300; v|=0x20020000|((p&0xf)<<2)|1; }
    else { v&=0xfcffcf00; v|=0x40040000|((p&0xf)<<2)|2; }
    return v; }
int main(void){
    static const u8 C80[]={42,58,106,122,138,155,171};
    int n80=0, n40=0;
    for(unsigned k=0;k<sizeof C80;k++) for(int d=-6;d<=6;d+=4){ int pri=C80[k]+d; rtw_bw_ch o; int r=rtw_bw_calc((u8)pri,RTW_BW_80,0,0,&o);
        u8 pi,c40; ref_update(C80[k],pri,2,&pi,&c40);
        CK(r==0&&o.cch==C80[k]&&o.pidx==pi&&o.cch40==c40&&o.bw==RTW_BW_80,"80 pri %d -> %d/%d/%d want %d/%d/%d",pri,o.cch,o.pidx,o.cch40,C80[k],pi,c40);
        rtw_bw_ch o2; CK(rtw_bw_calc((u8)pri,RTW_BW_80,0,C80[k],&o2)==0&&o2.cch==o.cch&&o2.pidx==o.pidx,"80 with vht seg0 %d",pri); n80++; }
    for(int pri=1;pri<=13;pri++) for(int s=-1;s<=1;s+=2){ rtw_bw_ch o; int r=rtw_bw_calc((u8)pri,RTW_BW_40,s,0,&o); int sc=pri+4*s;
        if(sc<1||sc>13){ CK(r!=0,"2g 40 pri %d sec %d should fail",pri,s); continue; }
        u8 pi,c40; ref_update(pri+2*s,pri,1,&pi,&c40); CK(r==0&&o.cch==pri+2*s&&o.pidx==pi,"2g 40 pri %d sec %d -> %d/%d",pri,s,o.cch,o.pidx); n40++; }
    { rtw_bw_ch o; CK(rtw_bw_calc(14,RTW_BW_40,0,0,&o)!=0,"ch14 40"); CK(rtw_bw_calc(6,RTW_BW_80,0,0,&o)!=0,"2g 80"); CK(rtw_bw_calc(37,RTW_BW_40,0,0,&o)!=0,"ch37");
      CK(rtw_bw_calc(36,RTW_BW_40,-1,0,&o)!=0,"36 below"); CK(rtw_bw_calc(36,RTW_BW_80,0,50,&o)!=0,"bad seg0");
      CK(rtw_bw_calc(11,RTW_BW_20,0,0,&o)==0&&o.cch==11&&o.pidx==0,"20"); }
    static const u8 P5[]={36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165,169,173,177};
    for(unsigned k=0;k<sizeof P5;k++){ int pri=P5[k]; rtw_bw_ch o; CK(rtw_bw_calc((u8)pri,RTW_BW_40,0,0,&o)==0,"5g 40 %d",pri);
        int base=pri>=149?149:36, up=((pri-base)/4)&1, cc=up?pri-2:pri+2; u8 pi,c40; ref_update(cc,pri,1,&pi,&c40);
        CK(o.cch==cc&&o.pidx==pi&&pi==(up?1:2),"5g 40 %d -> %d/%d",pri,o.cch,o.pidx); n40++; }
    static const u32 OLD[3]={0,0xFFFFFFFFu,0x12345678u};
    for(int i=0;i<3;i++) for(int bw=0;bw<3;bw++) for(int p=0;p<5;p++) CK(rtw_bw_adcclk(OLD[i],(u8)bw,(u8)p)==ref_adc(OLD[i],bw,bw?p:0),"adcclk %x %d %d",OLD[i],bw,p);
    CK(rtw_bw_adcclk(0,RTW_BW_40,1)==0x20020005u,"adc40"); CK(rtw_bw_adcclk(0,RTW_BW_80,4)==0x40040012u,"adc80");
    CK(rtw_bw_rf18(RTW_BW_20)==0xC00&&rtw_bw_rf18(RTW_BW_40)==0x800&&rtw_bw_rf18(RTW_BW_80)==0x400,"rf18");
    CK(rtw_bw_data_sc(RTW_BW_20,0)==0&&rtw_bw_data_sc(RTW_BW_40,2)==0x02&&rtw_bw_data_sc(RTW_BW_80,1)==0x91&&rtw_bw_data_sc(RTW_BW_80,3)==0x93&&rtw_bw_data_sc(RTW_BW_80,4)==0xA4&&rtw_bw_data_sc(RTW_BW_80,2)==0xA2,"txsc");
    CK(rtw_bw_rfmod(0xFFFFFFFFu,RTW_BW_20)==0xFFFFFE7Fu&&rtw_bw_rfmod(0,RTW_BW_40)==0x80&&rtw_bw_rfmod(0x180,RTW_BW_80)==0x100,"rfmod");
    { u32 a,b,t,c; rtw_bw_dfir(RTW_BW_20,&a,&b,&t,&c); CK(a==2&&b==2&&t==1&&c==0,"dfir20"); rtw_bw_dfir(RTW_BW_40,&a,&b,&t,&c); CK(a==2&&b==2&&t==0&&c==0,"dfir40"); rtw_bw_dfir(RTW_BW_80,&a,&b,&t,&c); CK(a==2&&b==1&&t==0&&c==1,"dfir80"); }
    u8 A[6]={2,0,0,0,0,1}, B[6]={2,0,0,0,0,2}, C[6]={2,0,0,0,0,3};
    u8 ieA[]={45,26,0x6E,0x01,0x17,0xFF,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 61,22,44,0x05,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 191,12,0,0,0,0,0xFE,0xFF,0,0,0xFE,0xFF,0,0, 192,5,1,42,0,0,0};
    u8 ieB[]={45,26,0x6E,0x01,0x17,0xFF,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 61,22,6,0x07,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    u8 ieC[]={45,26,0x2C,0x01,0x17,0xFF,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 61,40,6};
    rtw_bw_note(A,ieA,sizeof ieA); rtw_bw_note(B,ieB,sizeof ieB); rtw_bw_note(C,ieC,sizeof ieC);
    CK(rtw_bw_pick(A,44,1)==RTW_BW_80&&g_rtw_bw_vc0==42,"A 80"); CK(rtw_bw_pick(A,44,0)==RTW_BW_20,"not 8821C");
    CK(rtw_bw_pick(B,6,1)==RTW_BW_40&&g_rtw_bw_sec==-1,"B 40 below"); CK(rtw_bw_pick(C,6,1)==RTW_BW_20,"C 20");
    g_rtw_bw_mode=RTW_BWM_40; CK(rtw_bw_pick(A,44,1)==RTW_BW_40&&g_rtw_bw_sec==1,"A forced 40");
    g_rtw_bw_mode=RTW_BWM_20; CK(rtw_bw_pick(A,44,1)==RTW_BW_20,"forced 20");
    g_rtw_bw_mode=RTW_BWM_AUTO;
    rtw_bw_join_start(A,44,1); CK(rtw_bw_chan(44)==RTW_BW_80&&rtw_bw_chan(36)==RTW_BW_20,"chan");
    g_rtw_bw_cur=RTW_BW_80;
    { u8 lo=0x2C; rtw_bw_ht_cap(&lo); CK(lo==0x6E,"ht cap %x",lo); u8 v[16]; int n=rtw_bw_vht_ie(v); CK(n==14&&v[0]==191&&v[1]==12&&v[2]==0x20&&v[5]==0x01&&v[6]==0xFE,"vht ie"); }
    { u8 rid=RTW_RATEID_GN_N1SS; u32 mask=0xFF000u|0xFF0u, w0=0x40u|(5u<<16)|(1u<<21)|(1u<<30); rtw_bw_ra(&rid,&mask,&w0);
      CK(rid==10&&mask==((0x3FFu<<12)|0xFF0u)&&((w0>>16)&0x1F)==10&&((w0>>24)&3)==2&&((w0>>28)&3)==1&&(w0&(1u<<30)),"ra80 %x %x",mask,w0); }
    rtw_bw_join_done(A,3); CK(g_rtw_bw_falls==1&&rtw_bw_peer_of(A)->bad&&rtw_bw_chan(44)==RTW_BW_20,"join fail fallback");
    CK(rtw_bw_pick(A,44,1)==RTW_BW_20,"A 20 after fail"); g_rtw_bw_mode=RTW_BWM_80; CK(rtw_bw_pick(A,44,1)==RTW_BW_80,"forced 80 despite fail"); g_rtw_bw_mode=RTW_BWM_AUTO;
    rtw_bw_join_start(B,6,1); g_rtw_bw_cur=RTW_BW_40;
    { u8 rid=RTW_RATEID_BGN_20M_1SS; u32 mask=0xFF015u, w0=(3u<<16); rtw_bw_ra(&rid,&mask,&w0); CK(rid==RTW_RATEID_BGN_40M_1SS&&mask==0xFF015u&&((w0>>16)&0x1F)==1&&((w0>>24)&3)==1&&!((w0>>28)&3),"ra40"); }
    rtw_bw_join_done(B,0); CK(rtw_bw_chan(6)==RTW_BW_40,"join ok keeps");
    for(int i=0;i<RTW_BW_LOW_RPTS-1;i++)rtw_bw_ra_report(0x0C); rtw_bw_ra_report(0x13); for(int i=0;i<RTW_BW_LOW_RPTS-1;i++)rtw_bw_ra_report(0x04); CK(!rtw_bw_peer_of(B)->bad,"low rates reset");
    rtw_bw_ra_report(0x0B); CK(rtw_bw_peer_of(B)->bad&&g_rtw_bw_falls==2,"low rates fallback");
    g_rtw_bw_cur=RTW_BW_20; { u8 rid=RTW_RATEID_BGN_20M_1SS; u32 mask=0xFF015u, w0=(3u<<16)|(1u<<23); rtw_bw_ra(&rid,&mask,&w0); CK(rid==3&&w0==((3u<<16)|(1u<<23)),"ra20 unchanged"); u8 lo=0x2C; CK(!rtw_bw_ht_cap(&lo)&&lo==0x2C,"ht20"); u8 v[16]; CK(!rtw_bw_vht_ie(v),"vht20"); }
    CK(g_rtw_bw_drop==1,"low rates while joined ask for an in-session switch to 20 MHz");
    g_rtw_bw_cur=RTW_BW_40; CK(rtw_bw_drop_step(100,50)==RTW_BWD_NARROW,"narrow step");
    { u8 f[32], sa[6]={2,0,0,0,0,1}; int n=rtw_bw_action(f,B,sa,0);
      CK(n==27&&f[0]==0xD0&&f[1]==0&&!memcmp(f+4,B,6)&&!memcmp(f+10,sa,6)&&!memcmp(f+16,B,6)&&f[22]==0&&f[24]==7&&f[25]==0&&f[26]==0,"HT Notify Channel Width = 20 MHz");
      n=rtw_bw_action(f,B,sa,1); CK(n==27&&f[24]==21&&f[25]==2&&f[26]==0,"VHT Operating Mode Notification = 20 MHz, 1 SS"); }
    rtw_bw_narrowed(100,50); CK(rtw_bw_chan(6)==RTW_BW_20&&g_rtw_bw_drop==2,"after narrowing the joined channel is 20 MHz");
    CK(rtw_bw_drop_step(150,51)==RTW_BWD_NONE,"waiting for data"); CK(rtw_bw_drop_step(160,53)==RTW_BWD_OK&&g_rtw_bw_live==1&&!g_rtw_bw_drop,"data at 20 MHz confirms the switch");
    g_rtw_bw_drop=1; g_rtw_bw_cur=RTW_BW_80; CK(rtw_bw_drop_step(1000,7)==RTW_BWD_NARROW,"narrow from 80"); rtw_bw_narrowed(1000,7);
    CK(rtw_bw_drop_step(1000+RTW_BW_VERIFY_T-1,9)==RTW_BWD_NONE,"no data yet"); CK(rtw_bw_drop_step(1000+RTW_BW_VERIFY_T,9)==RTW_BWD_REJOIN&&g_rtw_bw_rejoin==1&&!g_rtw_bw_drop,"no data after the switch: controlled reconnect");
    g_rtw_bw_drop=1; g_rtw_bw_cur=RTW_BW_20; CK(rtw_bw_drop_step(2000,0)==RTW_BWD_NONE&&!g_rtw_bw_drop,"already 20 MHz: nothing to do");
    rtw_bw_peer_of(B)->bad=0; g_rtw_bw_mode=RTW_BWM_40; rtw_bw_join_start(B,6,1); g_rtw_bw_cur=RTW_BW_40; for(int i=0;i<RTW_BW_LOW_RPTS;i++)rtw_bw_ra_report(0x04); CK(!g_rtw_bw_drop,"forced width never narrows in session"); g_rtw_bw_mode=RTW_BWM_AUTO;
    g_rtw_bw_drop=2; rtw_bw_leave(); CK(!g_rtw_bw_drop,"disconnect cancels the switch");
    rtw_bw_peer_of(B)->bad=0; g_rtw_bw_dead=0; featon=0; rtw_bw_join_start(B,6,1); CK(g_rtw_bw_dead&&rtw_bw_chan(6)==RTW_BW_20,"guard off at boot (unfinished start or 3 failures) keeps auto at 20 MHz"); featon=1; g_rtw_bw_dead=0; rtw_bw_leave();
    { u8 me[6]={2,0,0,0,0,1}, ot[6]={9,9,9,9,9,9}; u8 f[24]; struct { u8 fc0, fc1; const u8* a1; const u8* a2; int want; const char* what; } T[]={
        {0x08,0x02,me,B,1,"data from the AP to us"},{0x88,0x02,me,B,1,"QoS data from the AP to us"},{0x08,0x42,me,B,1,"protected data"},
        {0x48,0x02,me,B,0,"null data"},{0xC8,0x02,me,B,0,"QoS null"},{0x08,0x02,0,B,0,"broadcast"},{0x08,0x02,me,ot,0,"another AP"},
        {0x08,0x01,me,B,0,"to-DS"},{0x08,0x03,me,B,0,"4-address"},{0xD0,0x00,me,B,0,"action frame"}};
      for(unsigned i=0;i<sizeof T/sizeof T[0];i++){ memset(f,0,24); f[0]=T[i].fc0; f[1]=T[i].fc1; for(int k=0;k<6;k++){ f[4+k]=T[i].a1?T[i].a1[k]:0xFF; f[10+k]=T[i].a2[k]; f[16+k]=0x33; } CK(wnd_ap_uc(f,me,B)==T[i].want,"unicast-from-AP filter: %s",T[i].what); }
      u32 uc=0, all=0; g_rtw_bw_drop=1; g_rtw_bw_cur=RTW_BW_40; CK(rtw_bw_drop_step(5000,uc)==RTW_BWD_NARROW,"narrow"); rtw_bw_narrowed(5000,uc);
      for(int t=1;t<=RTW_BW_VERIFY_T;t++){ memset(f,0,24); f[0]=0x08; f[1]=0x02; for(int k=0;k<6;k++){ f[4+k]=t&1?0xFF:me[k]; f[10+k]=t&1?B[k]:ot[k]; } all++; if(wnd_ap_uc(f,me,B))uc++;
        int r=rtw_bw_drop_step(5000+(u64)t,uc); if(r!=RTW_BWD_NONE){ CK(r==RTW_BWD_REJOIN&&t==RTW_BW_VERIFY_T,"AP that ignores the notify (only broadcasts and other APs' frames) is not taken as working: %d at %d",r,t); break; } }
      CK(all>=RTW_BW_VERIFY_RX&&uc==0,"stream had frames but none unicast from the AP"); }
    { u8 ie[]={45,26,0x6E,0,0x17,0xFF,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 191,12,0x20,0,0,0,0xFE,0xFF,0,0,0xFE,0xFF,0,0}; u8 ie2[]={45,26,0x2C,0,0x17,0xFF,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
      CK(rtw_bw_sgi(RTW_BW_40,ie,sizeof ie,0)==1&&rtw_bw_sgi(RTW_BW_80,ie,sizeof ie,0)==1&&rtw_bw_sgi(RTW_BW_20,ie,sizeof ie,0)==0,"SGI40/SGI80 from the AP capabilities (rtw88 rtw_update_sta_info)");
      CK(rtw_bw_sgi(RTW_BW_40,ie2,sizeof ie2,1)==0&&rtw_bw_sgi(RTW_BW_80,ie2,sizeof ie2,1)==0&&rtw_bw_sgi(RTW_BW_40,ie2,0,1)==1,"no SGI40 bit, no VHT: no SGI; no IEs keeps the old value"); }
    for(int i=0;i<200000;i++){ u8 ie[64]; u8 bs[6]={(u8)i,(u8)(i>>8),3,4,5,6}; for(int k=0;k<64;k++)ie[k]=(u8)(i*31+k*7+(i>>3)); rtw_bw_note(bs,ie,i%65);
        u8 pri=(u8)(i%180); rtw_bw_ch o; for(int bw=0;bw<3;bw++) if(rtw_bw_calc(pri,(u8)bw,(i%3)-1,(u8)(i>>5),&o)==0){ if(o.bw&&(o.cch<3||o.cch>175))bad++; } (void)rtw_bw_pick(bs,pri,1); }
    char sb[240]; rtw_bw_status(sb); printf("%s\n",sb);
    printf("80 MHz cases %d, 40 MHz cases %d\n",n80,n40);
    printf("%s %d\n",bad?"bw: FAIL":"bw: all ok",bad); return bad; }
