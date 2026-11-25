#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_sta.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_chan.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_mac.h"
#include "include/nvx/net/wifi/wifi_hw.h"
const struct rtw89_hci_ops* rtw89_hci;
rtw89_c2h_cb rtw89_c2h_hook;
void klog(const char* m){ (void)m; }
const char* regdb_country(void){ return "US"; }
int regdb_max_dbm(u8 ch){ (void)ch; return 0; }
void* dma_alloc_pages(unsigned long pg){ void* p=mmap(0,pg*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0); return p==MAP_FAILED?0:p; }
int rtw89_write_xtal_si(struct rtw89_softc* sc,u8 off,u8 val,u8 mask){ (void)sc;(void)off;(void)val;(void)mask; return 0; }
static int whw_n, whw_data; void wifi_hw_rx(const u8* f,int len,int rssi){ (void)rssi;(void)len; whw_n++; if((f[0]&0x0C)==8)whw_data++; }
int wifi_hw_assoc_ie(u8* out,int cap){ (void)out;(void)cap; return 0; }
struct h2c { u8 cls,func; };
static struct h2c hl[64]; static int hn;
int rtw89_h2c_send(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,int rack,int dack,const u8* p,u32 len){ (void)sc;(void)rack;(void)dack;(void)p;(void)len; assert(cat==1); if(hn<64){ hl[hn].cls=cls; hl[hn].func=func; } hn++; return 0; }
static int acks; int rtw89_h2c_wait_done(struct rtw89_softc* sc,u8 cls,u8 func,u32 us){ (void)sc;(void)us; assert(cls==9&&func==0x16&&hn>0&&hl[hn-1].func==0x16); acks++; return 0; }
int rtw89_c2h_handle(struct rtw89_softc* sc,const u8* c,u32 len){ u32 w0=(u32)c[0]|((u32)c[1]<<8); if(rtw89_c2h_hook)rtw89_c2h_hook(sc,(u8)(w0&3),(u8)((w0>>2)&0x3F),(u8)(w0>>8),c+8,len-8); return 0; }

static u8 regs[0x20010]; static u32 rf[2][256]; static int swsi_dead, unlock; static unsigned long dl, nwr;
static u32 g32(u32 o){ return (u32)regs[o]|((u32)regs[o+1]<<8)|((u32)regs[o+2]<<16)|((u32)regs[o+3]<<24); }
static void p32(u32 o,u32 v){ for(int i=0;i<4;i++)regs[o+i]=(u8)(v>>(8*i)); }
static u32 ph(u32 a){ return g32(0x10000+a); }
static u32 rd(struct rtw89_softc* sc,u32 o,int w){ (void)sc; assert(o+(u32)w<=sizeof regs); u32 v=0; for(int i=0;i<w;i++)v|=(u32)regs[o+i]<<(8*i);
    if(o==0x1174C&&swsi_dead)v|=1u<<25; return v; }
static void wr(struct rtw89_softc* sc,u32 o,u32 v,int w){ (void)sc; assert(o+(u32)w<=sizeof regs); nwr++; for(int i=0;i<w;i++)regs[o+i]=(u8)(v>>(8*i));
    if(o==0x10370){ int p=(v>>28)&7; u32 a=(v>>20)&0xFF, d=v&0xFFFFF; assert(p<2); if(v>>31){ u32 m=g32(0x10374)&0xFFFFF; rf[p][a]=(rf[p][a]&~m)|(d&m); } else rf[p][a]=d;
        if(a==0xc5&&unlock)rf[p][a]&=~0x8000u; }
    if(o==0x10378&&!swsi_dead){ int p=(v>>8)&7; u32 a=v&0xFF; assert(p<2); p32(0x1174C,(1u<<26)|(a==0xc5&&unlock?rf[p][a]&~0x8000u:rf[p][a])); } }
static void dly(u32 us){ dl+=us; }
static const struct rtw89_io IO={ rd,wr,dly };
static u32 rfv1(int p,u32 a){ return ph((p?0xF000u:0xE000u)+(a<<2))&0xFFFFF; }
static void reset_hw(void){ memset(regs,0,sizeof regs); memset(rf,0,sizeof rf); for(int p=0;p<2;p++){ rf[p][0x18]=0xFFFFF; rf[p][0xc5]=0x8000; p32(0x10000+(p?0xF000u:0xE000u)+0x18*4,0xFFFFF); } swsi_dead=unlock=0; dl=0; nwr=0; memset(&g_r89ch,0,sizeof g_r89ch); }

static void test_channel(void){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=RTW89_CHIP_8852B;
    assert(rtw89_encode_chan_idx(1)==1&&rtw89_encode_chan_idx(14)==14&&rtw89_encode_chan_idx(36)==0x20&&rtw89_encode_chan_idx(64)==0x2E&&rtw89_encode_chan_idx(100)==0x30&&rtw89_encode_chan_idx(132)==0x40&&rtw89_encode_chan_idx(144)==0x46&&rtw89_encode_chan_idx(149)==0x50&&rtw89_encode_chan_idx(177)==0x5E);
    assert(rtw8852b_sco_mapping(1)==109&&rtw8852b_sco_mapping(6)==108&&rtw8852b_sco_mapping(11)==106&&rtw8852b_sco_mapping(36)==51&&rtw8852b_sco_mapping(64)==49&&rtw8852b_sco_mapping(149)==46&&rtw8852b_sco_mapping(165)==45);
    reset_hw(); p32(0x10000+0x49C4,0xF0Fu); regs[0xC010]=3;
    assert(rtw8852b_set_channel(&sc,36)==0&&g_r89ch.ok&&g_r89ch.ch==36&&g_r89ch.band==1&&g_r89ch.lck_timeouts==0&&g_r89ch.lck_fix==0);
    u32 e36=0xC0000u|36u|(1u<<16)|(1u<<8)|(3u<<10)|0x1000u;
    for(int p=0;p<2;p++){ assert((rf[p][0x18]&0xFFFFF)==e36&&rfv1(p,0x18)==e36&&(rf[p][0xcf]&1)==1&&(rf[p][0x3f]&0x3F)==0x1b&&(rf[p][0xee]&4)==0&&(rf[p][0x33]&0x1F)==0x12); }
    assert(!(ph(0x4738)&(1u<<17))&&!(ph(0x4AA4)&(1u<<17))&&(ph(0x49C0)&0x7F)==51&&((ph(0x0734)>>16)&0xFF)==0x20&&(ph(0x2300)&0xFFFFFF)==0x3d23ff&&(ph(0x231c)&0xFFFFFF)==0xffdff5);
    assert(!(ph(0x0700)&(1u<<5))&&(ph(0x2344)>>31)==1&&(ph(0x49C0)>>30)==0&&(ph(0x49C4)&0x3F00)==0&&((ph(0x12AC)>>12)&0xFFF)==0x333&&((ph(0x32AC)>>12)&0xFFF)==0x333);
    assert(((ph(0xC0EC)>>13)&3)==0&&((ph(0xC0E4)>>4)&3)==2&&((ph(0xC1E4)>>4)&3)==2&&!(ph(0x46F8)&(1u<<12))&&!(ph(0x4440)>>31)&&(ph(0x0704)&2)&&((ph(0x1200)>>28)&7)==0&&((ph(0x20fc)>>24)&0xFF)==0);
    assert(!(ph(0x58DC)&(1u<<30))&&!(ph(0x7818)&(1u<<30))&&!(ph(0x0C3C)&(1u<<9))&&!(ph(0x4738)&(1u<<19))&&!(ph(0x4420)>>31));
    assert((regs[0xC010]&3)==0&&g32(0xC088)==0&&(regs[0xC628]&0x13)==3);
    reset_hw(); p32(0x10000+0x49C4,1);
    assert(rtw8852b_set_channel(&sc,1)==0); u32 e1=0xC0000u|1u|(3u<<10)|0x1000u;
    for(int p=0;p<2;p++)assert((rf[p][0x18]&0xFFFFF)==e1&&rfv1(p,0x18)==e1);
    assert((ph(0x4738)>>17&1)==1&&(ph(0x49C0)&0x7F)==109&&((ph(0x0734)>>16)&0xFF)==1&&(ph(0x23B0)&0x7FFFF)==0x1cfea&&(ph(0x23B4)&0x7FFFF)==0x27de3);
    assert((ph(0x0700)>>5&1)==1&&!(ph(0x2344)>>31)&&((ph(0x32AC)>>12)&0xFFF)==0x111&&((ph(0x12AC)>>12)&0xFFF)==0x333&&(regs[0xC628]&0x13)==0x10);
    reset_hw(); assert(rtw8852b_set_channel(&sc,14)==0&&(ph(0x2300)&0xFFFFFF)==0x3b13ff&&(ph(0x23B0)&0x7FFFF)==0x1ddc4&&(ph(0x49C0)&0x7F)==106);
    reset_hw(); unlock=1; assert(rtw8852b_set_channel(&sc,6)==0&&g_r89ch.lck_fix==3&&dl<100000);
    reset_hw(); swsi_dead=1; sc.err=0; assert(rtw8852b_set_channel(&sc,6)==-1&&!g_r89ch.ok&&strstr(sc.err,"RF register")&&dl<1000&&((ph(0x20fc)>>24)&0xFF)==0);
    assert(rtw8852b_set_channel(&sc,37)==-1);
    reset_hw(); sc.err=0; assert(rtw89_set_channel(&sc,6)==-1&&nwr==0); sc.state=RTW89_ST_READY; assert(rtw89_set_channel(&sc,6)==-1&&nwr==0); sc.phy_ok=1;
    assert(rtw89_set_channel(&sc,6)==0&&g_r89ch.switches==1); unsigned long w=nwr; assert(rtw89_set_channel(&sc,6)==0&&nwr==w&&g_r89ch.switches==1);
    printf("channel: RTL8852B set_channel 2.4/5 GHz 20 MHz (MAC RFMOD/TXRATE_CHK, BB band sel/SCO/TXFIR/CCK/BW/ADC/chan idx/5M mask/reset, RF 0x18 SWSI+direct, LCK check, RXBB BW, help enter/leave), RF read timeout fails closed, not-ready refused, same channel skipped\n"); }

static void test_channel_c(const char* fn){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=RTW89_CHIP_8852C; sc.cv=1; memset(&rtw89_gain,0,sizeof rtw89_gain);
    assert(rtw89_chan_freq(1)==2412&&rtw89_chan_freq(14)==2484&&rtw89_chan_freq(36)==5180&&rtw89_chan_freq(165)==5825);
    assert(rtw8852c_spur_freq(6)==2440&&rtw8852c_spur_freq(13)==2480&&rtw8852c_spur_freq(153)==5760&&rtw8852c_spur_freq(1)==0&&rtw8852c_spur_freq(149)==0);
    int t,f; assert(rtw8852c_csi_tone(6)==38); rtw8852c_nbi_tone(6,&t,&f); assert(t==41&&f==2); assert(rtw8852c_csi_tone(153)==1984); rtw8852c_nbi_tone(153,&t,&f); assert(t==16&&f==0);
    assert(rtw8852c_csi_tone(13)==102); rtw8852c_nbi_tone(13,&t,&f); assert(t==(25+32)%64&&f==2);
    reset_hw(); p32(0x10000+0x2320,1); assert(rtw8852c_set_channel(&sc,36)==0&&g_r89ch.ok&&g_r89ch.ch==36&&g_r89ch.band==1);
    u32 e=(0xFFFFFu&~(0x30000u|0x300u|0xFFu|0xC00u))|36u|(1u<<16)|(1u<<8)|(3u<<10);
    for(int p=0;p<2;p++){ assert((rf[p][0x18]&0xFFFFF)==e&&rfv1(p,0x18)==e&&(rf[p][0x3f]&0x3F)==0x1b&&(rf[p][0x33]&0x1F)==0xa&&(rf[p][0xee]&4)==0&&((rf[p][0x8f]>>5)&3)==2); }
    assert(rf[1][0x19]==0&&rf[1][0x05]==0);
    assert(!(ph(0x4738)&(1u<<17))&&!(ph(0x4AA4)&(1u<<17))&&(ph(0x4E30)&0x1FFF)==5180&&(ph(0x4974)&0x7F)==51&&((ph(0x0734)>>16)&0xFF)==0x20&&(ph(0x45BC)&0xFFFFFF)==0);
    assert(((ph(0xC0D4)>>26)&3)==3&&((ph(0xC1D8)>>5)&0xF)==0xF&&((ph(0xC0E4)>>4)&3)==2&&((ph(0x0C9C)>>12)&0xFF)==0xFF&&!(ph(0x4E30)&(1u<<26)));
    assert(!(ph(0x0700)&(1u<<5))&&(ph(0x2320)&1)&&(ph(0x0C80)>>31)&&!(ph(0x42C4)&(1u<<23))&&!(ph(0x4C14)&0x1000)&&(ph(0x49E8)&0x80)&&(ph(0x46C0)&1)&&!(ph(0x40F8)&1));
    assert(!(ph(0x4738)&(1u<<19))&&((ph(0x476C)>>24)&0xFF)==0x1a&&(ph(0x4778)&0xFF)==0x2a&&((ph(0x0980)>>17)&0xF)==0xF&&((ph(0x0980)>>26)&0xF)==4&&!(ph(0x4978)&(1u<<14)));
    assert((ph(0x58DC)>>30)==3&&(ph(0x78DC)>>30)==3&&!(ph(0x5818)&(1u<<30))&&!(ph(0x7820)>>31)&&(ph(0x0000)>>31)&&((ph(0x20fc)>>24)&0xFF)==0&&(ph(0x0704)&2)&&!(ph(0x0C3C)&(1u<<9))&&((ph(0x1200)>>28)&7)==0);
    assert(g32(0xD800)==0x19999980u&&g32(0xD808)==0x01209333u&&g32(0xD820)==0x3939u&&g32(0xD824)==0x7C0u&&g32(0xDC00)==0&&(regs[0xC010]&3)==0&&(regs[0xC628]&0x13)==3);
    reset_hw(); p32(0x10000+0x2320,1); assert(rtw8852c_set_channel(&sc,6)==0); u32 e6=(0xFFFFFu&~(0x30000u|0x300u|0xFFu|0xC00u))|6u|(3u<<10);
    for(int p=0;p<2;p++)assert((rf[p][0x18]&0xFFFFF)==e6&&rfv1(p,0x18)==e6);
    assert((ph(0x4738)>>17&1)&&(ph(0x4A1C)&0x7FFFF)==0x20399&&(ph(0x4A20)&0x7FFFF)==0x2c4f2&&(ph(0x45BC)&0xFFFFFF)==0x3d23ff&&(ph(0x45C8)&0xFFFFFF)==0xffdff5&&(ph(0x4E30)&0x1FFF)==2437);
    assert((ph(0x0700)>>5&1)&&!(ph(0x2320)&1)&&!(ph(0x0C80)>>31)&&(ph(0x42AC)&0x7FF)==38&&(ph(0x42C4)>>23&1)&&(ph(0x4C14)&0xFF)==41&&((ph(0x4C14)>>10)&3)==2&&(ph(0x4C14)&0x1000)&&(ph(0x4CD8)&0xFF)==41&&!(ph(0x4C20)&0x1000));
    assert((ph(0x4738)>>19&1)&&(ph(0x4AA4)>>22&1)&&((ph(0x476C)>>24)&0xFF)==0x20&&((ph(0x0980)>>17)&0xF)==0&&(ph(0x4978)>>14&1)&&((ph(0x4974)>>22)&0xF)==2&&(ph(0x441C)>>31)&&(ph(0x0C6C)>>21&1)&&(regs[0xC628]&0x13)==0x10);
    reset_hw(); assert(rtw8852c_set_channel(&sc,14)==0&&(ph(0x45BC)&0xFFFFFF)==0x3b13ff&&(ph(0x4A1C)&0x7FFFF)==0x20d8a&&(ph(0x4E30)&0x1FFF)==2484&&!(ph(0x42C4)&(1u<<23)));
    reset_hw(); assert(rtw8852c_set_channel(&sc,153)==0&&(ph(0x42AC)&0x7FF)==1984&&(ph(0x4C14)&0xFF)==16&&(ph(0x4CD8)&0x1000));
    reset_hw(); sc.cv=0; rf[0][0x18]=0x12345; assert(rtw8852c_set_channel(&sc,40)==0&&(rf[1][0x05]&1)==1&&((rf[1][0x19]>>4)&3)==3&&rf[1][0x18]==rf[0][0x18]); sc.cv=1;
    reset_hw(); swsi_dead=1; sc.err=0; assert(rtw8852c_set_channel(&sc,6)==-1&&!g_r89ch.ok&&strstr(sc.err,"RF register")&&dl<2000&&((ph(0x20fc)>>24)&0xFF)==0);
    reset_hw(); sc.err=0; sc.state=RTW89_ST_READY; sc.phy_ok=1; assert(rtw89_set_channel(&sc,11)==0&&g_r89ch.ch==11); unsigned long w=nwr; assert(rtw89_set_channel(&sc,11)==0&&nwr==w);
    sc.chip=3; g_r89ch.ok=0; reset_hw(); assert(rtw89_set_channel(&sc,6)==-1&&nwr==0); sc.chip=RTW89_CHIP_8852C;
    reset_hw(); rtw8852c_bb_sethw(&sc);
    assert((ph(0x0A10)&1)&&(ph(0x2A10)&1)&&ph(0x5880)==0x77777777u&&ph(0x7884)==0x77777777u&&ph(0x0334)==0xFFFFFFFFu&&(ph(0x5868)&0xFFFF)==2&&(ph(0x5868)>>16)==0xA9A9u&&ph(0x7868)==ph(0x5868)&&ph(0x5894)==0x080400u&&ph(0x7894)==0x080400u);
    if(!fn){ printf("channel c: RTL8852C set_channel 2.4/5 GHz 20 MHz without gain table\n"); return; }
    FILE* fp=fopen(fn,"rb"); assert(fp); static u8 b[400000]; u32 n=(u32)fread(b,1,sizeof b,fp); fclose(fp);
    struct rtw89_phy_tbls tb; assert(rtw89_phy_tables_parse(b,n,&tb)==0&&tb.nt==5); const struct rtw89_phy_tbl* gt=0; for(u32 i=0;i<tb.nt;i++)if(tb.t[i].kind==RTW89_TBL_BB_GAIN)gt=&tb.t[i]; assert(gt&&gt->n==662);
    nwr=0; assert(rtw89_phy_init_reg(&sc,gt,0xFF,0,-2)>0&&nwr==0&&rtw89_gain.n>0);
    const struct rtw89_bb_gain* g=&rtw89_gain;
    assert((u8)g->lna[1][0][0]==0xC9&&(u8)g->lnab[0][0][0]==0xA0&&(u8)g->lnab[0][0][3]==0x02&&(u8)g->lnab[1][0][0]==0xAF&&(u8)g->lnab[1][0][3]==0x05);
    assert(g->op1[1][0][0]==0x0A&&(u8)g->op1[1][0][1]==0xFB&&g->op1[1][0][3]==0x0E&&g->op1[1][0][4]==0x06&&g->op1[1][0][6]==0x10&&g->tiaop1[1][0][0]==0x33&&g->tiaop1[1][0][3]==0x34&&g->tiaop1[1][0][4]==0x3C&&g->tiaop1[1][0][7]==0x34);
    reset_hw(); assert(rtw8852c_set_channel(&sc,36)==0);
    assert(((ph(0x45DC)>>16)&0xFF)==0xC9&&(ph(0x4BB4)&0xFF)==0xAF&&((ph(0x4BB4)>>24)&0xFF)==0x05&&(ph(0x4668)&0xFF)==0x0A&&((ph(0x466C)>>16)&0xFF)==0x10&&(ph(0x4670)&0xFF)==0x33&&(ph(0x4674)>>24)==0x34&&((ph(0x4674)&0xFF)==0x3C));
    assert((ph(0x474C)&0xFF)==(u8)g->op1[1][1][0]&&(ph(0x4758)>>24)==(u8)g->tiaop1[1][1][7]&&(ph(0x4C78)&0xFF)==(u8)g->lnab[1][1][0]&&((ph(0x4664)>>16)&0xFF)==(u8)g->tia[1][0][0]);
    reset_hw(); assert(rtw8852c_set_channel(&sc,6)==0&&(ph(0x4BB8)>>24)==0xA0&&(ph(0x4BBC)&0xFF)==(u8)g->lnab[0][0][1]&&ph(0x4668)==0&&((ph(0x4678)>>16)&0xFF)==(u8)g->lna[0][0][0]);
    memset(&rtw89_gain,0,sizeof rtw89_gain);
    printf("channel c: RTL8852C set_channel 2.4/5 GHz 20 MHz (MAC RFMOD/TXRATE_CHK, gain error + bypass + op1dB from the 8852C gain table, band sel, FC0/SCO, CCK FIR, chan idx, BW/ADC/EDCCA, CCK en, spur CSI/NBI notch, BTG, 5M mask, TXPW reset, MUIC, TX path, BB reset, RF 0x18 ch+bw, CAV path B copy, RXBB/TIA BW, help enter/leave), RF read timeout fails closed, unknown chip refused, BB GPIO/RFE sethw\n"); }

static const u8 AP[6]={0x02,0x11,0x22,0x33,0x44,0x55}, ME[6]={0x00,0xE0,0x4C,0x88,0x52,0xB0};
static u8 q[6][64]; static int ql[6], qn; static u8 cq[4][40]; static int cl[4], cn;
static void ap_frame(u8 fc,const u8* body,int bl){ if(qn>=6)return; u8* f=q[qn]; memset(f,0,64); f[0]=fc; memcpy(f+4,fc==0x80?(const u8*)"\xff\xff\xff\xff\xff\xff":ME,6); memcpy(f+10,AP,6); memcpy(f+16,AP,6); memcpy(f+24,body,bl); ql[qn++]=24+bl; }
static void q_end(void){ u8* c=cq[cn]; memset(c,0,40); c[0]=1|(1<<2); c[1]=9; c[4]=36; c[10]=RTW89_SCAN_END_SCAN_NOTIFY; cl[cn++]=36; }
static int c2h_rx(struct rtw89_softc* sc,u8* buf,u32 cap){ (void)sc; for(int i=0;i<qn;i++)rtw89_rx_frame(q[i],ql[i],-40); qn=0;
    if(cn){ cn--; memcpy(buf,cq[cn],cl[cn]<(int)cap?(u32)cl[cn]:cap); return cl[cn]; } return 0; }
static const struct rtw89_hci_ops HCI={ 0,c2h_rx,0 };
static int txn, txdata; static u8 txrate;
static int tx_hook(struct rtw89_softc* sc,const u8* f,u32 len,int mgmt,u8 rate,u8 macid){ (void)sc;(void)len; assert(macid==0); txn++;
    if(f[0]==0xB0){ u8 b[6]={0,0,2,0,0,0}; ap_frame(0xB0,b,6); }
    if(f[0]==0x00){ u8 b[6]={0x11,0,0,0,1,0xC0}; ap_frame(0x10,b,6); }
    if(!mgmt){ txdata++; txrate=rate; } return 0; }
static int count(int from,u8 cls,u8 func){ int n=0; for(int i=from;i<hn&&i<64;i++) if(hl[i].cls==cls&&hl[i].func==func)n++; return n; }

static void test_e2e(int chip){
    acks=whw_n=whw_data=txn=txdata=qn=cn=0;
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=chip; sc.cv=1; memcpy(sc.mac,ME,6); sc.state=RTW89_ST_READY; sc.phy_ok=1; reset_hw();
    rtw89_hci=&HCI; rtw89_tx_hook=tx_hook; rtw89_chan_hook=rtw89_set_channel; rtw89_scan_pre_hook=rtw89_sta_vif_init; memset(&g_r89s,0,sizeof g_r89s);
    u8 ch=36; hn=0; assert(rtw89_scan_begin(&sc,&ch,1)==0&&g_r89scan.active&&g_r89s.vif==1&&acks==1);
    int cc=chip==RTW89_CHIP_8852C; assert(hn==5-cc&&hl[0].cls==8&&hl[0].func==4&&hl[1].cls==6&&(cc||(hl[2].cls==5&&hl[2].func==2))&&hl[3-cc].func==0x16&&hl[4-cc].func==0x17);
    u8 bcn[12]={0}; ap_frame(0x80,bcn,12); q_end(); rtw89_scan_poll(&sc,8); assert(!g_r89scan.active&&whw_n==1);
    hn=0; assert(rtw89_scan_begin(&sc,&ch,1)==0&&hn==2&&count(0,8,4)==0); q_end(); rtw89_scan_poll(&sc,8); assert(!g_r89scan.active);
    hn=0; assert(rtw89_sta_join(&sc,AP,36,"Home",2,0)==0&&g_r89s.state==R89S_UP&&g_r89s.aid==1&&g_r89ch.ch==36&&(rf[0][0x18]&0xFF)==36);
    assert(count(0,8,4)==0&&count(0,6,0)==2&&count(0,8,0)==1&&count(0,5,2)==0);
    u8 d[40]={0x08,0x01}; memcpy(d+4,AP,6); memcpy(d+10,ME,6); memcpy(d+16,AP,6);
    assert(rtw89_sta_tx(&sc,d,40)==0&&txdata==1&&txrate==RTW89_HW_RATE_OFDM6);
    u8 rxd[8]={0xAA,0xAA,3,0,0,0,0x88,0x8E}; ap_frame(0x08,rxd,8); q[qn-1][1]=2; rtw89_scan_poll(&sc,4); assert(whw_data==1);
    u8 key[16]={1}; hn=0; assert(rtw89_sta_set_key(&sc,0,WIFI_KEY_CCMP,key,16,1)==0&&hn==0);
    hn=0; assert(rtw89_scan_begin(&sc,&ch,1)==0&&hn==2&&count(0,6,0)==0); q_end(); rtw89_scan_poll(&sc,8);
    hn=0; rtw89_sta_leave(&sc); assert(g_r89s.state==R89S_IDLE&&!g_r89s.vif&&count(0,8,4)==1);
    hn=0; assert(rtw89_scan_begin(&sc,&ch,1)==0&&count(0,8,4)==1&&hn==5-cc); q_end(); rtw89_scan_poll(&sc,8);
    rtw89_hci=0; rtw89_chan_hook=0; rtw89_scan_pre_hook=0;
    printf("e2e %s: role/MACID/CAM/CMAC H2C before first scan, add-channel ack between scan H2Cs, beacon to wifi_hw, join with real channel switch, data TX (OFDM6) and RX, software CCMP keys, scan while connected keeps CAM, leave removes role\n",chip==RTW89_CHIP_8852C?"RTL8852C":"RTL8852B"); }

static void test_gain(const char* fn){
    FILE* f=fopen(fn,"rb"); assert(f); static u8 b[200000]; u32 n=(u32)fread(b,1,sizeof b,f); fclose(f);
    struct rtw89_phy_tbls tb; assert(rtw89_phy_tables_parse(b,n,&tb)==0); const struct rtw89_phy_tbl* t=0; for(u32 i=0;i<tb.nt;i++)if(tb.t[i].kind==RTW89_TBL_BB_GAIN)t=&tb.t[i]; assert(t);
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=RTW89_CHIP_8852B; reset_hw(); memset(&rtw89_gain,0,sizeof rtw89_gain);
    assert(rtw8852b_set_channel(&sc,36)==0&&ph(0x45DC)==0&&ph(0x49B0)==0);
    nwr=0; assert(rtw89_phy_init_reg(&sc,t,1,1,-2)==66&&nwr==0&&rtw89_gain.n==66);
    const struct rtw89_bb_gain* g=&rtw89_gain;
    assert(g->lna[0][0][0]==-73&&g->lna[0][0][1]==-35&&g->lna[0][0][3]==24&&g->lna[0][0][4]==54&&g->lna[0][0][6]==111&&g->tia[0][0][0]==49&&g->tia[0][0][1]==79&&g->lna[1][0][0]==(signed char)0x9e);
    assert(g->rpl20[0][0]==-12&&g->rpl40[0][0][0]==-8&&g->rpl40[0][0][2]==-8&&g->rpl40[0][1][0]==0&&g->rpl80[1][0][1]==-24&&g->rpl80[1][0][3]==8&&g->rpl80[1][1][10]==-8&&g->rpl80[0][0][1]==0);
    assert(rtw89_gain_band(1)==0&&rtw89_gain_band(64)==1&&rtw89_gain_band(100)==2&&rtw89_gain_band(165)==3);
    reset_hw(); assert(rtw8852b_set_channel(&sc,36)==0);
    assert(((ph(0x45DC)>>16)&0xFF)==0x9e&&((ph(0x45DC)>>24)&0xFF)==0xc3&&(ph(0x4660)&0xFF)==0xe6&&((ph(0x4664)>>16)&0xFF)==(u8)g->tia[1][0][0]&&((ph(0x4740)>>16)&0xFF)==(u8)g->lna[1][1][0]);
    assert((ph(0x49B0)>>8)==0xF8F8F4u&&ph(0x4A00)==ph(0x49B0)&&ph(0x49B4)==0xF8E8F8F8u&&ph(0x4A04)==ph(0x49B4)&&ph(0x49B8)==0xF80808E8u&&ph(0x4A08)==ph(0x49B8)&&ph(0x4678)==0);
    reset_hw(); assert(rtw8852b_set_channel(&sc,6)==0&&((ph(0x4678)>>16)&0xFF)==0xb7&&(ph(0x467C)&0xFF)==0xfb&&((ph(0x4680)>>24)&0xFF)==0x4f&&((ph(0x475C)>>16)&0xFF)==(u8)g->lna[0][1][0]&&ph(0x45DC)==0);
    memset(&rtw89_gain,0,sizeof rtw89_gain);
    printf("gain: BB gain table parsed (LNA/TIA gain error, RPL offsets 20/40/80 per band/path), written at channel switch for 2.4 GHz / 5 GHz, empty table writes nothing\n"); }
int main(int argc,char** argv){ test_channel(); test_e2e(RTW89_CHIP_8852B); test_e2e(RTW89_CHIP_8852C); if(argc>1)test_gain(argv[1]); test_channel_c(argc>2?argv[2]:0); printf("rtw89e host ok\n"); return 0; }
