#include <stdio.h>
#include <stdlib.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
#include "drivers/net/wifi/realtek/rtw88/rtw_coex_core.h"
static int fails;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)
static int run(rtw_cx_st* s,u8 et,u16 ed,int ec,const char* m){ u8 t; u16 d; int c=rtw_cx_decide(s,&t,&d); if(c!=ec||t!=et||d!=ed){ printf("FAIL: %s (case %d table %u tdma 0x%x)\n",m,c,t,d); fails++; return 0; } return 1; }
static int info(rtw_cx_st* s,u8 src,u8 lb2,u8 lb3,u8 hb0,u8 hb1,u8 hb2,u8 hb3){ u8 b[7]={src,lb2,lb3,hb0,hb1,hb2,hb3}; return rtw_cx_bt_info(s,b,7); }
int main(void){
    rtw_cx_st s={0};
    s.wl_conn=1; run(&s,2,0,CX_WL_ONLY,"BT off, connected: Wi-Fi only table 2, TDMA off");
    s.wl_conn=0; run(&s,1,0,CX_WL_NOTCONN,"BT off, not connected: table 1");
    s.bt_on=1; rtw_cx_link(&s); run(&s,1,0,CX_WL_NOTCONN,"BT up, Wi-Fi not connected: table 1, TDMA off");
    s.wl_conn=1; run(&s,10,3,CX_BT_IDLE,"BT idle, Wi-Fi connected quiet: table 10, TDMA 3");
    s.wl_busy=1; run(&s,11,7,CX_BT_IDLE,"BT idle not connected, Wi-Fi busy: table 11, TDMA 7");
    CHECK(info(&s,1,0x01,0,0,0,0,0)==1,"BT info accepted"); run(&s,12,7,CX_BT_IDLE,"BT connected idle, Wi-Fi busy: table 12, TDMA 7");
    CHECK(info(&s,1,0x01,0,0,0,0,0)==0,"same BT info skipped");
    u8 sh[5]={1,0x21,0,0,0}; CHECK(rtw_cx_bt_info(&s,sh,5)==0,"short BT info ignored");
    CHECK(info(&s,4,0x29,0,0,0,0,0)==0,"scoreboard source ignored");
    s.wl_busy=0; CHECK(info(&s,2,0x29,0,0,0,0,0)==1&&s.hid&&!s.a2dp&&s.status==CX_BTS_ACL_BUSY,"HID decoded from lb2 0x29");
    run(&s,9,9,CX_BT_HID,"BT HID: table 9, TDMA 9");
    info(&s,1,0x29,0,0,0,0x04,0); run(&s,9,18,CX_BT_HID,"BT HID with A2DP active bit: table 9, TDMA 18");
    s.wl_busy=1; info(&s,1,0x29,0,0,0,0x60,0); CHECK(s.hid418,"4/18 HID from hb2"); run(&s,32,27|RTW_CX_4SLOT,CX_BT_HID,"4/18 HID busy: table 32, TDMA 27 4-slot");
    info(&s,1,0x29,0,0,1,0,0); run(&s,27,9,CX_BT_HID,"BLE remote: table 27, TDMA 9");
    s.wl_busy=0; info(&s,1,0x49,0,0,0,0,0); CHECK(s.a2dp&&!s.hid,"A2DP decoded"); run(&s,9,14|RTW_CX_4SLOT,CX_BT_A2DP,"A2DP quiet: table 9, TDMA 14 4-slot");
    s.wl_busy=1; run(&s,12,13|RTW_CX_4SLOT,CX_BT_A2DP,"A2DP busy: table 12, TDMA 13 4-slot");
    info(&s,1,0x49,0,0,0x80,0,0); run(&s,12,13|RTW_CX_4SLOT,CX_BT_OTHER,"A2DP multi-link: other profile path");
    info(&s,1,0x69,0,0,0,0,0); run(&s,12,13|RTW_CX_4SLOT,CX_BT_OTHER,"A2DP+HID: other profile path");
    info(&s,1,0x04,0,0,0,0,0); CHECK(s.inq_page&&s.status==CX_BTS_INQ_PAGE,"inquiry decoded");
    s.wl_busy=0; run(&s,9,27,CX_BT_INQ,"BT inquiry, Wi-Fi connected: table 9, TDMA 27");
    s.wl_busy=1; run(&s,12,18,CX_BT_INQ,"BT inquiry, Wi-Fi busy: table 12, TDMA 18");
    s.wl_scan=1; run(&s,15,8,CX_BT_INQ,"BT inquiry during Wi-Fi scan: table 15, TDMA 8");
    info(&s,1,0x04,0x80,0,0,0,0); run(&s,15,9,CX_BT_INQ,"BT page during Wi-Fi scan: TDMA 9");
    s.wl_scan=0; s.wl_conn=0; s.wl_busy=0; run(&s,1,0,CX_BT_INQ,"BT inquiry, Wi-Fi not connected: table 1, TDMA off");
    info(&s,1,0x00,0,0,0,0,0); s.host_inq=1; run(&s,1,0,CX_BT_INQ,"host BT scan counts as inquiry");
    s.host_inq=0; s.wl_conn=1; s.under5g=1; run(&s,0,0,CX_WL_5G,"Wi-Fi on 5 GHz: table 0, TDMA off");
    s.under5g=0; info(&s,1,0x49,0,0,0,0,0); s.wl_connecting=1; run(&s,9,11|RTW_CX_4SLOT,CX_WL_LINKSCAN,"connecting with A2DP: table 9, TDMA 11 4-slot");
    CHECK(rtw_cx_tab_8821c[9][0]==0x66555555&&rtw_cx_tab_8821c[9][1]==0x5a5a5a5a&&rtw_cx_tdma_8821c[13][0]==0x51&&rtw_cx_tdma_8821c[13][4]==0x54,"8821C tables match rtw88");
    srand(7); for(int i=0;i<200000;i++){ rtw_cx_st r={0}; r.bt_on=rand()&1; r.wl_conn=rand()&1; r.wl_scan=rand()&1; r.wl_connecting=rand()&1; r.wl_busy=rand()&1; r.under5g=(rand()&7)==0; r.host_inq=(rand()&7)==0;
        u8 b[7]; for(int k=0;k<7;k++)b[k]=(u8)rand(); b[0]=(u8)(b[0]&0xF0)|(u8)(1+(rand()&1)); rtw_cx_bt_info(&r,b,7);
        u8 t; u16 d; int c=rtw_cx_decide(&r,&t,&d); if(t>=38||(d&0xFF)>=28||c<0||c>=CX_N||(d&~(0xFFu|RTW_CX_4SLOT))){ printf("FAIL: random state out of range %d %u %x\n",c,t,d); fails++; break; }
        if(!r.bt_on&&(d||t>2)){ printf("FAIL: BT off must stay Wi-Fi only\n"); fails++; break; } }
    { static const unsigned T[]={0,30,50,79,80,100,119,120,150,179,180,200,250,280,281,290,1000}; for(unsigned i=0;i<sizeof T/sizeof T[0];i++) for(int s4=0;s4<2;s4++){ unsigned tb=T[i]; u8 r;
        if(s4&&tb<120)r=0xC1; else if(tb<80&&tb>0){ unsigned t=100/tb; if(100%tb)t++; r=(u8)(t&0x3F); } else if(tb>=180){ unsigned t=tb/100; if(tb%100<=80)t--; r=(u8)((t&0x3F)|0x80); } else r=1;
        if(rtw_cx_slot_byte(s4,tb)!=r){ printf("FAIL: H2C 0x69 slot byte tbtt %u 4slot %d: %02x vs %02x\n",tb,s4,rtw_cx_slot_byte(s4,tb),r); fails++; } }
      CHECK(rtw_cx_slot_byte(1,100)==0xC1&&rtw_cx_slot_byte(0,100)==0x01&&rtw_cx_slot_byte(0,50)==0x02&&rtw_cx_slot_byte(0,200)==0x81&&rtw_cx_slot_byte(0,290)==0x82,"H2C 0x69 byte matches rtw88 rtw_coex_tdma_timer_base"); }
    { u8 o[3]; rtw_cx_chinfo(1,6,0,o); CHECK(!o[0]&&!o[1]&&!o[2],"0x66 on 2.4 GHz shared antenna: 0 0 0");
      rtw_cx_chinfo(1,42,2,o); CHECK(o[0]==0&&o[1]==42&&o[2]==2,"0x66 on 5 GHz: link 0, center channel, bandwidth (8821C afh_5g_num is 1)");
      rtw_cx_chinfo(0,42,2,o); CHECK(!o[0]&&!o[1]&&!o[2],"0x66 disconnected: 0 0 0"); }
    { rtw_cx_st a={0}; a.wl_busy=1; CHECK(rtw_cx_tdma_scbd(&a),"scoreboard TDMA set when busy"); a.a2dp=1; CHECK(rtw_cx_tdma_scbd(&a),"busy with A2DP"); a.multi=1; CHECK(!rtw_cx_tdma_scbd(&a),"A2DP multi-link clears TDMA bit");
      a.multi=0; a.inq_remain=1; CHECK(!rtw_cx_tdma_scbd(&a),"A2DP with inquiry remain clears TDMA bit"); a.a2dp=0; a.wl_busy=0; CHECK(!rtw_cx_tdma_scbd(&a),"quiet clears TDMA bit"); }
    CHECK(rtw_cx_busy(101,0)&&rtw_cx_busy(0,101)&&!rtw_cx_busy(100,100),"busy = more than 100 frames in 2 s (rtw88 watch_dog)");
    CHECK(rtw_cx_nolps(rtw_cx_tdma_8821c[13])&&!rtw_cx_nolps(rtw_cx_tdma_8821c[0]),"TDMA byte1 bit4 without bit5 forces LPS off");
    { rtw_cx_st a={0}; info(&a,1,0x01,0,0,0x02,0,0); CHECK(a.reinit&&!a.ign_off,"BT reinit bit asks for channel info resend");
      info(&a,1,0x01,0,0,0x08,0,0); CHECK(a.ign_off,"ignore-WLAN-act without setup-link asks to clear it"); a.ign_off=0; info(&a,1,0x01,0,0,0x0C,0,0); CHECK(!a.ign_off,"setup link in progress: leave it");
      info(&a,1,0x04,0,0,0,0,0); CHECK(a.inq_page&&a.inq_remain&&!a.inq_fall,"inquiry starts: remain set"); info(&a,1,0x00,0,0,0,0,0); CHECK(!a.inq_page&&a.inq_remain&&a.inq_fall,"inquiry ends: remain held until the 4 s timer"); }
    if(fails){ printf("coex: %d failed\n",fails); return 1; }
    printf("coex: all ok\n"); return 0; }
