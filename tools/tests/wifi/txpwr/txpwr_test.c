#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "include/nvx/base.h"
#include "drivers/net/wifi/realtek/rtw88/rtw_txpwr.c"
#include "drivers/net/wifi/realtek/rtw88/rtw_bw.c"
int wifi_feat_on(int f){ (void)f; return 1; } void wifi_feat_fail(int f,const char* w){ (void)f; (void)w; }
char* scopy(char* p,const char* s){ while(*s)*p++=*s++; *p=0; return p; }
char* udec(char* p,u32 v){ *p=0; (void)v; return p; }
u8 g_wifi_curch=0; int nw=0; u32 W[64];
void klog(const char*s){ printf("klog: %s\n",s); }
u32 rtw_find_mmio(void){ return 0; }
void rtw_set_txpwr(u32 m,u8 ch){ (void)m;(void)ch; }
void rtw_w32(u32 m,u32 o,u32 v){ (void)m; W[(o-0x1d00)/4]=v; nw++; }
struct r2_1s{ signed char ofdm:4, bw20:4; } __attribute__((packed));
struct r2_ns{ signed char bw20:4, bw40:4, cck:4, ofdm:4; } __attribute__((packed));
struct r2g{ u8 cck_base[6]; u8 bw40_base[5]; struct r2_1s ht_1s; struct r2_ns h2,h3,h4; } __attribute__((packed));
struct r5_1s{ signed char ofdm:4, bw20:4; } __attribute__((packed));
struct r5_ns{ signed char bw20:4, bw40:4; } __attribute__((packed));
struct r5_of{ signed char o3:4, o2:4, o4:4, res:4; } __attribute__((packed));
struct r5_v{ signed char bw160:4, bw80:4; } __attribute__((packed));
struct r5g{ u8 bw40_base[14]; struct r5_1s ht_1s; struct r5_ns h2,h3,h4; struct r5_of of; struct r5_v v1,v2,v3,v4; } __attribute__((packed));
struct rtx{ struct r2g g2; struct r5g g5; } __attribute__((packed));
static int ref_group(int ch,int cck){ static const u8 G5[][2]={{36,0},{44,1},{52,2},{60,3},{100,4},{108,5},{116,6},{124,7},{132,8},{140,9},{149,10},{157,11},{165,12},{173,13}};
  if(ch<=14){ if(ch==14)return cck?5:4; return ch<=2?0:ch<=5?1:ch<=8?2:ch<=11?3:4; } int g=0; for(int i=0;i<14;i++)if(ch>=G5[i][0])g=G5[i][1]; return g; }
static int ref_pwr(const struct rtx* t,int ch,int bw,int cls){
  if(ch<=14){ int g=ref_group(ch,cls==0); if(cls==0)return t->g2.cck_base[g]; int p=t->g2.bw40_base[g]; if(cls==1)return p+t->g2.ht_1s.ofdm; return bw==0?p+t->g2.ht_1s.bw20:p; }
  int g=ref_group(ch,0), p=t->g5.bw40_base[g]; if(cls==1)return p+t->g5.ht_1s.ofdm;
  if(bw==0)return p+t->g5.ht_1s.bw20; if(bw==1)return p; return (t->g5.bw40_base[g]+t->g5.bw40_base[g+1])/2+t->g5.v1.bw80; }
int main(void){ int bad=0;
  if(sizeof(struct rtx)!=42||__builtin_offsetof(struct rtx,g5.v1)!=38){ puts("efuse layout"); bad++; }
  { srand(3); static const u8 C2[]={1,2,3,6,9,11,13,14}, C5[]={36,38,42,44,48,52,58,64,100,106,112,122,128,138,140,144,149,155,161,165,171};
    for(int it=0;it<4000;it++){ u8 e[48]; for(int i=0;i<48;i++){ e[i]=(u8)(16+(rand()%40)); } e[11]=(u8)(rand()&0xFF); if(e[11]==0xFF)e[11]=0x12; e[32]=(u8)(rand()&0xFF); if(e[32]==0xFF)e[32]=0x21; e[38]=(u8)(rand()&0xFF); if(e[38]==0xFF)e[38]=0xE0;
      const struct rtx* t=(const struct rtx*)e;
      for(unsigned k=0;k<sizeof C2;k++) for(int bw=0;bw<2;bw++){ int c,o,h; rtw_txpwr_bases(e,C2[k],bw,&c,&o,&h); if(c!=ref_pwr(t,C2[k],bw,0)||o!=ref_pwr(t,C2[k],bw,1)||h!=ref_pwr(t,C2[k],bw,2)){ bad++; printf("2g ch %d bw %d: %d %d %d vs %d %d %d\n",C2[k],bw,c,o,h,ref_pwr(t,C2[k],bw,0),ref_pwr(t,C2[k],bw,1),ref_pwr(t,C2[k],bw,2)); it=1<<30; break; } }
      for(unsigned k=0;k<sizeof C5;k++) for(int bw=0;bw<3;bw++){ if(bw==2&&C5[k]!=42&&C5[k]!=58&&C5[k]!=106&&C5[k]!=122&&C5[k]!=138&&C5[k]!=155&&C5[k]!=171)continue; int c,o,h; rtw_txpwr_bases(e,C5[k],bw,&c,&o,&h); if(o!=ref_pwr(t,C5[k],bw,1)||h!=ref_pwr(t,C5[k],bw,2)){ bad++; printf("5g ch %d bw %d: %d %d vs %d %d\n",C5[k],bw,o,h,ref_pwr(t,C5[k],bw,1),ref_pwr(t,C5[k],bw,2)); it=1<<30; break; } } }
    u8 e[48]; memset(e,0xFF,sizeof e); int c,o,h; rtw_txpwr_bases(e,6,0,&c,&o,&h); if(c!=0x20||o!=0x20||h!=0x20){ bad++; puts("blank efuse defaults"); } rtw_txpwr_bases(e,42,2,&c,&o,&h); if(h!=0x20){ bad++; puts("blank efuse 80"); } }
  { int l20=rtw_txpwr_limit(2,38,0,2), l40=rtw_txpwr_limit(2,38,1,2), l80=rtw_txpwr_limit(2,42,2,4), v40=rtw_txpwr_limit(2,42,1,4);
    printf("limits etsi ht1s ch38 20:%d 40:%d, vht1s ch42 40:%d 80:%d\n",l20,l40,v40,l80); if(l40>l20||l80>63||l40==63){ bad++; puts("bandwidth limits"); }
    g_rtw_txpwr_regd=2; g_rtw_txpwr_bw=1; int i40=rtw_txpwr_index(38,12,0x30,0x30,0x30); g_rtw_txpwr_bw=0; int i20=rtw_txpwr_index(38,12,0x30,0x30,0x30); g_rtw_txpwr_regd=-1;
    printf("index ch38 MCS0 at 40 %d, at 20 %d\n",i40,i20); if(i40>i20){ bad++; puts("40 MHz limit not used"); } }
  { int diff=0, n=0; static const signed char RSB[6]={0,0,1,1,2,2};
    for(int regd=0;regd<RTW_TP_NREGD;regd++) for(int pri=1;pri<=177;pri++){ if(pri>13&&pri<36)continue;
      for(int bw=1;bw<=2;bw++){ if(bw==2&&pri<36)continue; int sec=pri<=13?(pri>7?-1:1):0;
        rtw_bw_ch bc; if(rtw_bw_calc((u8)pri,(u8)bw,sec,0,&bc))continue;
        u8 cbw[3]={(u8)pri,bc.cch40,bc.cch}; g_rtw_txpwr_bw=bw; g_rtw_txpwr_pri=(u8)pri; g_rtw_txpwr_c40=bw==2?(u8)(pri>bc.cch?bc.cch+4:bc.cch-4):bc.cch;
        for(int rs=0;rs<RTW_TP_NRS;rs++){ if(pri<=14&&rs>=4)continue; int mb=RSB[rs]<bw?RSB[rs]:bw, ref=TP_MAX, old=TP_MAX;
          for(int w=0;w<=mb;w++){ int l=tp_lmt_abs(regd,pri>14,w,rs,cbw[w]); if(l<ref)ref=l; l=tp_lmt_abs(regd,pri>14,w,rs,bc.cch); if(l<old)old=l; }
          int got=rtw_txpwr_limit(regd,bc.cch,bw,rs); n++; if(old!=ref)diff++;
          if(got!=ref){ bad++; printf("limit regd %d pri %d bw %d rs %d: %d, rtw88 %d\n",regd,pri,bw,rs,got,ref); } } } }
    g_rtw_txpwr_bw=0; g_rtw_txpwr_pri=0; g_rtw_txpwr_c40=0;
    printf("per-width limit channel (rtw88 cch_by_bw): %d cases, %d differ from using the center channel for every width\n",n,diff); if(!diff){ bad++; puts("no case exercises cch_by_bw"); } }

  rtw_txpwr_tables_init();
  printf("2g base cck %d ofdm %d ht1 %d vht1 %d; 5g ofdm %d vht1 %d\n",tp_base[0][0],tp_base[0][1],tp_base[0][2],tp_base[0][4],tp_base[1][1],tp_base[1][4]);
  if(tp_base[0][0]!=32||tp_base[0][1]!=28||tp_base[0][2]!=26||tp_base[1][4]!=24)bad++;
  if(tp_off[0][0]!=6||tp_off[0][3]!=0||tp_off[0][11]!=0||tp_off[0][4]!=8||tp_off[0][53]!=-4)bad++;
  printf("lmt ch1 etsi cck %d fcc cck %d ww %d; ch36 etsi vht %d\n",rtw_txpwr_limit(2,1,0,0),rtw_txpwr_limit(0,1,0,0),rtw_txpwr_limit(12,1,0,0),rtw_txpwr_limit(2,36,0,4));
  if(rtw_txpwr_limit(2,1,0,0)!=30)bad++;
  if(rtw_txpwr_compute(6,0x2c,0x2a,0x2a)||g_rtw_txpwr_idx[3]!=0x2c+rtw_txpwr_limit(2,6,0,0)-32)bad++;
  rtw_txpwr_write(0,6); printf("writes %d 1d00=%08x 1d2c=%08x 1d34=%08x\n",nw,W[0],W[11],W[13]); if(nw!=8||W[13]>>16)bad++;
  for(int i=0;i<20;i++)printf("%d ",g_rtw_txpwr_idx[i]); printf("| "); for(int i=44;i<54;i++)printf("%d ",g_rtw_txpwr_idx[i]); puts("");
  nw=0; rtw_txpwr_compute(100,0,0x28,0x28); rtw_txpwr_write(0,100); if(nw!=7||g_rtw_txpwr_idx[0])bad++;
  g_rtw_txpwr_used=6; rtw_txpwr_join(6); rtw_txpwr_join(1); rtw_txpwr_join(2); if(g_rtw_txpwr_dead)bad++; rtw_txpwr_join(3); if(!g_rtw_txpwr_dead||rtw_txpwr_on())bad++;
  rtw_txpwr_set_mode(1); if(!rtw_txpwr_on())bad++; if(rtw_txpwr_set_regd("fcc")||g_rtw_txpwr_regd!=0||rtw_txpwr_set_regd("xx")!=-1)bad++;
  for(int i=0;i<3;i++)rtw_txpwr_compute(6,0,0,0); if(!g_rtw_txpwr_dead)bad++;
  printf("%s %d\n",bad?"txpwr: FAIL":"txpwr: all ok",bad); return bad; }
