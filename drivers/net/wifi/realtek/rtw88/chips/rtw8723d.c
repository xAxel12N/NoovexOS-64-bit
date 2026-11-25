// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2018-2019  Realtek Corporation
 * Copyright 2024 Fiona Klute (rtw8723x.c)
 * RTL8723DE chip file for NoovexOS, ported from Linux rtw88 rtw8723d.c, rtw8723x.c and the legacy (8051)
 * firmware download in mac.c, taken under BSD-3-Clause.
 */

#include "include/nvx/base.h"
#include "include/nvx/drivers/net/wifi/rtw_chip.h"
#include "include/nvx/drivers/net/wifi/rtw_efuse.h"
#include "include/nvx/drivers/net/wifi/rtw8821ce.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8723d_tables.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8723d_txpwr_tbl.h"
#include "drivers/net/wifi/realtek/rtw88/chips/rtw8723d.h"
#include "include/nvx/drivers/net/wifi/rtw_ra.h"

extern const unsigned char _binary_rtw8723d_fw_bin_start[], _binary_rtw8723d_fw_bin_end[];

#define BIT(n) (1u<<(n))

static u8 g8d_txpwr[0x2A];
static u8 g8d_afe, g8d_bt, g8d_bw, g8d_cc;
u32 rtw8723d_lck_fail, rtw8723d_fw_fail;

static u32 sh_of(u32 mask){ u32 s=0; while(s<31&&!((mask>>s)&1u))s++; return s; }
static void w32m(u32 m,u32 r,u32 mask,u32 v){ u32 s=sh_of(mask); rtw_w32(m,r,(rtw_r32(m,r)&~mask)|((v<<s)&mask)); }
static void w8set(u32 m,u32 r,u8 b){ rtw_w8(m,r,(u8)(rtw_r8(m,r)|b)); }
static void w8clr(u32 m,u32 r,u8 b){ rtw_w8(m,r,(u8)(rtw_r8(m,r)&~b)); }
static void w16set(u32 m,u32 r,u16 b){ rtw_w16(m,r,(u16)(rtw_r16(m,r)|b)); }
static void w32set(u32 m,u32 r,u32 b){ rtw_w32(m,r,rtw_r32(m,r)|b); }
static void w32clr(u32 m,u32 r,u32 b){ rtw_w32(m,r,rtw_r32(m,r)&~b); }

u32 rtw8723d_rf_read(u32 m,int path,u32 addr){
    static const u32 h1[2]={0x820,0x828}, h2[2]={0x824,0x82C}, rd[2]={0x8A0,0x8A4}, rdpi[2]={0x8B8,0x8BC};
    if(path<0||path>1)return 0xFFFFFFFFu;
    u32 v=rtw_r32(m,h2[path]); rtw_w32(m,h2[path],(v&~0x7F800000u)|((addr&0xFFu)<<23));
    v=rtw_r32(m,h2[0]); rtw_w32(m,h2[0],v&~0x80000000u); rtw_w32(m,h2[0],v|0x80000000u);
    rtw_delay_us(120);
    u32 r=(rtw_r32(m,h1[path])&BIT(8))?rdpi[path]:rd[path];
    return rtw_r32(m,r)&0x000FFFFFu;
}

static const rtw_pwr_cmd carddis_to_cardemu[]={ {0x0005,0x88,0x00,RTW_PWR_WRITE}, {0x0005,0x18,0x00,RTW_PWR_WRITE}, {0x0301,0xFF,0x00,RTW_PWR_WRITE} };
static const rtw_pwr_cmd cardemu_to_act[]={ {0x0005,0x1C,0x00,RTW_PWR_WRITE}, {0x0075,0x01,0x01,RTW_PWR_WRITE}, {0x0006,0x02,0x02,RTW_PWR_POLL}, {0x0075,0x01,0x00,RTW_PWR_WRITE}, {0x0006,0x01,0x01,RTW_PWR_WRITE}, {0x0005,0x03,0x00,RTW_PWR_POLL}, {0x0005,0x80,0x00,RTW_PWR_WRITE}, {0x0005,0x18,0x00,RTW_PWR_WRITE}, {0x0005,0x01,0x01,RTW_PWR_WRITE}, {0x0005,0x01,0x00,RTW_PWR_POLL}, {0x0010,0x40,0x40,RTW_PWR_WRITE}, {0x0049,0x02,0x02,RTW_PWR_WRITE}, {0x0063,0x02,0x02,RTW_PWR_WRITE}, {0x0062,0x02,0x00,RTW_PWR_WRITE}, {0x0058,0x01,0x01,RTW_PWR_WRITE}, {0x005A,0x02,0x02,RTW_PWR_WRITE}, {0x0069,0x40,0x40,RTW_PWR_WRITE}, {0x001F,0xFF,0x00,RTW_PWR_WRITE}, {0x0077,0xFF,0x00,RTW_PWR_WRITE}, {0x001F,0xFF,0x07,RTW_PWR_WRITE}, {0x0077,0xFF,0x07,RTW_PWR_WRITE} };
static const rtw_pwr_cmd act_to_lps[]={ {0x0301,0xFF,0xFF,RTW_PWR_WRITE}, {0x0522,0xFF,0xFF,RTW_PWR_WRITE}, {0x05F8,0xFF,0x00,RTW_PWR_POLL}, {0x05F9,0xFF,0x00,RTW_PWR_POLL}, {0x05FA,0xFF,0x00,RTW_PWR_POLL}, {0x05FB,0xFF,0x00,RTW_PWR_POLL}, {0x0002,0x01,0x00,RTW_PWR_WRITE}, {0x0002,0x00,0x00,RTW_PWR_DELAY}, {0x0002,0x02,0x00,RTW_PWR_WRITE}, {0x0100,0xFF,0x03,RTW_PWR_WRITE}, {0x0101,0x02,0x00,RTW_PWR_WRITE}, {0x0553,0x20,0x20,RTW_PWR_WRITE} };
static const rtw_pwr_cmd act_to_pre_carddis[]={ {0x0003,0x04,0x00,RTW_PWR_WRITE}, {0x0080,0xFF,0x00,RTW_PWR_WRITE} };
static const rtw_pwr_cmd act_to_cardemu[]={ {0x0002,0x01,0x00,RTW_PWR_WRITE}, {0x0049,0x02,0x00,RTW_PWR_WRITE}, {0x0006,0x01,0x01,RTW_PWR_WRITE}, {0x0005,0x02,0x02,RTW_PWR_WRITE}, {0x0005,0x02,0x00,RTW_PWR_POLL}, {0x0010,0x40,0x00,RTW_PWR_WRITE} };
static const rtw_pwr_cmd cardemu_to_carddis[]={ {0x0005,0x04,0x04,RTW_PWR_WRITE}, {0x0005,0x18,0x18,RTW_PWR_WRITE} };
static const rtw_pwr_cmd act_to_post_carddis[]={ {0x001D,0x01,0x00,RTW_PWR_WRITE}, {0x001D,0x01,0x01,RTW_PWR_WRITE}, {0x001C,0xFF,0x0E,RTW_PWR_WRITE} };
#define NSEQ(a) ((int)(sizeof(a)/sizeof(a[0])))

int wifi_feat_begin(int f); void wifi_feat_ok(int f); void wifi_feat_fail(int f,const char* why);
extern int g_h2c_box;
u32 rtw8723d_trx_fail;
static void rtw8723d_pre_cfg(u32 m){
    rtw_w8(m,0x001C,0);
    rtw_w8(m,0x007C,(rtw_r32(m,0x00F0)&BIT(24))?0xC3:0x83);
}
static void rtw8723d_power_off(u32 m);
static int rtw8723d_power_on(u32 m){
    wifi_feat_begin(17);
    rtw8723d_pre_cfg(m);
    if(rtw_r8(m,0x0100)!=0xEA){ rtw8723d_power_off(m); rtw8723d_pre_cfg(m); }
    int f=rtw_pwr_seq_run(m,carddis_to_cardemu,NSEQ(carddis_to_cardemu));
    f+=rtw_pwr_seq_run(m,cardemu_to_act,NSEQ(cardemu_to_act));
    rtw_w8(m,0x0100,0xFF); rtw_delay_us(2000);
    rtw_w8(m,0x0423,0x7F); rtw_delay_us(2000);
    w8set(m,0x0008,(u8)BIT(3));
    rtw_w16(m,0x0040,(u16)(rtw_r16(m,0x0040)&~BIT(12)));
    rtw_w16(m,0x0100,0x02FF);
    return f;
}

int rtw8723d_trx_init(u32 m){
    const rtw_chip* c=&rtw8723d_chip; const rtw_rqpn* q=&c->rqpn; const rtw_page_table* t=&c->page_table;
    u16 map=(u16)(((u32)q->vo<<4)|((u32)q->vi<<6)|((u32)q->be<<8)|((u32)q->bk<<10)|((u32)q->mg<<12)|((u32)q->hi<<14));
    rtw_w16(m,0x010C,map);
    rtw_w8(m,0x0100,0); rtw_w8(m,0x0100,0xFF);
    u32 pages=c->txff_size>>7, bnd=pages-c->rsvd_drv_pages;
    u32 pub=bnd-t->hq-t->lq-t->nq-t->exq-t->gapq;
    rtw_w32(m,0x0214,(t->nq&0xFFu)|((u32)(t->exq&0xFFu)<<16));
    rtw_w32(m,0x0200,BIT(31)|(t->hq&0xFFu)|((u32)(t->lq&0xFFu)<<8)|((pub&0xFFu)<<16));
    rtw_w8(m,0x0114,(u8)bnd);
    rtw_w16(m,0x0116,(u16)(c->rxff_size-128u-1u));
    rtw_w8(m,0x0209,(u8)bnd); rtw_w8(m,0x0424,(u8)bnd); rtw_w8(m,0x0425,(u8)bnd); rtw_w8(m,0x045D,(u8)bnd);
    w32set(m,0x0224,BIT(16));
    int ok=0; for(int i=0;i<1000;i++){ if(!(rtw_r32(m,0x0224)&BIT(16))){ ok=1; break; } rtw_delay_us(10); }
    if(!ok){ rtw8723d_trx_fail++; wifi_feat_fail(17,"page setup (auto LLT) did not finish"); return -1; }
    rtw_w8(m,0x060F,4);
    w32set(m,0x0608,BIT(28));
    w32clr(m,0x07D4,BIT(8)|BIT(9));
    wifi_feat_ok(17);
    return 0;
}
static void rtw8723d_power_off(u32 m){
    rtw_pwr_seq_run(m,act_to_lps,NSEQ(act_to_lps)); rtw_pwr_seq_run(m,act_to_pre_carddis,NSEQ(act_to_pre_carddis));
    rtw_pwr_seq_run(m,act_to_cardemu,NSEQ(act_to_cardemu)); rtw_pwr_seq_run(m,cardemu_to_carddis,NSEQ(cardemu_to_carddis));
    rtw_pwr_seq_run(m,act_to_post_carddis,NSEQ(act_to_post_carddis));
}

static int rtw8723d_pcie_phy(u32 m){
    u32 cut=(rtw_r32(m,0x00F0)>>12)&0xFu;
    int r=rtw_mdio_write(m,0x08,0x4A22,1);
    if(cut>1)r|=rtw_mdio_write(m,0x09,0x1000,1);
    return r?-1:0;
}

static int g2_cck_grp(u8 ch){ return ch<=2?0:ch<=5?1:ch<=8?2:ch<=11?3:ch<=13?4:5; }
static int g2_bw40_grp(u8 ch){ int g=g2_cck_grp(ch); return g>4?4:g; }
static int s4(u8 v){ return (v&8)?(int)v-16:(int)v; }

int rtw8723d_efuse_parse(const u8* map,int len,rtw_efuse_info* o){
    if(!map||!o||len<0xDA)return -1;
    for(int i=0;i<6;i++)o->mac[i]=map[0xD0+i];
    o->rfe_option=0; o->crystal_cap=map[0xB9]==0xFF?0x20:map[0xB9]; o->thermal_meter=map[0xBA];
    o->country_code[0]=map[0xCB]; o->country_code[1]=map[0xCC]; g8d_afe=map[0x04]; g8d_bt=map[0xC3];
    for(int i=0;i<0x2A;i++)g8d_txpwr[i]=map[0x10+i];
    for(int p=0;p<2;p++) for(int c=0;c<14;c++){ u8 b=g8d_txpwr[6+g2_bw40_grp((u8)(c+1))]; o->tx_pwr_idx_2g[p][c]=p?0:(b==0xFF?0x20:b); o->tx_pwr_idx_5g[p][c]=0; }
    return 0;
}

static int rtw8723d_mac_init(u32 m){
    rtw_w32(m,0x0604,BIT(9)|BIT(12)|BIT(13));
    rtw_w16(m,0x06A0,0xFFFF); rtw_w16(m,0x06A2,0x0400); rtw_w16(m,0x06A4,0xFFFF);
    rtw_w32(m,0x0608,0x700060CEu);
    rtw_w32(m,0x0304,0); rtw_w32(m,0x01C0,0);
    rtw_w8(m,0x0577,0x03); rtw_w8(m,0x0976,0);
    return 0;
}

static void rtw8723d_phy_init(u32 m){
    w16set(m,0x0002,(u16)(BIT(13)|BIT(1)|BIT(0)));
    w8set(m,0x001F,(u8)(BIT(0)|BIT(1)|BIT(2)));
    rtw_w8(m,0x0025,0x80);
}

static void rtw8723d_lck(u32 m){
    u8 ctx=rtw_r8(m,0x0D03); int tx=(ctx&0x70)!=0;
    if(tx)rtw_w8(m,0x0D03,(u8)(ctx&~0x70)); else rtw_w8(m,0x0522,0xFF);
    u32 lc=rtw8723d_rf_read(m,0,0x18);
    rtw_rf_write(m,0,0x18,0x000FFFFFu,lc|BIT(15));
    int ok=0; for(int t=0;t<100;t++){ rtw_delay_us(10000); if(!(rtw8723d_rf_read(m,0,0x18)&BIT(15))){ ok=1; break; } }
    if(!ok)rtw8723d_lck_fail++;
    rtw_rf_write(m,0,0x18,0x000FFFFFu,lc);
    if(tx)rtw_w8(m,0x0D03,ctx); else rtw_w8(m,0x0522,0x00);
}

void rtw8723d_pt_init(void);
static void rtw8723d_phy_post(u32 m){
    w32clr(m,0x0608,BIT(11));
    w8set(m,0x05A7,(u8)BIT(0));
    w16set(m,0x0078,(u16)(BIT(4)|BIT(15)));
    u32 x=g_rtw_efuse.crystal_cap&0x3Fu; w32m(m,0x002C,0x00FFF000u,x|(x<<6));
    w32set(m,0x0800,BIT(24)|BIT(25));
    if((g8d_afe>>4)==14){ w32set(m,0x002C,BIT(18)); w32clr(m,0x0024,BIT(1)|BIT(2)); w32set(m,0x00CC,BIT(2)); w32clr(m,0x00CC,BIT(1)); }
    rtw_w8(m,0x051B,0x09); rtw_w8(m,0x0421,0x1F); rtw_w16(m,0x042A,0x3030); rtw_w32(m,0x04CC,0x0201FFFFu);
    rtw_w8(m,0x055A,0x2); rtw_w8(m,0x0550,(u8)(BIT(4)|BIT(3)|BIT(2)));
    { u32 v=rtw_r32(m,0x0540); v&=~0x000FFFFFu; v|=0x04u|(0x64u<<8); rtw_w8(m,0x0540,(u8)v); }
    rtw_w8(m,0x0512,0); rtw_w8(m,0x051A,0x16); rtw_w16(m,0x0546,0x0040);
    rtw_w16(m,0x063A,0x100A); rtw_w16(m,0x0514,0x100A); rtw_w16(m,0x0516,0x100A);
    rtw_w8(m,0x04C7,(u8)BIT(7)); rtw_w8(m,0x060C,0x17); rtw_w8(m,0x04CA,0x0A); rtw_w8(m,0x0456,0x70); rtw_w8(m,0x004E,0x82);
    rtw_w32(m,0x0798,0x90039003u); rtw_w32(m,0x079C,0x883C883Cu); rtw_w32(m,0x07A4,0xCB004010u); rtw_w32(m,0x07A8,0x01233425u);
    w16set(m,0x020C,(u16)BIT(9));
    rtw8723d_lck(m);
    w32m(m,0x0C50,0xFFu,0x50); w32m(m,0x0C50,0xFFu,0x20);
    rtw8723d_pt_init();
}

static void notch_cfg(u32 m,u8 ch,int on){
    static const u32 csi[2][4]={ {0x04000000u,0,0,0}, {0,0,0,0x00080000u} };
    if(!on){ w32m(m,0x0C40,0x1F000000u,0x1F); w32m(m,0x0C40,BIT(9),0); for(int i=0;i<4;i++)rtw_w32(m,0x0D40+4*i,0); w32m(m,0x0D2C,BIT(28),0); return; }
    if(ch!=13&&ch!=14){ w32m(m,0x0C40,BIT(9),0); w32m(m,0x0D2C,BIT(28),0); return; }
    w32m(m,0x0C40,0x1F000000u,ch==13?0xB:0x5); w32m(m,0x0C40,BIT(9),1);
    for(int i=0;i<4;i++)rtw_w32(m,0x0D40+4*i,csi[ch-13][i]);
    w32m(m,0x0D2C,BIT(28),1);
}
u32 rtw8723d_notch_on;
void rtw8723d_spur_cal(u32 m,u8 ch){
    if(ch<13){ notch_cfg(m,ch,0); return; }
    u32 f=ch==13?0xFCCDu:ch==14?0xFF9Au:0; int on=0;
    if(f){ rtw_w32(m,0x088C,0xCCF000C0u); rtw_w32(m,0x0808,f); rtw_w32(m,0x0808,0x400000u|f); rtw_delay_us(30000);
        on=rtw_r32(m,0x08B4)>=0x16; rtw_w32(m,0x0808,f); rtw_w32(m,0x088C,0xCCC000C0u); }
    if(on)rtw8723d_notch_on++;
    notch_cfg(m,ch,on);
}

static const struct { u16 r; u32 v; } cck_dfir[2][3]={
    { {0xA24,0x64B80C1Cu}, {0xA28,0x00008810u}, {0xAAC,0x01235667u} },
    { {0xA24,0x0000B81Cu}, {0xA28,0x00000000u}, {0xAAC,0x00003667u} } };

static void rtw8723d_set_channel(u32 m,u8 ch,u8 bw){
    if(ch<1||ch>14)return;
    if(bw>RTW_BW_40)bw=RTW_BW_40;
    if(bw==RTW_BW_40&&(ch<3||ch>11))bw=RTW_BW_20;
    u8 cc=bw==RTW_BW_40?(u8)(ch<=7?ch+2:ch-2):ch, upper=bw==RTW_BW_40&&ch<=7?0:1;
    u32 a=rtw8723d_rf_read(m,0,0x18);
    a=(a&~0xFFu)|cc; a&=~(BIT(11)|BIT(10)); a|=bw==RTW_BW_40?BIT(10):(BIT(11)|BIT(10));
    rtw_rf_write(m,0,0x18,0x000FFFFFu,a);
    rtw8723d_spur_cal(m,cc);
    rtw_w8(m,0x0483,(u8)(bw==RTW_BW_40?(upper?1:2):0));
    { u16 v=rtw_r16(m,0x0668); v&=(u16)~(BIT(7)|BIT(8)); if(bw==RTW_BW_40)v|=(u16)BIT(7); rtw_w16(m,0x0668,v); }
    for(int i=0;i<3;i++)rtw_w32(m,cck_dfir[cc<=13?0:1][i].r,cck_dfir[cc<=13?0:1][i].v);
    if(bw==RTW_BW_20){ w32m(m,0x0800,BIT(0),0); w32m(m,0x0900,BIT(0),0); w32m(m,0x0954,BIT(19),1); w32m(m,0x0954,0x0F000000u,0xA); }
    else { w32m(m,0x0800,BIT(0),1); w32m(m,0x0900,BIT(0),1); w32m(m,0x0954,BIT(19),0); w32m(m,0x0A00,BIT(4),upper?1:0); }
    g8d_bw=bw; g8d_cc=cc;
    if(g_rtw_chip->set_tx_power)g_rtw_chip->set_tx_power(m,ch);
}

int8_t rtw8723d_pt_rem_ofdm, rtw8723d_pt_rem_cck; u8 rtw8723d_txpwr_used[4];
static u8 clamp63(int v){ return (u8)(v<0?0:v>63?63:v); }
void rtw8723d_txpwr_calc(u8 ch,u8* cck,u8* ofdm,u8* ht20){
    u8 c=g8d_txpwr[g2_cck_grp(ch)], b=g8d_txpwr[6+g2_bw40_grp(ch)], d=g8d_txpwr[11];
    if(c==0xFF)c=0x20;
    if(b==0xFF)b=0x20;
    if(d==0xFF)d=0;
    *cck=clamp63(c); *ofdm=clamp63(b+s4(d&0xF)); *ht20=clamp63(b+s4((u8)(d>>4)));
}
int rtw_txpwr_regd_now(void) __attribute__((weak));
int regdb_max_dbm(u8 ch) __attribute__((weak));
static int l8d_raw(int rg,int bw,int rs,u8 ch){ int v=63;
    for(int i=0;i<(int)(sizeof rtw8723d_txpwr_lmt/sizeof rtw8723d_txpwr_lmt[0]);i++){ const signed char* e=rtw8723d_txpwr_lmt[i];
        if(!e[1]&&e[2]==bw&&e[3]==rs&&e[4]==ch&&(rg==12||e[0]==rg)&&e[5]<v)v=e[5]; }
    return v; }
static int l8d_one(int rg,int bw,int rs,u8 ch){ int v=l8d_raw(rg,bw,rs,ch); if(v<63)return v; for(u8 c=1;c<=13;c++){ int w=l8d_raw(rg,0,rs,c); if(w<v)v=w; } return v; }
int rtw8723d_txpwr_limit(int regd,u8 ch,int bw,u8 cc,int rs){
    static const signed char alt[13]={0,1,2,0,2,2,0,2,0,2,2,2,12};
    int rg=regd<0||regd>12?12:alt[regd], v=l8d_one(rg,0,rs,ch);
    if(rs==2&&bw){ int w=l8d_raw(rg,1,rs,cc); if(w<v)v=w; }
    if(regdb_max_dbm){ int d=regdb_max_dbm(ch); if(d>0&&d*2<v)v=d*2; }
    return v; }
static u8 l8d_cap(u8 b,int rem,int lmt,int base){ int v=b+rem, c=b+lmt-base; return clamp63(v<c?v:c); }
void rtw8723d_set_tx_power(u32 m,u8 ch){
    if(ch<1||ch>14)return;
    u8 c,o,h; rtw8723d_txpwr_calc(ch,&c,&o,&h);
    int rg=rtw_txpwr_regd_now?rtw_txpwr_regd_now():12, bw=g8d_bw&&g8d_cc?1:0; u8 cc=bw?g8d_cc:ch;
    c=l8d_cap(c,rtw8723d_pt_rem_cck,rtw8723d_txpwr_limit(rg,ch,0,ch,0),32); o=l8d_cap(o,rtw8723d_pt_rem_ofdm,rtw8723d_txpwr_limit(rg,ch,0,ch,1),28); h=l8d_cap(h,rtw8723d_pt_rem_ofdm,rtw8723d_txpwr_limit(rg,ch,bw,cc,2),26);
    rtw8723d_txpwr_used[0]=c; rtw8723d_txpwr_used[1]=o; rtw8723d_txpwr_used[2]=h; rtw8723d_txpwr_used[3]=(u8)rg;
    u32 o4=o*0x01010101u, h4=h*0x01010101u;
    w32m(m,0x0E08,0x0000FF00u,c); w32m(m,0x086C,0xFFFFFF00u,(u32)c*0x010101u);
    rtw_w32(m,0x0E00,o4); rtw_w32(m,0x0E04,o4); rtw_w32(m,0x0E10,h4); rtw_w32(m,0x0E14,h4);
}

typedef struct { u32 btg, rfe_lw, pi, padlut, gaintx, bspad, wlint, wlsel, pts; } iqk_cfg8d;
static const iqk_cfg8d iqk8d[2]={ {0x99000000u,0xEF,0x8214019Fu,0xDF,0x56,0x54,0xE0D,0x60D,0xFA000000u}, {0x99000280u,0xEE,0x8214018Au,0xDE,0x66,0x64,0xE6D,0x66D,0xF9000000u} };
static const u16 iqk_adda[16]={0x85C,0xE6C,0xE70,0xE74,0xE78,0xE7C,0xE80,0xE84,0xE88,0xE8C,0xED0,0xED4,0xED8,0xEDC,0xEE0,0xEEC};
static const u16 iqk_mac8[3]={0x522,0x550,0x551}, iqk_bb[9]={0xC04,0xC08,0x874,0xB68,0xB6C,0x870,0x860,0x864,0xA04};
typedef struct { u32 adda[16], mac32, bb[9], igia, igib, btg_sel_bb, lte_path, lte_gnt; u8 mac8[3], btg; } iqk_bak8d;
u32 rtw8723d_iqk_runs, rtw8723d_iqk_fail;
int rtw8723d_iqk_res[8], rtw8723d_iqk_done;
static u32 r32m(u32 m,u32 r,u32 mask){ return (rtw_r32(m,r)&mask)>>sh_of(mask); }
static void rfw(u32 m,int p,u32 a,u32 mask,u32 v){ if(mask!=0x000FFFFFu){ u32 s=sh_of(mask); v=(rtw8723d_rf_read(m,p,a)&~mask)|((v<<s)&mask); } rtw_w32(m,p?0x0844u:0x0840u,((a&0xFFu)<<20)|(v&0x000FFFFFu)); rtw_delay_us(13); }
static int iqs(int v){ return (int)((u32)v<<22)>>22; }
static int iqmul(int x,int y,int* ext){ int t=x*y; if(ext)*ext=(t>>7)&1; return t>>8; }
static void lte_cfg(u32 m,u32 d){ rtw_w32(m,0x07C4,d); rtw_w32(m,0x07C0,0xC0020038u); w32m(m,0x0070,BIT(26),1); }
static void lte_rst(u32 m,const iqk_bak8d* b){ rtw_w32(m,0x07C4,b->lte_gnt); rtw_w32(m,0x07C0,0xC00F0038u); rtw_w32(m,0x0070,b->lte_path); }
static void adda_on(u32 m){ for(int i=0;i<16;i++)rtw_w32(m,iqk_adda[i],0x03C00016u); }
static void iqk_mode(u32 m,int on){ w32m(m,0x0E28,0xFFFFFF00u,on?0x808000u:0); }
static void iqk_backup(u32 m,iqk_bak8d* b){
    b->btg=rtw_r8(m,0x0067); b->lte_path=rtw_r32(m,0x0070); rtw_w32(m,0x07C0,0x800F0038u); rtw_delay_us(1000); b->lte_gnt=rtw_r32(m,0x07C8);
    for(int i=0;i<16;i++)b->adda[i]=rtw_r32(m,iqk_adda[i]);
    for(int i=0;i<3;i++)b->mac8[i]=rtw_r8(m,iqk_mac8[i]);
    b->mac32=rtw_r32(m,0x0040);
    for(int i=0;i<9;i++)b->bb[i]=rtw_r32(m,iqk_bb[i]);
    b->igia=rtw_r32(m,0x0C50)&0xFFu; b->igib=rtw_r32(m,0x0C58)&0xFFu; b->btg_sel_bb=rtw_r32(m,0x0948);
}
static void iqk_restore(u32 m,const iqk_bak8d* b){
    for(int i=0;i<16;i++)rtw_w32(m,iqk_adda[i],b->adda[i]);
    for(int i=0;i<3;i++)rtw_w8(m,iqk_mac8[i],b->mac8[i]);
    rtw_w32(m,0x0040,b->mac32);
    for(int i=0;i<9;i++)rtw_w32(m,iqk_bb[i],b->bb[i]);
    w32m(m,0x0C50,0xFFu,0x50); w32m(m,0x0C50,0xFFu,b->igia); w32m(m,0x0C58,0xFFu,0x50); w32m(m,0x0C58,0xFFu,b->igib);
    rtw_w32(m,0x0E30,0x01008C00u); rtw_w32(m,0x0E34,0x01008C00u);
}
static void iqk_shot(u32 m,int tx,const iqk_cfg8d* c){
    iqk_mode(m,1); lte_cfg(m,0x0000FF00u);
    rtw_w32(m,0x07C0,0x800F0054u); rtw_delay_us(1000);
    rtw_w32(m,0x0E48,tx?c->pts:0xF9000000u); rtw_w32(m,0x0E48,0xF8000000u);
    for(int i=0;i<1000;i++){ if(rtw_r32(m,0x0EAC)&BIT(26))return; rtw_delay_us(10); }
}
static int iqk_tx_ok(u32 m){ u32 f=r32m(m,0x0EAC,BIT(28)), x=r32m(m,0x0E94,0x03FF0000u), y=r32m(m,0x0E9C,0x03FF0000u); return !f&&x!=0x142&&y!=0x42; }
static int iqk_rx_ok(u32 m){ u32 f=r32m(m,0x0EAC,BIT(27)), x=r32m(m,0x0EA4,0x03FF0000u); int y=iqs((int)r32m(m,0x0EAC,0x03FF0000u)); if(y<0)y=-y; return !f&&x<0x11A&&x>0xE6&&y<0x1A; }
static void iqk_post(u32 m,const iqk_cfg8d* c,const iqk_bak8d* b){
    lte_rst(m,b); rtw_w32(m,0x0948,b->btg_sel_bb); iqk_mode(m,0); rtw_delay_us(1000);
    rfw(m,0,c->padlut,0x800,0); rfw(m,0,0x01,BIT(0),0); rfw(m,0,0x02,BIT(0),0);
}
static void iqk_lut(u32 m,const iqk_cfg8d* c,u32 wa,u32 wd1,u32 wd0){ rfw(m,0,c->rfe_lw,0x000FFFFFu,0x80000); rfw(m,0,0x33,0x000FFFFFu,wa); rfw(m,0,0x3E,0x000FFFFFu,wd1); rfw(m,0,0x3F,0x000FFFFFu,wd0); rfw(m,0,c->rfe_lw,0x000FFFFFu,0); }
static int iqk_tx_path(u32 m,const iqk_cfg8d* c,const iqk_bak8d* b){
    rtw_w32(m,0x0948,c->btg); iqk_mode(m,0); rtw_delay_us(1000);
    iqk_lut(m,c,0x00004,0x0005D,0xBFFE0);
    rtw_w32(m,0x0E30,0x08008C0Cu); rtw_w32(m,0x0E34,0x38008C1Cu); rtw_w32(m,0x0E38,c->pi); rtw_w32(m,0x0E3C,0x28160200u);
    rtw_w32(m,0x0E40,0x01007C00u); rtw_w32(m,0x0E44,0x01004800u); rtw_w32(m,0x0E4C,0x00462911u);
    rfw(m,0,c->padlut,0x800,1); rfw(m,0,c->gaintx,0x600,0); rfw(m,0,c->gaintx,0x1E0,3); rfw(m,0,0x8D,0x1F,0xF);
    rfw(m,0,c->rfe_lw,0x10,1); rfw(m,0,c->bspad,0x1,1);
    rfw(m,0,0x01,0x000FFFFFu,c->wlint); rfw(m,0,0x02,0x000FFFFFu,c->wlsel);
    iqk_shot(m,1,c); int st=iqk_tx_ok(m)?1:0; iqk_post(m,c,b); return st;
}
static int iqk_rx_path(u32 m,const iqk_cfg8d* c,const iqk_bak8d* b){
    rtw_w32(m,0x0948,c->btg); iqk_mode(m,0);
    rtw_w32(m,0x0E40,0x01007C00u); rtw_w32(m,0x0E44,0x01004800u);
    rtw_w32(m,0x0E30,0x18008C1Cu); rtw_w32(m,0x0E34,0x38008C1Cu); rtw_w32(m,0x0E50,0x38008C1Cu); rtw_w32(m,0x0E54,0x38008C1Cu);
    rtw_w32(m,0x0E38,0x82160000u); rtw_w32(m,0x0E3C,0x28160000u); rtw_w32(m,0x0E4C,0x0046A911u);
    iqk_lut(m,c,0x00006,0x0005F,0xA7FFB);
    rfw(m,0,c->padlut,0x800,1); rfw(m,0,c->gaintx,0x600,0);
    rfw(m,0,0x01,0x000FFFFFu,c->wlint); rfw(m,0,0x02,0x000FFFFFu,c->wlsel);
    iqk_shot(m,0,c); int st=iqk_tx_ok(m)?1:0;
    if(st){
        u32 x=r32m(m,0x0E94,0x03FF0000u), y=r32m(m,0x0E9C,0x03FF0000u);
        rtw_w32(m,0x0E40,0x80007C00u|(x<<16)|y); rtw_w32(m,0x0E44,0x01004800u);
        rtw_w32(m,0x0E30,0x38008C1Cu); rtw_w32(m,0x0E34,0x18008C1Cu); rtw_w32(m,0x0E50,0x38008C1Cu); rtw_w32(m,0x0E54,0x38008C1Cu);
        rtw_w32(m,0x0E38,0x82170000u); rtw_w32(m,0x0E3C,0x28171400u); rtw_w32(m,0x0E4C,0x0046A8D1u);
        iqk_mode(m,0); rtw_delay_us(1000);
        rfw(m,0,c->rfe_lw,0x80000,1); rfw(m,0,0x33,0x000FFFFFu,0x00007); rfw(m,0,0x3E,0x000FFFFFu,0x0005F); rfw(m,0,0x3F,0x000FFFFFu,0xB3FDB); rfw(m,0,c->rfe_lw,0x000FFFFFu,0);
        iqk_shot(m,0,c); if(iqk_rx_ok(m))st|=2;
    }
    iqk_post(m,c,b); return st;
}
static void iqk_standby(u32 m,int p){ iqk_mode(m,0); rtw_delay_us(1000); rfw(m,p,0x00,0x000FFFFFu,0x10000); iqk_mode(m,1); }
static void iqk_precfg(u32 m,int s0){
    if(s0){ iqk_standby(m,0); adda_on(m); }
    iqk_mode(m,1); rtw_w32(m,0x0E40,0x01007C00u); rtw_w32(m,0x0E44,0x01004800u);
    if(!s0){ iqk_standby(m,1); adda_on(m); }
}
static void iqk_round(u32 m,int r[8],const iqk_bak8d* b){
    adda_on(m); rtw_w8(m,0x0522,0xFF);
    w32m(m,0x0A04,0x0F000000u,0xF); rtw_w32(m,0x0C04,0x03A05611u); rtw_w32(m,0x0C08,0x000800E4u); rtw_w32(m,0x0874,0x25204200u);
    for(int p=0;p<2;p++){
        const iqk_cfg8d* c=&iqk8d[p]; int* o=r+p*4;
        iqk_precfg(m,p);
        for(int i=0;i<2;i++){ if(iqk_tx_path(m,c,b)==1){ o[0]=(int)r32m(m,0x0E94,0x03FF0000u); o[1]=(int)r32m(m,0x0E9C,0x03FF0000u); break; } o[0]=0x100; o[1]=0; }
        for(int i=0;i<2;i++){ if(iqk_rx_path(m,c,b)==3){ o[2]=(int)r32m(m,0x0EA4,0x03FF0000u); o[3]=(int)r32m(m,0x0EAC,0x03FF0000u); break; } o[2]=0x100; o[3]=0; }
    }
    iqk_mode(m,0); rtw_delay_us(1000);
}
int rtw8723d_iqk_similar(int res[4][8],int c1,int c2){
    u32 bm=0; int cand[2]={-1,-1};
    for(int i=0;i<8;i++){ int d=iqs(res[c1][i])-iqs(res[c2][i]); if(d<0)d=-d; if(d<=5)continue;
        if((i==2||i==6)&&!bm){ if(res[c1][i]+res[c1][i+1]==0)cand[i/4]=c2; else if(res[c2][i]+res[c2][i+1]==0)cand[i/4]=c1; else bm|=BIT(i); }
        else bm|=BIT(i); }
    if(bm){ for(int i=0;i<8;i++){ int j=i&~1; if(bm&(3u<<j))continue; res[3][i]=res[c1][i]; } return 0; }
    int ok=1; for(int p=0;p<2;p++){ if(cand[p]<0)continue; for(int j=p*4;j<p*4+2;j++)res[3][j]=res[cand[p]][j]; ok=0; }
    return ok;
}
static void iqk_fill(u32 m,const int* r){
    if(r[0]){ int old=(int)r32m(m,0x0C80,0xFFC00000u), ea, ec;
        int a=iqmul(iqs(r[0]),old,&ea); w32m(m,0x0C80,0x3FFu,(u32)a&0x3FFu); w32m(m,0x0C4C,BIT(31),(u32)ea);
        int c=iqmul(iqs(r[1]),old,&ec); w32m(m,0x0C94,0xF0000000u,((u32)c&0x3C0u)>>6); w32m(m,0x0C80,0x003F0000u,(u32)c&0x3Fu); w32m(m,0x0C4C,BIT(29),(u32)ec);
        if(r[2]){ w32m(m,0x0C14,0x3FFu,(u32)r[2]); w32m(m,0x0C14,0xFC00u,(u32)r[3]&0x3Fu); w32m(m,0x0CA0,0xF0000000u,((u32)r[3]>>6)&0xFu); } }
    if(r[4]){ int old=(int)r32m(m,0x0CD4,0x007FE000u), ea, ec;
        int a=iqmul(iqs(r[4]),old,&ea); w32m(m,0x0CD0,0x7FEu,(u32)a&0x3FFu); w32m(m,0x0CD0,BIT(0),(u32)ea);
        int c=iqmul(iqs(r[5]),old,&ec); w32m(m,0x0CD4,0x7FEu,(u32)c&0x3FFu); w32m(m,0x0CD4,BIT(0),(u32)ec);
        if(r[6]){ w32m(m,0x0CD8,0x3FFu,(u32)r[6]); w32m(m,0x0CD8,0x003FF000u,(u32)r[7]); } }
}
static void rtw8723d_calibrate(u32 m){
    extern int g_rtw_iqk_last;
    int res[4][8]; for(int i=0;i<4;i++)for(int j=0;j<8;j++)res[i][j]=0;
    iqk_bak8d b; iqk_backup(m,&b); int fin=-1;
    for(int i=0;i<3&&fin<0;i++){
        w32m(m,0x0064,BIT(31),1); lte_cfg(m,0x0000FF00u);
        iqk_round(m,res[i],&b);
        if(i>0)iqk_restore(m,&b);
        lte_rst(m,&b); rtw_w8(m,0x0067,b.btg);
        for(int j=0;j<i;j++) if(rtw8723d_iqk_similar(res,j,i)){ fin=j; break; }
    }
    if(fin<0){ int t=0; for(int i=0;i<8;i++)t+=res[3][i]; if(t)fin=3; }
    rtw8723d_iqk_runs++;
    if(fin>=0){ iqk_fill(m,res[fin]); for(int i=0;i<8;i++)rtw8723d_iqk_res[i]=res[fin][i]; rtw8723d_iqk_done=1;
        u32 fm=(res[fin][0]==0x100&&!res[fin][1]?1u:0u)|(res[fin][2]==0x100&&!res[fin][3]?2u:0u)|(res[fin][4]==0x100&&!res[fin][5]?4u:0u)|(res[fin][6]==0x100&&!res[fin][7]?8u:0u);
        g_rtw_iqk_last=1|(int)(fm<<8); }
    else { rtw8723d_iqk_fail++; g_rtw_iqk_last=-2; }
    rtw_w32(m,0x0948,b.btg_sel_bb);
}

static const u16 iqk_mtx[8]={0xC80,0xC94,0xC4C,0xC14,0xCA0,0xCD0,0xCD4,0xCD8};
static u32 iqk_mtx_sv[8];
void rtw8723d_iqk_regs(u32 m,int put){
    if(!put){ for(int i=0;i<8;i++)iqk_mtx_sv[i]=rtw_r32(m,iqk_mtx[i]); return; }
    for(int i=0;i<8;i++)rtw_w32(m,iqk_mtx[i],iqk_mtx_sv[i]);
    rtw8723d_iqk_done=0; for(int i=0;i<8;i++)rtw8723d_iqk_res[i]=0;
}

static const u32 pt_ofdm[43]={
    0x0b40002d,0x0c000030,0x0cc00033,0x0d800036,0x0e400039,0x0f00003c,0x10000040,0x11000044,0x12000048,0x1300004c,0x14400051,0x15800056,
    0x16c0005b,0x18000060,0x19800066,0x1b00006c,0x1c800072,0x1e400079,0x20000080,0x22000088,0x24000090,0x26000098,0x288000a2,0x2ac000ab,
    0x2d4000b5,0x300000c0,0x32c000cb,0x35c000d7,0x390000e4,0x3c8000f2,0x40000100,0x43c0010f,0x47c0011f,0x4c000130,0x50800142,0x55400155,
    0x5a400169,0x5fc0017f,0x65400195,0x6b8001ae,0x71c001c7,0x788001e2,0x7f8001fe};
static const u16 pt_cck[41]={
    0x0CD,0x0D9,0x0E6,0x0F3,0x102,0x111,0x121,0x132,0x144,0x158,0x16C,0x182,0x198,0x1B1,0x1CA,0x1E5,0x202,0x221,0x241,0x263,
    0x287,0x2AE,0x2D6,0x301,0x32F,0x35F,0x392,0x3C9,0x402,0x43F,0x47F,0x4C3,0x50C,0x558,0x5A9,0x5FF,0x65A,0x6BA,0x720,0x78C,0x7FF};
static const u8 pt_ga_n[30]={0,0,1,1,1,2,2,3,4,4,4,4,5,5,5,6,6,7,7,8,8,8,9,9,9,10,10,10,10,10};
static const u8 pt_ga_p[30]={0,0,1,1,2,2,2,3,3,4,4,5,5,6,7,7,8,8,8,9,9,10,10,10,10,10,10,10,10,10};
static const u8 pt_cck_n[30]={0,1,1,1,1,2,2,3,3,4,4,5,5,6,6,6,7,7,7,8,8,9,9,10,10,11,11,11,11,11};
static const u8 pt_cck_p[30]={0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,9,9,10,10,11,11,11,11,11,11,11,11,11};
static const int8_t pt_xtal_p[30]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,-10,-12,-14,-16,-16,-16,-16,-16,-16,-16,-16,-16,-16,-16};
static struct { u32 ewma; u8 trig, init, k; } pt;
u8 rtw8723d_pt_avg; int8_t rtw8723d_pt_delta; u32 rtw8723d_pt_runs, rtw8723d_pt_iqk;
void rtw8723d_pt_init(void){ pt.ewma=0; pt.trig=0; pt.init=1; pt.k=g_rtw_efuse.thermal_meter; rtw8723d_pt_avg=0; rtw8723d_pt_delta=0; rtw8723d_pt_rem_ofdm=0; rtw8723d_pt_rem_cck=0; }
int rtw8723d_pt_limit_ofdm(u8 r){ return r<=3?30:r<=10?36:r==11?34:r<=14?38:r<=16?36:r<=19?34:30; }
static void pt_matrix(u32 m,int idx,int b){
    if(idx>42)idx=42; else if(idx<0)idx=0;
    u32 sw=pt_ofdm[idx], d=sw>>22;
    if(rtw8723d_iqk_done){
        int x=rtw8723d_iqk_res[b?4:0], y=rtw8723d_iqk_res[b?5:1], de, ae, ce;
        iqmul(x,(int)d,&de); int a=iqmul(iqs(x),(int)d,&ae), c=iqmul(iqs(y),(int)d,&ce);
        if(!b){ rtw_w32(m,0x0C80,(d<<22)|(((u32)c&0x3Fu)<<16)|((u32)a&0x3FFu)); w32m(m,0x0C94,0xF0000000u,((u32)c&0x3C0u)>>6);
            rtw_w32(m,0x0C4C,(rtw_r32(m,0x0C4C)&~(BIT(31)|BIT(29)|BIT(28)))|((u32)ae<<31)|((u32)ce<<29)|((u32)de<<28)); }
        else { w32m(m,0x0CD4,0x007FE000u,d); w32m(m,0x0CD4,0x7FEu,(u32)c&0x3FFu); w32m(m,0x0CD0,0x7FEu,(u32)a&0x3FFu);
            w32m(m,0x0CD4,BIT(12),(u32)de); w32m(m,0x0CD0,BIT(0),(u32)ae); w32m(m,0x0CD4,BIT(0),(u32)ce); }
        return;
    }
    if(!b){ rtw_w32(m,0x0C80,sw); w32m(m,0x0C94,0xF0000000u,0); w32clr(m,0x0C4C,BIT(31)|BIT(29)|BIT(28)); return; }
    w32m(m,0x0CD0,0x7FEu,sw&0x3FFu); w32m(m,0x0CD0,0x0007E000u,(sw>>10)&0x3Fu); w32m(m,0x0CD4,0x7FEu,(sw>>16)&0x3Fu); w32m(m,0x0CD4,0x007FE000u,d);
    w32m(m,0x0CD4,BIT(12),0); w32m(m,0x0CD4,BIT(0),0); w32m(m,0x0CD0,BIT(0),0);
}
static void pt_set(u32 m,u8 rate){
    int lo=rtw8723d_pt_limit_ofdm(rate), f=28+rtw8723d_pt_delta, idx, rem;
    if(f>lo){ idx=lo; rem=f-lo; } else if(f<0){ idx=0; rem=f; } else { idx=f; rem=0; }
    rtw8723d_pt_rem_ofdm=(int8_t)rem; pt_matrix(m,idx,0); pt_matrix(m,idx,1);
    f=28+rtw8723d_pt_delta;
    if(f>40){ idx=40; rem=f-40; } else if(f<0){ idx=0; rem=f; } else { idx=f; rem=0; }
    rtw8723d_pt_rem_cck=(int8_t)rem; w32m(m,0x0AB4,0x7FFu,pt_cck[idx]);
    if(g_wifi_curch)rtw8723d_set_tx_power(m,g_wifi_curch);
}
static void pt_track(u32 m){
    u8 t=(u8)((rtw8723d_rf_read(m,0,0x42)>>10)&0x3Fu), e=g_rtw_efuse.thermal_meter, rate=g_rtw_ra_rpt.n?g_rtw_ra_rpt.rate:0;
    pt.ewma=pt.ewma?(((pt.ewma<<2)-pt.ewma)+((u32)t<<10))>>2:(u32)t<<10;
    u8 avg=(u8)(pt.ewma>>10); rtw8723d_pt_avg=avg; rtw8723d_pt_runs++;
    int dk=avg>pt.k?avg-pt.k:pt.k-avg, iqk=dk>=8;
    if(iqk){ pt.k=avg; rtw8723d_lck(m); }
    if(pt.init)pt.init=0; else if(avg==t)goto out;
    { int d=avg>e?avg-e:e-avg; if(d>29)d=29;
      int ck=rate<=3, hot=avg>e;
      int8_t cur=(int8_t)(hot?(ck?pt_cck_p:pt_ga_p)[d]:-(int)(ck?pt_cck_n:pt_ga_n)[d]);
      if(cur!=rtw8723d_pt_delta){ rtw8723d_pt_delta=cur; pt_set(m,rate); }
      int x=(int)(g_rtw_efuse.crystal_cap&0x3Fu)+(hot?pt_xtal_p[d]:0); if(x<0)x=0; if(x>0x3F)x=0x3F;
      w32m(m,0x002C,0x00FFF000u,(u32)x|((u32)x<<6)); }
out:
    if(iqk){ rtw8723d_pt_iqk++; rtw_chip_calibrate(m); }
}
void rtw8723d_pwr_track(u32 m){
    if(!m||g_rtw_efuse.thermal_meter==0xFF)return;
    if(!pt.trig){ rtw_rf_write(m,0,0x42,0x30000u,3); pt.trig=1; return; }
    pt_track(m); pt.trig=0;
}

static void cpu_en(u32 m,int on){
    if(on){ w8set(m,0x001D,(u8)BIT(0)); w8set(m,0x0003,(u8)BIT(2)); }
    else { w8clr(m,0x0003,(u8)BIT(2)); w8clr(m,0x001D,(u8)BIT(0)); }
}
int rtw8723d_fw_download(u32 m,const u8* fw,long sz){
    if(!fw||sz<=32||sz>0x8000+32){ rtw8723d_fw_fail++; return -10; }
    u16 sig=(u16)(fw[0]|(fw[1]<<8)); if((sig&0xFFF0u)!=0x23D0u){ rtw8723d_fw_fail++; return -11; }
    cpu_en(m,0); cpu_en(m,1);
    w8set(m,0x0080,(u8)BIT(0));
    int en=0; for(int t=0;t<10;t++){ if(rtw_r8(m,0x0080)&BIT(0)){ en=1; break; } w8set(m,0x0080,(u8)BIT(0)); rtw_delay_us(20000); }
    if(!en){ rtw8723d_fw_fail++; return -12; }
    w32clr(m,0x0080,BIT(19));
    const u8* d=fw+32; u32 left=(u32)(sz-32);
    w8set(m,0x0080,(u8)BIT(2));
    for(u32 pg=0;left;pg++){
        u32 n=left>0x1000u?0x1000u:left;
        u32 v=rtw_r32(m,0x0080); v=(v&~(7u<<16))|((pg<<16)&(7u<<16)); rtw_w32(m,0x0080,v);
        for(u32 i=0;i<n;i+=4){ u32 w=0; for(u32 k=0;k<4&&i+k<n;k++)w|=(u32)d[i+k]<<(8*k); rtw_w32(m,0x1000+i,w); }
        d+=n; left-=n;
    }
    int rp=0; for(int t=0;t<1000;t++){ if(rtw_r32(m,0x0080)&BIT(2)){ rp=1; break; } rtw_delay_us(10); }
    w8clr(m,0x0080,(u8)BIT(0));
    if(!rp){ rtw8723d_fw_fail++; wifi_feat_fail(17,"firmware page check failed"); return -13; }
    u32 v=rtw_r32(m,0x0080); v|=BIT(1); v&=~BIT(6); rtw_w32(m,0x0080,v);
    cpu_en(m,0); cpu_en(m,1);
    u32 rdy=BIT(1)|BIT(2)|BIT(6)|BIT(7);
    for(int t=0;t<10;t++){ if((rtw_r32(m,0x0080)&rdy)==rdy){ g_h2c_box=0; return 0; } rtw_delay_us(20000); }
    rtw8723d_fw_fail++; wifi_feat_fail(17,"firmware did not report ready"); return 1;
}

void rtw8723d_coex_init(u32 m){
    int share=g8d_bt!=0xFF&&(g8d_bt&1), aux=g8d_bt!=0xFF&&(g8d_bt&BIT(6));
    w8set(m,0x0550,(u8)BIT(3));
    rtw_w16(m,0x0948,(u16)(share?(aux?0x80:0x200):(aux?0x280:0)));
    int ok=rtw_lte_mask(m,0x38,BIT(7),0);
    ok&=rtw_lte_mask(m,0xA0,0xFFFFu,0xFFFF); ok&=rtw_lte_mask(m,0xA4,0xFFFFu,0xFFFF);
    rtw_w8(m,0x0790,(u8)((rtw_r8(m,0x0790)&~0x3Fu)|0x05u));
    rtw_w8(m,0x0778,0x01);
    w32set(m,0x0040,BIT(5)|BIT(9));
    w8set(m,0x04C6,(u8)BIT(4));
    w16set(m,0x0762,(u16)BIT(12));
    w8set(m,0x06CF,(u8)BIT(3));
    ok&=rtw_lte_mask(m,0x38,0xC000u,1); ok&=rtw_lte_mask(m,0x38,0x0C00u,1);
    ok&=rtw_lte_mask(m,0x38,0x3000u,3); ok&=rtw_lte_mask(m,0x38,0x0300u,3);
    w8set(m,0x0073,0x04); w8set(m,0x0067,(u8)BIT(7));
    rtw_w32(m,0x06C0,0x55555555u); rtw_w32(m,0x06C4,0x55555555u); rtw_w32(m,0x06C8,0xF0FFFFFFu);
    rtw_w16(m,0x00AA,(u16)(0x8000u|0x0003u));
    g_rtw_coex_ok=ok;
}

const rtw_chip rtw8723d_chip = {
    .name = "RTL8723DE",
    .vendor = 0x10EC, .device = 0xD723,
    .bus = RTW_BUS_PCIE,
    .n_paths = 1,
    .wcpu = RTW_WCPU_8051,
    .txff_pages = 32768/128,
    .rxff_size = 16384,
    .txff_size = 32768,
    .rsvd_drv_pages = 8,
    .csi_pages = 0, .cond_mode = RTW_COND_8821C, .sys_func_en = 0xFD, .fw_rxff_size = 0,
    .tx_desc_sz = 40, .rx_desc_sz = 24, .tx_buf_desc_sz = 16,
    .fw_start = _binary_rtw8723d_fw_bin_start,
    .fw_end = _binary_rtw8723d_fw_bin_end,
    .fw_name = "rtw88/rtw8723d_fw.bin",
    .page_table = { 12, 2, 2, 0, 1 },
    .rqpn = { 2, 2, 1, 1, 0, 3 },
    .mac_tbl = { rtw8723d_cmac, sizeof(rtw8723d_cmac)/8, 1 },
    .bb_tbl = { rtw8723d_cbb, sizeof(rtw8723d_cbb)/8, 4 },
    .agc_tbl = { rtw8723d_cagc, sizeof(rtw8723d_cagc)/8, 4 },
    .agc_btg_tbl = { 0, 0, 0 },
    .rf_tbl = { { rtw8723d_crf_a, sizeof(rtw8723d_crf_a)/8, 5 }, { 0, 0, 0 } },
    .power_on = rtw8723d_power_on,
    .power_off = rtw8723d_power_off,
    .pcie_phy = rtw8723d_pcie_phy,
    .efuse_phy = 512, .efuse_log = 512, .efuse_prot = 97,
    .efuse_parse = rtw8723d_efuse_parse,
    .mac_init = rtw8723d_mac_init,
    .phy_init = rtw8723d_phy_init,
    .set_channel = rtw8723d_set_channel,
    .set_tx_power = rtw8723d_set_tx_power,
    .calibrate = rtw8723d_calibrate,
    .coex_wifi_only = rtw8723d_coex_init,
    .rfe_setup = 0,
    .phy_post = rtw8723d_phy_post,
    .rf_base = { 0x8A0, 0x8A4 },
    .rf_sipi = { 0x840, 0x844 },
    .rf_mix = 0,
    .trx_init = rtw8723d_trx_init,
    .rx_p0_pwdb = 1,
    .rf_read = rtw8723d_rf_read,
    .pwr_track = rtw8723d_pwr_track,
    .iqk_regs = rtw8723d_iqk_regs,
    .lte_base = 0x07C0,
};
