/* Host test of drivers/net/wifi/rtw88/chips/rtw8822b.c against a simulated register file and RF.
   Build: see tools/tests/wifi/rtw8822b_test.sh. The chip file is included directly so its statics are reachable.
   The core contract functions (rtw_r8..rtw_w32, rtw_rf_read/write, rtw_h2c_pkt_send, rtw_apply_cond) are
   provided here with rtw88 semantics; rtw_apply_cond walks the tables like rtw_parse_tbl_phy_cond and
   rtw8822b_expect.h (rtw8822b_gen.py) holds the counts computed from the Linux source. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "drivers/net/wifi/rtw88/chips/rtw8822b.c"
#include "drivers/net/wifi/rtw88/chips/rtw8822b_tables.c"
#include "rtw8822b_expect.h"

const unsigned char _binary_rtw8822b_fw_bin_start[4]={0x22,0x88,0,0}, _binary_rtw8822b_fw_bin_end[1]={0};
rtw_efuse_info g_rtw_efuse;

static u8 regs[0x20000];
static u32 rf[2][256];
static u64 waited;
static int nw, nrfw, h2c_n; static u8 h2c_last[32];
static int cut_cfg=3, rfe_cfg=3;
static u32 tbl_n, tbl_ck;
static int log_on; static u32 wlog_off[4096], wlog_val[4096]; static int wlog_n;

static u32 rd(u32 o,int n){ u32 v=0; for(int i=0;i<n;i++)v|=(u32)regs[(o+i)&0x1FFFF]<<(8*i); return v; }
static void hw_write(u32 o,u32 v,int n){
    if(o>=0x20000){ return; }
    for(int i=0;i<n;i++){ u32 a=o+i; u8 b=(u8)(v>>(8*i));
        if(a==0x0005){ if(b&2)regs[0x0100]=0xEA; if(b&1)regs[0x0100]=0x00; b&=(u8)~0x03u; }
        if(a==0x1703)b|=0x20;
        regs[a]=b; }
    nw++;
    if(log_on&&wlog_n<4096){ wlog_off[wlog_n]=o; wlog_val[wlog_n]=v; wlog_n++; }
}
u8 rtw_r8(u32 m,u32 o){ (void)m; return (u8)rd(o,1); }
u16 rtw_r16(u32 m,u32 o){ (void)m; return (u16)rd(o,2); }
u32 rtw_r32(u32 m,u32 o){ (void)m; return rd(o,4); }
void rtw_w8(u32 m,u32 o,u8 v){ (void)m; hw_write(o,v,1); }
void rtw_w16(u32 m,u32 o,u16 v){ (void)m; hw_write(o,v,2); }
void rtw_w32(u32 m,u32 o,u32 v){ (void)m; hw_write(o,v,4); }
void rtw_delay_us(u32 us){ waited+=us; }
static u32 shf(u32 mask){ u32 s=0; while(s<31&&!((mask>>s)&1u))s++; return s; }
u32 rtw_rf_read(u32 m,int path,u32 addr,u32 mask){ (void)m; return (rf[path&1][addr&0xFF]&mask)>>shf(mask); }
void rtw_rf_write(u32 m,int path,u32 addr,u32 mask,u32 val){ (void)m;
    u32* r=&rf[path&1][addr&0xFF]; *r=((*r&~mask)|((val<<shf(mask))&mask))&0xFFFFFu; nrfw++; }
void rtw_bb_write_mask(u32 m,u32 a,u32 mask,u32 v){ (void)m; hw_write(a,(rd(a,4)&~mask)|((v<<shf(mask))&mask),4); }
u32 rtw_bb_read_mask(u32 m,u32 a,u32 mask){ (void)m; return (rd(a,4)&mask)>>shf(mask); }
int g_rtw_iqk_last; static int h2c_fail, iqk_mute, npump;
void ui_pump(void){ npump++; }
int rtw_h2c_pkt_send(u32 m,const u8* pkt,int len){ (void)m; if(h2c_fail)return -1; memcpy(h2c_last,pkt,len>32?32:len); h2c_n++; if(iqk_mute)return 0;
    if(pkt[0]==0x01&&pkt[1]==0xFF&&pkt[2]==0x0E)rf[0][0x08]=0xABCDE;
    return 0; }

static int mdio_n, mdio_g2, mdio_last_a, mdio_last_d;
int rtw_mdio_write(u32 m,u8 addr,u16 data,int g1){ (void)m; mdio_n++; if(!g1)mdio_g2++; mdio_last_a=addr; mdio_last_d=data; return 0; }

static int cond_ok(u32 c){
    u32 cut=(c>>24)&0xFu, pkg=(c>>12)&0xFu, itf=(c>>8)&0xFu, r=c&0xFFu;
    if(cut&&cut!=(u32)cut_cfg)return 0;
    if(pkg&&pkg!=15u)return 0;
    if(itf&&itf!=1u)return 0;
    return r==(u32)rfe_cfg;
}
static int apply_path;
int rtw_apply_cond(u32 m,const unsigned int* t,int n,u8 sz,const char* lbl,int verify){
    (void)lbl; (void)verify;
    u32 pc=0; int matched=1, skipped=0; tbl_n=0; tbl_ck=0;
    for(int i=0;i<n;i++){
        u32 a=t[i*2], d=t[i*2+1];
        if(a>>31){ u32 br=(a>>28)&3u; if(br==3){ matched=1; skipped=0; } else if(br==2) matched=skipped?0:1; else pc=a; continue; }
        if((a>>30)&1u){ if(!skipped){ if(cond_ok(pc)){ matched=1; skipped=1; } else { matched=0; skipped=0; } } else matched=0; continue; }
        if(!matched)continue;
        tbl_n++; tbl_ck=tbl_ck*31u+a*7u+d;
        if(sz==5){ if(a==0xFFEu||a==0xFEu)continue; rtw_rf_write(m,apply_path,a,0xFFFFF,d); }
        else if(sz==1)rtw_w8(m,a,(u8)d);
        else if(a<0xF9||a>0xFE)rtw_w32(m,a,d);
    }
    return 0;
}

static int bad;
#define CHECK(c,...) do{ if(!(c)){ bad++; printf("FAIL %s:%d: ",__FILE__,__LINE__); printf(__VA_ARGS__); printf("\n"); } }while(0)

static void reset_hw(void){
    memset(regs,0,sizeof regs); memset(rf,0,sizeof rf);
    regs[0x0100]=0xEA; regs[0x0006]=0x02;
    u32 cfg=((u32)cut_cfg<<12)|(1u<<27); regs[0xF0]=(u8)cfg; regs[0xF1]=(u8)(cfg>>8); regs[0xF2]=(u8)(cfg>>16); regs[0xF3]=(u8)(cfg>>24);
    regs[0x1702]=0; regs[0x1703]=0x20;
    regs[0x0C50]=0x20; regs[0x0E50]=0x20;
}

static void mk_efuse(u8* map,int rfe){
    memset(map,0xFF,768);
    map[0]=0x29; map[1]=0x81;
    u8 mac[6]={0x00,0xE0,0x4C,0x22,0x88,0x01}; memcpy(map+0xD0,mac,6);
    map[0xB9]=0x21; map[0xBA]=0x1C; map[0xC3]=0x01; map[0xCA]=(u8)rfe; map[0xCB]='S'; map[0xCC]='E';
    for(int p=0;p<2;p++){ u8* t=map+0x10+42*p;
        for(int g=0;g<6;g++)t[g]=(u8)(0x2A+p);
        for(int g=0;g<5;g++)t[6+g]=(u8)(0x28+p);
        t[11]=0xF2; t[12]=0xEF; t[13]=0xFE;
        for(int g=0;g<14;g++)t[18+g]=(u8)(0x24+g);
        t[32]=0xE1; t[33]=0xFE; t[38]=0x20; t[39]=0x10; }
}

static void test_chip_table(void){
    const rtw_chip* c=&rtw8822b_chip;
    CHECK(!strcmp(c->name,"RTL8822BE")&&c->vendor==0x10EC&&c->device==0xB822&&c->bus==RTW_BUS_PCIE,"identity");
    CHECK(c->n_paths==2&&c->wcpu==RTW_WCPU_3081,"paths/wcpu");
    hw_write(0xF0,(rd(0xF0,4)&~0xF000u)|(3u<<12),4); mdio_n=0; CHECK(c->pcie_phy&&c->pcie_phy(0)==1&&mdio_n==0,"PCIe PHY: nothing on cut D");
    hw_write(0xF0,(rd(0xF0,4)&~0xF000u)|(2u<<12),4); mdio_n=mdio_g2=0; CHECK(c->pcie_phy(0)==0&&mdio_n==20&&mdio_g2==10&&mdio_last_a==0x2A&&mdio_last_d==0x3040,"PCIe PHY: cut C gen1+gen2 (%d writes)",mdio_n);
    hw_write(0xF0,0,4);
    CHECK(c->txff_size==262144&&c->rxff_size==24576&&c->txff_pages==2048&&c->rsvd_drv_pages==8,"fifo sizes");
    CHECK(c->tx_desc_sz==48&&c->rx_desc_sz==24&&c->tx_buf_desc_sz==16,"desc sizes");
    CHECK(c->fw_start==_binary_rtw8822b_fw_bin_start&&c->fw_end==_binary_rtw8822b_fw_bin_end&&c->fw_name,"fw");
    CHECK(c->page_table.hq==64&&c->page_table.nq==64&&c->page_table.lq==64&&c->page_table.exq==64&&c->page_table.gapq==1,"page table");
    CHECK(c->rqpn.vo==2&&c->rqpn.vi==2&&c->rqpn.be==1&&c->rqpn.bk==1&&c->rqpn.mg==0&&c->rqpn.hi==3,"rqpn");
    CHECK(c->power_on&&c->power_off&&c->efuse_parse&&c->mac_init&&c->phy_init&&c->set_channel&&c->set_tx_power
          &&c->calibrate&&c->coex_wifi_only&&c->rfe_setup&&c->phy_post,"null op");
    CHECK(c->mac_tbl.t&&c->mac_tbl.n==rtw8822b_cmac_n&&c->mac_tbl.sz==1,"mac tbl");
    CHECK(c->bb_tbl.t&&c->bb_tbl.n==rtw8822b_cbb_n&&c->bb_tbl.sz==4,"bb tbl");
    CHECK(c->agc_tbl.t&&c->agc_tbl.n==rtw8822b_cagc_n&&c->agc_tbl.sz==4,"agc tbl");
    CHECK(c->rf_tbl[0].t==rtw8822b_crf_a&&c->rf_tbl[0].n==rtw8822b_crf_a_n&&c->rf_tbl[1].t==rtw8822b_crf_b&&c->rf_tbl[1].n==rtw8822b_crf_b_n,"rf tbls");
    CHECK(c->rf_base[0]==0x2800&&c->rf_base[1]==0x2C00&&c->rf_sipi[0]==0xC90&&c->rf_sipi[1]==0xE90&&!c->rf_mix,"rf addrs");
}

static void test_tables(void){
    const rtw_chip* c=&rtw8822b_chip;
    const rtw_tbl* t[5]={&c->mac_tbl,&c->agc_tbl,&c->bb_tbl,&c->rf_tbl[0],&c->rf_tbl[1]};
    int done=0;
    for(unsigned i=0;i<sizeof g_exp/sizeof g_exp[0];i++){
        const exp8b* e=&g_exp[i];
        cut_cfg=e->cut; rfe_cfg=e->rfe; reset_hw(); nw=nrfw=0;
        apply_path=e->tbl==4?1:0;
        rtw_apply_cond(0,t[e->tbl]->t,t[e->tbl]->n,t[e->tbl]->sz,"t",0);
        CHECK((int)tbl_n==e->n&&tbl_ck==e->ck,"table %d cut %d rfe %d: %u writes ck %08X, want %d %08X",e->tbl,e->cut,e->rfe,tbl_n,tbl_ck,e->n,e->ck);
        if(e->tbl>=3)CHECK(nrfw>0&&nrfw<=e->n,"table %d issued %d rf writes",e->tbl,nrfw);
        done++;
    }
    cut_cfg=3; rfe_cfg=3; reset_hw(); apply_path=1;
    rtw_apply_cond(0,c->rf_tbl[1].t,c->rf_tbl[1].n,5,"rf-b",0);
    CHECK(rf[1][0x00]!=0||rf[1][0x18]!=0||rf[1][0xEF]!=0,"rf-b table left path B empty");
    printf("  tables: %d table/cut/rfe combinations match the Linux-source walk\n",done);
}

static void test_efuse(void){
    u8 map[768]; rtw_efuse_info e;
    mk_efuse(map,3); memset(&e,0,sizeof e);
    int r=rtw8822b_chip.efuse_parse(map,768,&e);
    CHECK(r==0,"efuse_parse rfe 3 returned %d",r);
    CHECK(e.mac[0]==0x00&&e.mac[1]==0xE0&&e.mac[2]==0x4C&&e.mac[5]==0x01,"mac");
    CHECK(e.rfe_option==3&&e.crystal_cap==0x21&&e.thermal_meter==0x1C&&e.country_code[0]=='S'&&e.country_code[1]=='E',"fields");
    CHECK(e.tx_pwr_idx_2g[0][0]==0x2A&&e.tx_pwr_idx_2g[1][0]==0x2B&&e.tx_pwr_idx_2g[0][6]==0x28&&e.tx_pwr_idx_2g[0][11]==0xF2,"2g pwr");
    CHECK(e.tx_pwr_idx_5g[0][0]==0x24&&e.tx_pwr_idx_5g[0][13]==0x31&&e.tx_pwr_idx_5g[1][10]==0x2E,"5g pwr");
    CHECK(g8b_share_ant==1,"share_ant");
    mk_efuse(map,7); map[0xB9]=0xFF;
    CHECK(rtw8822b_chip.efuse_parse(map,768,&e)==-2&&e.crystal_cap==0,"unsupported rfe / xtal 0xff");
    CHECK(rtw8822b_chip.efuse_parse(map,0x80,&e)==-1,"short map");
}

static void bring_up(int rfe){
    u8 map[768]; cut_cfg=3; rfe_cfg=rfe; reset_hw(); mk_efuse(map,rfe);
    memset(&g_rtw_efuse,0,sizeof g_rtw_efuse); rtw8822b_chip.efuse_parse(map,768,&g_rtw_efuse);
    rf[0][0x18]=0xFFFFF; rf[1][0x18]=0xFFFFF;
}

static void test_power(void){
    cut_cfg=2; reset_hw(); log_on=1; wlog_n=0;
    int r=rtw8822b_chip.power_on(0);
    CHECK(r==0,"power_on returned %d",r);
    int saw10a9=0; for(int i=0;i<wlog_n;i++)if(wlog_off[i]==0x10A9&&wlog_val[i]==0xEF)saw10a9=1;
    CHECK(saw10a9,"cut C: 0x10A9=0xEF missing");
    CHECK(regs[0x0300]==0&&regs[0x0301]==0,"PCIe 0x300/0x301");
    CHECK((regs[0x0029])==0xF9&&(regs[0x00AF]&0x20)&&(regs[0x0074]&0x20),"act regs");
    CHECK(regs[0xFF1A]==0&&regs[0x10C3]==0,"USB-only entries applied on PCIe");
    cut_cfg=3; reset_hw(); wlog_n=0;
    rtw8822b_chip.power_on(0);
    saw10a9=0; for(int i=0;i<wlog_n;i++)if(wlog_off[i]==0x10A9)saw10a9=1;
    CHECK(!saw10a9,"cut D must skip the cut C entries");
    regs[0x0100]=0x00; regs[0x0006]=0x02; wlog_n=0;
    r=rtw8822b_chip.power_on(0);
    CHECK(r==0,"power_on (already on) returned %d",r);
    int saw93=0; for(int i=0;i<wlog_n;i++)if(wlog_off[i]==0x0093)saw93=1;
    CHECK(saw93,"already-on: no power-off cycle first");
    log_on=0;
}

static const struct { u8 ch, bw; u32 rf18; u32 rfbe; u8 sc; } chs[] = {
    {1,RTW_BW_20,0x8FC01,0,0x00}, {6,RTW_BW_20,0x8FC06,0,0x00}, {11,RTW_BW_20,0x8FC0B,0,0x00},
    {36,RTW_BW_20,0x9FD24,7,0x00}, {149,RTW_BW_20,0xDFD95,5,0x00},
    {36,RTW_BW_40,0x9F926,6,0x02}, {36,RTW_BW_80,0x9F52A,5,0xA4}, {157,RTW_BW_80,0xDF59B,7,0x91},
    {1,RTW_BW_40,0x8F803,0,0x02}, {11,RTW_BW_40,0x8F809,0,0x01},
};
static void test_channel(void){
    for(unsigned i=0;i<sizeof chs/sizeof chs[0];i++){
        bring_up(3); rtw8822b_chip.phy_init(0);
        rtw8822b_chip.set_channel(0,chs[i].ch,chs[i].bw);
        CHECK(rf[0][0x18]==chs[i].rf18,"ch %u bw %u: RF18 A 0x%05X want 0x%05X",chs[i].ch,chs[i].bw,rf[0][0x18],chs[i].rf18);
        CHECK(rf[1][0x18]==chs[i].rf18,"ch %u: RF18 B 0x%05X",chs[i].ch,rf[1][0x18]);
        CHECK(((rf[0][0xBE]>>15)&7)==chs[i].rfbe,"ch %u: RF 0xBE band %u want %u",chs[i].ch,(rf[0][0xBE]>>15)&7,chs[i].rfbe);
        CHECK(regs[0x0483]==chs[i].sc,"ch %u bw %u: 0x483=0x%02X want 0x%02X",chs[i].ch,chs[i].bw,regs[0x0483],chs[i].sc);
        u32 mod=rd(0x0668,4)&0x180;
        CHECK(mod==(chs[i].bw==RTW_BW_80?0x100u:chs[i].bw==RTW_BW_40?0x80u:0),"ch %u: RFMOD 0x%X",chs[i].ch,mod);
        CHECK((rd(0x08AC,4)&3)==chs[i].bw,"ch %u: 0x8AC bw",chs[i].ch);
        int g5=chs[i].ch>14;
        CHECK(((regs[0x0454]>>7)&1)==g5,"ch %u: CCK check",chs[i].ch);
        CHECK((rd(0xCB0,4)&0xFFFFFF)==(g5?0x477547u:0x745774u)&&(rd(0xEB0,4)&0xFFFFFF)==(g5?0x477547u:0x745774u),"ch %u: ifem RFE sel",chs[i].ch);
        CHECK((rd(0xCA0,4)&0xFFFF)==(g5?0xA5A5u:0xA501u),"ch %u: TRSW",chs[i].ch);
        CHECK((rf[0][0xB8]>>19)&1,"ch %u: XTALX2 toggle",chs[i].ch);
    }
    bring_up(2); rtw8822b_chip.set_channel(0,100,RTW_BW_20);
    CHECK((rd(0xCB0,4)&0xFFFFFF)==0x177517&&(rd(0x083C,4))==0x9194b2b9,"efem 5g rfe 2");
    CHECK(rd(0x0838,4)==(0x87766431u&~0xF0u)+0x40u,"rfe 2 ch100 bw20: 0x838 0x%08X",rd(0x0838,4));
    bring_up(2); rtw8822b_chip.set_channel(0,144,RTW_BW_20);
    CHECK((rf[0][0xDF]>>18)&1,"ch 144: RF 0xDF[18]");
    bring_up(9); rf[0][0x18]=0x12345; rtw8822b_chip.set_channel(0,6,RTW_BW_20);
    CHECK(rf[0][0x18]==0x12345,"unsupported rfe must not program the channel");
}

static void test_txpower(void){
    bring_up(3); rtw8822b_chip.set_channel(0,1,RTW_BW_20);
    CHECK(rd(0x1D00,4)==0x2A2A2A2A,"cck A 0x%08X",rd(0x1D00,4));
    CHECK(rd(0x1D04,4)==0x2A2A2A2A,"ofdm A 0x%08X",rd(0x1D04,4));
    CHECK(rd(0x1D0C,4)==0x27272727,"ht1s A 0x%08X",rd(0x1D0C,4));
    CHECK(rd(0x1D14,4)==0x26262626,"ht2s A 0x%08X",rd(0x1D14,4));
    CHECK(rd(0x1D80,4)==0x2B2B2B2B&&rd(0x1D8C,4)==0x28282828,"path B 0x%08X 0x%08X",rd(0x1D80,4),rd(0x1D8C,4));
    bring_up(3); rtw8822b_chip.set_channel(0,36,RTW_BW_20);
    CHECK(rd(0x1D00,4)==0,"5g must not write cck");
    CHECK(rd(0x1D04,4)==0x25252525,"5g ofdm 0x%08X",rd(0x1D04,4));
    CHECK(rd(0x1D0C,4)==0x22222222,"5g ht1s 0x%08X",rd(0x1D0C,4));
    bring_up(3); rtw8822b_chip.set_channel(0,36,RTW_BW_80);
    CHECK(rd(0x1D2C,4)==0x26262626,"5g vht1s bw80 0x%08X",rd(0x1D2C,4));
    for(u32 a=0x1D00;a<0x1D40;a+=4){ u32 v=rd(a,4); for(int k=0;k<4;k++)CHECK(((v>>(8*k))&0xFF)<=0x3F,"index above 0x3f at 0x%X",a); }
}

static void test_phy_iqk_coex(void){
    bring_up(3); rtw8822b_chip.phy_init(0);
    CHECK((regs[0x0002]&3)==3&&(regs[0x001F]&7)==7&&(rd(0x00EC,4)&0x07000000)==0x07000000&&!(rd(0x0808,4)&0x30000000),"phy_init");
    rtw8822b_chip.mac_init(0);
    CHECK(rd(0x0608,4)==0xE400220E&&regs[0x060C]==24&&rd(0x07D8,4)==0xB0810041&&regs[0x07D4]==0x98&&rd(0x0514,4)==0x10100E0A,"mac_init");
    rtw8822b_chip.phy_post(0);
    CHECK(((rd(0x0024,4)>>25)&0x3F)==0x21&&((rd(0x0028,4)>>1)&0x3F)==0x21,"crystal cap");
    CHECK((rd(0x0808,4)&0x30000000)==0x30000000&&(rd(0x0808,4)&0xFF)==0x33,"rx path / psel");
    CHECK(rf[0][0x3F]==0x4080C&&rf[0][0x3E]==0x34&&rf[0][0xEF]==0,"RF mode table");
    CHECK(rd(0x1C94,4)==0xAFFFAFFF&&regs[0x08A4]==0x7F&&regs[0x08A5]==0x7F,"bf/edcca");
    CHECK((rd(0x0C08,4)&0xFFFF)==0x3231&&(rd(0x0E08,4)&0xFFFF)==0x3231,"AGC tr");
    u64 w0=waited; h2c_n=0;
    rtw8822b_chip.calibrate(0);
    CHECK(h2c_n==1&&h2c_last[0]==0x01&&h2c_last[1]==0xFF&&h2c_last[2]==0x0E&&h2c_last[4]==9&&h2c_last[8]==0,"IQK H2C");
    CHECK(rtw8822b_iqk_rf08==0xABCDE&&rf[0][0x08]==0,"IQK poll/clear");
    CHECK(waited-w0<20000,"IQK should finish on first poll");
    CHECK(g_rtw_iqk_last>0,"IQK result %d",g_rtw_iqk_last);
    h2c_fail=1; w0=waited; rtw8822b_chip.calibrate(0); h2c_fail=0; CHECK(g_rtw_iqk_last==-1&&waited-w0<1000,"IQK send failure: %d",g_rtw_iqk_last);
    iqk_mute=1; rf[0][0x08]=0; w0=waited; npump=0; rtw8822b_chip.calibrate(0); iqk_mute=0;
    CHECK(g_rtw_iqk_last==-2&&waited-w0>=5000000&&npump>=70,"IQK timeout: %d, %d UI pumps",g_rtw_iqk_last,npump);
    rf[0][0x08]=0; w0=waited;
    rtw8822b_iqk_rf08=0;
    bring_up(3); rtw8822b_chip.coex_wifi_only(0);
    CHECK(rd(0x00AA,2)==0x8003,"scbd 0x%04X",rd(0x00AA,2));
    CHECK(regs[0x0CB4]==0x77&&(regs[0x0CBD]&3)==2&&(regs[0x004F]&1)&&!(regs[0x004E]&0x80)&&(regs[0x0073]&4),"ant switch");
    CHECK(rd(0x06C0,4)==0x55555555&&rd(0x06C8,4)==0xF0FFFFFF,"coex table");
    CHECK((regs[0x06CC]&0x18)==0x18&&(regs[0x06CF]&8),"wl pri");
    g8b_scbd=0; bring_up(2); rtw8822b_chip.coex_wifi_only(0);
    CHECK(rd(0x00AA,2)==0x8103,"rfe 2 scbd extfem 0x%04X",rd(0x00AA,2));
    bring_up(2); rtw8822b_chip.rfe_setup(0,1);
    CHECK((rd(0xCB0,4)&0xFFFFFF)==0x177517&&(rd(0xEB0,4)&0xFFFFFF)==0x177517,"rfe_setup 5g efem");
    rtw8822b_chip.rfe_setup(0,0);
    CHECK((rd(0xCB0,4)&0xFFFFFF)==0x705770,"rfe_setup 2g efem");
    rtw8822b_chip.power_off(0);
}

int main(void){
    test_chip_table();
    test_tables();
    test_efuse();
    test_power();
    test_channel();
    test_txpower();
    test_phy_iqk_coex();
    if(bad){ printf("rtw8822b: %d check(s) FAILED\n",bad); return 1; }
    printf("rtw8822b: all checks passed\n");
    return 0;
}
