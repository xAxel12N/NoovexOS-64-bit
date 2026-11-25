/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/rtw8852b.c (rtw8852b_set_channel, rtw8852b_set_channel_help, rtw8852b_bb_reset_en,
 * rtw8852b_adc_en, rtw8852b_tssi_cont_en), rtw8852b_common.c (__rtw8852bx_set_channel_mac, __rtw8852bx_set_channel_bb, rtw8852bx_ctrl_sco_cck,
 * rtw8852bx_sco_mapping, rtw8852bx_ctrl_ch, rtw8852b_bw_setting, rtw8852bx_ctrl_bw, rtw8852bx_ctrl_cck_en, rtw8852bx_5m_mask,
 * __rtw8852bx_bb_reset_all), rtw8852b_rfk.c (rtw8852b_set_channel_rf, _ch_setting, _bw_setting, _set_s0_arfc18, _lck_check, _set_rxbb_bw),
 * rtw8852c.c (rtw8852c_set_channel_mac, rtw8852c_ctrl_sco_cck, rtw8852c_set_gain_error, rtw8852c_ctrl_ch, rtw8852c_ctrl_bw, rtw8852c_bw_setting,
 * rtw8852c_edcca_per20_bitmap_sifs, rtw8852c_spur_freq, rtw8852c_set_csi_tone_idx, rtw8852c_set_nbi_tone_idx, rtw8852c_5m_mask, rtw8852c_ctrl_btg_bt_rx,
 * rtw8852c_ctrl_tx_path_tmac, rtw8852c_bb_reset_all/_en, rtw8852c_set_channel_bb, rtw8852c_set_channel_help, rtw8852c_bb_sethw, rtw8852c_bb_gpio_init),
 * rtw8852c_rfk.c (_ch_setting, _ctrl_ch, _bw_setting, _ctrl_bw, _set_rxbb_bw, _set_tia_bw, rtw8852c_tssi_cont_en), util.h (s32_div_u32_round_down/closest),
 * phy.c (rtw89_phy_read_rf, rtw89_phy_read_rf_a, rtw89_encode_chan_idx) and reg.h; GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2024  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_chan.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_txpwr.h"

struct rtw89_chan_state g_r89ch;

static u32 k8_sh(u32 m){ u32 s=0; while(s<31&&!((m>>s)&1))s++; return s; }
static u32 k8_prd(struct rtw89_softc* sc,u32 a){ return sc->io->rd(sc,a|RTW89_PHY_ADDR_OFFSET,4); }
static u32 k8_prdm(struct rtw89_softc* sc,u32 a,u32 m){ return (k8_prd(sc,a)&m)>>k8_sh(m); }
static void k8_pwm(struct rtw89_softc* sc,u32 a,u32 m,u32 v){ u32 o=m==0xFFFFFFFFu?0:k8_prd(sc,a)&~m; sc->io->wr(sc,a|RTW89_PHY_ADDR_OFFSET,o|((v<<k8_sh(m))&m),4); }
static void k8_m8(struct rtw89_softc* sc,u32 a,u8 clr,u8 set){ sc->io->wr(sc,a,((sc->io->rd(sc,a,1)&0xFFu)&(u32)~clr)|set,1); }

u32 rtw89_read_rf(struct rtw89_softc* sc,int path,u32 addr,u32 mask){
    static const u32 base[2]={0xE000,0xF000};
    if(path<0||path>1)return RTW89_INV_RF_DATA;
    mask&=0xFFFFF; if(!mask)return 0;
    if(addr&RTW89_RF_ADDR_ADSEL)return k8_prdm(sc,base[path]+((addr&0xFF)<<2),mask);
    for(u32 t=0;k8_prd(sc,R_SWSI_V1)&(B_SWSI_W_BUSY_V1|B_SWSI_R_BUSY_V1);t++){ if(t>=30){ g_r89ch.rf_err++; return RTW89_INV_RF_DATA; } sc->io->delay(1); }
    k8_pwm(sc,R_SWSI_READ_ADDR_V1,0x7FFu,((u32)(path&7)<<8)|(addr&0xFF)); sc->io->delay(2);
    for(u32 t=0;!(k8_prd(sc,R_SWSI_V1)&B_SWSI_R_DATA_DONE_V1);t++){ if(t>=30){ g_r89ch.rf_err++; return RTW89_INV_RF_DATA; } sc->io->delay(1); }
    return k8_prdm(sc,R_SWSI_V1,mask); }

u8 rtw89_encode_chan_idx(u8 ch){
    static const u8 b5[4]={36,100,132,149};
    if(ch<=14)return ch&0xF;
    for(int i=3;i>=0;i--) if(ch>=b5[i])return (u8)(((i+2)<<4)|(((ch-b5[i])>>1)&0xF));
    return 0; }

u8 rtw8852b_sco_mapping(u8 c){
    return c==1?109:c<=6?108:c<=10?107:c<=14?106:c<=38?51:c<=58?50:c<=64?49:c<=102?48:c<=126?47:c<=151?46:c<=177?45:0; }

static const u32 k8_barker[14]={0x1cfea,0x1d0e1,0x1d1d7,0x1d2cd,0x1d3c3,0x1d4b9,0x1d5b0,0x1d6a6,0x1d79c,0x1d892,0x1d988,0x1da7f,0x1db75,0x1ddc4};
static const u32 k8_cckth[14]={0x27de3,0x27f35,0x28088,0x281da,0x2832d,0x2847f,0x285d2,0x28724,0x28877,0x289c9,0x28b1c,0x28c6e,0x28dc1,0x290ed};
static const u32 k8_fir_reg[8]={0x2300,0x2304,0x2308,0x230c,0x2310,0x2314,0x2318,0x231c};
static const u32 k8_fir14[8]={0x3b13ff,0x1c42de,0xfdb0ad,0xf60f6e,0xfd8f92,0x2d011,0x1c02c,0xfff00a};
static const u32 k8_fir[8]={0x3d23ff,0x29b354,0xfc1c8,0xfdb053,0xf86f9a,0xfaef92,0xfe5fcc,0xffdff5};

static void k8_mac(struct rtw89_softc* sc,u8 ch){
    k8_m8(sc,0xC010,3,0); sc->io->wr(sc,0xC088,0,4);
    if(ch>14)k8_m8(sc,0xC628,(1u<<4),3); else k8_m8(sc,0xC628,3,(1u<<4)); }

static void k8_reset_all(struct rtw89_softc* sc){
    k8_pwm(sc,0x1200,0x70000000u,7); k8_pwm(sc,0x3200,0x70000000u,7); sc->io->delay(1);
    k8_pwm(sc,0x0704,2,1); k8_pwm(sc,0x0704,2,0);
    k8_pwm(sc,0x1200,0x70000000u,0); k8_pwm(sc,0x3200,0x70000000u,0); k8_pwm(sc,0x0704,2,1); }

static const u32 k8_lna[7][4]={ {0x4678,0x475C,0x45DC,0x4740},{0x4678,0x475C,0x45DC,0x4740},{0x467C,0x4760,0x4660,0x4744},{0x467C,0x4760,0x4660,0x4744},{0x467C,0x4760,0x4660,0x4744},{0x467C,0x4760,0x4660,0x4744},{0x4680,0x4764,0x4664,0x4748} };
static const u32 k8_lna_m[7]={0x00FF0000u,0xFF000000u,0xFFu,0xFF00u,0x00FF0000u,0xFF000000u,0xFFu};
static u32 k8_avg(signed char a,signed char b){ return (u32)((a+b)>>1)&0xFFu; }
static void k8_gain(struct rtw89_softc* sc,u8 ch){
    const struct rtw89_bb_gain* g=&rtw89_gain; if(!g->n)return;
    u8 b=rtw89_gain_band(ch); int g2=ch<=14;
    for(int p=0;p<2;p++){ for(int i=0;i<7;i++)k8_pwm(sc,k8_lna[i][p+(g2?0:2)],k8_lna_m[i],(u8)g->lna[b][p][i]);
        for(int i=0;i<2;i++)k8_pwm(sc,g2?(p?0x4764:0x4680):(p?0x4748:0x4664),i?0xFF000000u:0x00FF0000u,(u8)g->tia[b][p][i]); }
    u32 v1=(k8_avg(g->rpl20[b][0],g->rpl20[b][1])<<8)|(k8_avg(g->rpl40[b][0][0],g->rpl40[b][1][0])<<16)|(k8_avg(g->rpl40[b][0][1],g->rpl40[b][1][1])<<24);
    u32 v2=k8_avg(g->rpl40[b][0][2],g->rpl40[b][1][2])|(k8_avg(g->rpl80[b][0][0],g->rpl80[b][1][0])<<8)|(k8_avg(g->rpl80[b][0][1],g->rpl80[b][1][1])<<16)|(k8_avg(g->rpl80[b][0][10],g->rpl80[b][1][10])<<24);
    u32 v3=k8_avg(g->rpl80[b][0][2],g->rpl80[b][1][2])|(k8_avg(g->rpl80[b][0][3],g->rpl80[b][1][3])<<8)|(k8_avg(g->rpl80[b][0][4],g->rpl80[b][1][4])<<16)|(k8_avg(g->rpl80[b][0][9],g->rpl80[b][1][9])<<24);
    k8_pwm(sc,0x49B0,0xFFFFFF00u,v1>>8); k8_pwm(sc,0x4A00,0xFFFFFF00u,v1>>8);
    k8_pwm(sc,0x49B4,0xFFFFFFFFu,v2); k8_pwm(sc,0x4A04,0xFFFFFFFFu,v2); k8_pwm(sc,0x49B8,0xFFFFFFFFu,v3); k8_pwm(sc,0x4A08,0xFFFFFFFFu,v3); }
static void k8_bb(struct rtw89_softc* sc,u8 ch){
    int g2=ch<=14;
    if(g2){ k8_pwm(sc,0x23B0,0x7FFFFu,k8_barker[ch-1]); k8_pwm(sc,0x23B4,0x7FFFFu,k8_cckth[ch-1]); }
    k8_pwm(sc,0x4738,1u<<17,(u32)g2); k8_pwm(sc,0x4AA4,1u<<17,(u32)g2);
    k8_pwm(sc,0x49C0,0x7Fu,rtw8852b_sco_mapping(ch));
    for(int i=0;i<8;i++)k8_pwm(sc,k8_fir_reg[i],0xFFFFFFu,ch==14?k8_fir14[i]:k8_fir[i]);
    k8_gain(sc,ch);
    u32 rx0=k8_prdm(sc,0x49C4,0xFu);
    k8_pwm(sc,0x49C0,0xC0000000u,0); k8_pwm(sc,0x49C4,0x3000u,0); k8_pwm(sc,0x49C4,0xF00u,0);
    k8_pwm(sc,0x12AC,0xFFF000u,0x333); k8_pwm(sc,0x32AC,0xFFF000u,0x333);
    k8_pwm(sc,0xC0EC,0x6000u,0); k8_pwm(sc,0xC0E4,0x30u,2); k8_pwm(sc,0xC1EC,0x6000u,0); k8_pwm(sc,0xC1E4,0x30u,2);
    if(rx0==1)k8_pwm(sc,0x32AC,0xFFF000u,0x111); else if(rx0==2)k8_pwm(sc,0x12AC,0xFFF000u,0x111);
    k8_pwm(sc,0x0700,1u<<5,(u32)g2); k8_pwm(sc,0x2344,1u<<31,(u32)!g2);
    if(!g2){ k8_pwm(sc,0x4738,1u<<19,0); k8_pwm(sc,0x4738,1u<<22,0); k8_pwm(sc,0x4AA4,1u<<19,0); k8_pwm(sc,0x4AA4,1u<<22,0);
        k8_pwm(sc,0x49C4,1u<<14,0); k8_pwm(sc,0x49C0,0x3C00000u,0); k8_pwm(sc,0x4420,1u<<31,0); k8_pwm(sc,0x0C6C,1u<<21,0); }
    k8_pwm(sc,0x0734,0xFF0000u,rtw89_encode_chan_idx(ch));
    k8_pwm(sc,0x46F8,1u<<12,0); k8_pwm(sc,0x47B8,1u<<12,0); k8_pwm(sc,0x4440,1u<<31,0);
    k8_reset_all(sc); }

static int k8_s0_arfc18(struct rtw89_softc* sc,u32 v){
    u32 bak=rtw89_read_rf(sc,0,RR_LDO,0xFFFFF);
    rtw89_phy_write_rf(sc,0,RR_LDO,0x1C0u,1); rtw89_phy_write_rf(sc,0,RR_CFGCH,0xFFFFF,v);
    int to=1; for(u32 t=0;t<=1000;t++){ u32 b=rtw89_read_rf(sc,0,RR_LPF,1u<<8); if(b==0){ to=0; break; } sc->io->delay(1); }
    if(bak!=RTW89_INV_RF_DATA)rtw89_phy_write_rf(sc,0,RR_LDO,0xFFFFF,bak);
    if(to)g_r89ch.lck_timeouts++;
    return to; }

static void k8_relock(struct rtw89_softc* sc){
    rtw89_phy_write_rf(sc,0,RR_LCK_TRG,1u<<8,1); u32 t=rtw89_read_rf(sc,0,RR_CFGCH,0xFFFFF);
    if(t!=RTW89_INV_RF_DATA)k8_s0_arfc18(sc,t);
    rtw89_phy_write_rf(sc,0,RR_LCK_TRG,1u<<8,0); }

static void k8_lck_check(struct rtw89_softc* sc){
    if(rtw89_read_rf(sc,0,RR_SYNFB,1u<<15)==0){ g_r89ch.lck_fix++;
        rtw89_phy_write_rf(sc,0,RR_MMD,1u<<8,1); rtw89_phy_write_rf(sc,0,RR_MMD,1u<<6,0); rtw89_phy_write_rf(sc,0,RR_MMD,1u<<6,1); rtw89_phy_write_rf(sc,0,RR_MMD,1u<<8,0); }
    sc->io->delay(10);
    if(rtw89_read_rf(sc,0,RR_SYNFB,1u<<15)==0){ g_r89ch.lck_fix++; k8_relock(sc); }
    if(rtw89_read_rf(sc,0,RR_SYNFB,1u<<15)==0){ g_r89ch.lck_fix++;
        u32 t=rtw89_read_rf(sc,0,RR_POW,0xFFFFF); if(t!=RTW89_INV_RF_DATA)rtw89_phy_write_rf(sc,0,RR_POW,0xFFFFF,t);
        t=rtw89_read_rf(sc,0,RR_SX,0xFFFFF); if(t!=RTW89_INV_RF_DATA)rtw89_phy_write_rf(sc,0,RR_SX,0xFFFFF,t);
        rtw89_phy_write_rf(sc,0,RR_SYNLUT,1u<<4,1); rtw89_phy_write_rf(sc,0,RR_POW,0xCu,0); rtw89_phy_write_rf(sc,0,RR_POW,0xCu,3); rtw89_phy_write_rf(sc,0,RR_SYNLUT,1u<<4,0);
        k8_relock(sc); } }

static int k8_ch_setting(struct rtw89_softc* sc,int path,u8 ch,int dav){
    u32 a=dav?RR_CFGCH:RR_CFGCH_V1, r=rtw89_read_rf(sc,path,a,0xFFFFF);
    if(r==RTW89_INV_RF_DATA)return -1;
    r&=~(0x30000u|0x8000u|0x4000u|0x2000u|0x300u|0xFFu); r|=ch;
    if(ch>14)r|=(1u<<16)|(1u<<8);
    r|=0x1000u;
    if(path==0&&dav){ if(!k8_s0_arfc18(sc,r))k8_lck_check(sc); }
    else if(rtw89_phy_write_rf(sc,path,a,0xFFFFF,r)<0)return -1;
    rtw89_phy_write_rf(sc,path,RR_LCKST,1,0); rtw89_phy_write_rf(sc,path,RR_LCKST,1,1);
    return 0; }

static int k8_bw_setting(struct rtw89_softc* sc,int path,int dav){
    u32 a=dav?RR_CFGCH:RR_CFGCH_V1, r=rtw89_read_rf(sc,path,a,0xFFFFF);
    if(r==RTW89_INV_RF_DATA)return -1;
    r=(r&~(0xC00u|0x8000u|0x4000u|0x2000u|0x1000u))|(3u<<10)|0x1000u;
    return rtw89_phy_write_rf(sc,path,a,0xFFFFF,r); }

static int k8_rf(struct rtw89_softc* sc,u8 ch){
    for(int i=0;i<4;i++) if(k8_ch_setting(sc,i&1,ch,i<2)<0)return -1;
    for(int i=0;i<4;i++) if(k8_bw_setting(sc,i&1,i<2)<0)return -1;
    for(int p=0;p<2;p++){ rtw89_phy_write_rf(sc,p,RR_LUTWE2,1u<<2,1); rtw89_phy_write_rf(sc,p,RR_LUTWA,0x1Fu,0x12); rtw89_phy_write_rf(sc,p,RR_LUTWD0,0x3Fu,0x1b); rtw89_phy_write_rf(sc,p,RR_LUTWE2,1u<<2,0); }
    return 0; }

static void k8_help(struct rtw89_softc* sc,int enter,int g2){
    static const u32 trk[2]={0x5818,0x7818}, rst[2]={0x58DC,0x78DC};
    if(enter){ for(int p=0;p<2;p++){ k8_pwm(sc,rst[p],1u<<30,1); k8_pwm(sc,trk[p],1u<<30,1); }
        k8_pwm(sc,0x20fc,0xFF000000u,0xf); sc->io->delay(40);
        k8_pwm(sc,0x2344,1u<<31,1); k8_pwm(sc,0x0C3C,1u<<9,1); k8_pwm(sc,0x1200,0x70000000u,7); k8_pwm(sc,0x3200,0x70000000u,7); sc->io->delay(1); k8_pwm(sc,0x0704,2,0); return; }
    k8_pwm(sc,0x20fc,0xFF000000u,0);
    for(int p=0;p<2;p++){ k8_pwm(sc,rst[p],1u<<30,0); k8_pwm(sc,trk[p],1u<<30,0); }
    k8_pwm(sc,0x1200,0x70000000u,0); k8_pwm(sc,0x3200,0x70000000u,0); k8_pwm(sc,0x0704,2,1);
    if(g2)k8_pwm(sc,0x2344,1u<<31,0);
    k8_pwm(sc,0x0C3C,1u<<9,0); }

int rtw8852b_set_channel(struct rtw89_softc* sc,u8 ch){
    if(!sc||!sc->io||!rtw89_chan_valid(ch))return -1;
    g_r89ch.ok=0; k8_help(sc,1,ch<=14);
    k8_mac(sc,ch); k8_bb(sc,ch); int r=k8_rf(sc,ch);
    k8_help(sc,0,ch<=14);
    if(r<0){ sc->err="RF register 0x18 unreadable during channel switch"; sc->err_reg=R_SWSI_V1; return -1; }
    g_r89ch.ch=ch; g_r89ch.band=rtw89_chan_band(ch); g_r89ch.ok=1; g_r89ch.switches++; return 0; }

static const u32 c8_barker[14]={0x1fe4f,0x1ff5e,0x2006c,0x2017b,0x2028a,0x20399,0x204a8,0x205b6,0x206c5,0x207d4,0x208e3,0x209f2,0x20b00,0x20d8a};
static const u32 c8_cckth[14]={0x2bdac,0x2bf21,0x2c095,0x2c209,0x2c37e,0x2c4f2,0x2c666,0x2c7db,0x2c94f,0x2cac3,0x2cc38,0x2cdac,0x2cf21,0x2d29e};
static const u32 c8_pco[8]={0x45BC,0x45CC,0x45D0,0x45D4,0x45D8,0x45C0,0x45C4,0x45C8};
static const u32 c8_byp_g[7][2]={{0x4BB8,0x4C7C},{0x4BBC,0x4C80},{0x4BBC,0x4C80},{0x4BBC,0x4C80},{0x4BBC,0x4C80},{0x4BC0,0x4C84},{0x4BC0,0x4C84}};
static const u32 c8_byp_a[7][2]={{0x4BB4,0x4C78},{0x4BB4,0x4C78},{0x4BB4,0x4C78},{0x4BB4,0x4C78},{0x4BB8,0x4C7C},{0x4BB8,0x4C7C},{0x4BB8,0x4C7C}};
static const u32 c8_byp_mg[7]={0xFF000000u,0xFFu,0xFF00u,0xFF0000u,0xFF000000u,0xFFu,0xFF00u};
static const u32 c8_byp_ma[7]={0xFFu,0xFF00u,0xFF0000u,0xFF000000u,0xFFu,0xFF00u,0xFF0000u};

u16 rtw89_chan_freq(u8 ch){ return ch<=14?(ch==14?2484:(u16)(2407+5*ch)):(u16)(5000+5*ch); }
u32 rtw8852c_spur_freq(u8 ch){ return (ch>=5&&ch<=8)?2440:ch==13?2480:ch==153?5760:0; }
static int c8_divd(int a,int b,int* r){ int q=a/b, m=a%b; if(m<0){ q--; m+=b; } if(r)*r=m; return q; }
int rtw8852c_csi_tone(u8 ch){ int d=((int)rtw8852c_spur_freq(ch)-(int)rtw89_chan_freq(ch))*1000000, t; c8_divd(c8_divd(d+78125/2,78125,0),2048,&t); return t; }
void rtw8852c_nbi_tone(u8 ch,int* tone,int* frac){ int d=((int)rtw8852c_spur_freq(ch)-(int)rtw89_chan_freq(ch))*1000000, f, i=c8_divd(d,312500,&f); c8_divd(i+32,64,tone); *frac=c8_divd(f+78125/2,78125,0); }

static void c8_gain(struct rtw89_softc* sc,u8 ch){
    const struct rtw89_bb_gain* g=&rtw89_gain; if(!g->n)return;
    u8 b=rtw89_gain_band(ch); int g2=ch<=14;
    for(int p=0;p<2;p++){
        for(int i=0;i<7;i++){ k8_pwm(sc,k8_lna[i][p+(g2?0:2)],k8_lna_m[i],(u8)g->lna[b][p][i]);
            k8_pwm(sc,g2?c8_byp_g[i][p]:c8_byp_a[i][p],g2?c8_byp_mg[i]:c8_byp_ma[i],(u8)g->lnab[b][p][i]);
            if(!g2){ u32 m=0xFFu<<((i&3)*8); k8_pwm(sc,(i<4?0x4668u:0x466Cu)+(p?0xE4u:0),m,(u8)g->op1[b][p][i]); k8_pwm(sc,(i<4?0x4670u:0x4674u)+(p?0xE4u:0),m,(u8)g->tiaop1[b][p][i]); } }
        if(!g2)k8_pwm(sc,p?0x4758:0x4674,0xFF000000u,(u8)g->tiaop1[b][p][7]);
        for(int i=0;i<2;i++)k8_pwm(sc,g2?(p?0x4764:0x4680):(p?0x4748:0x4664),i?0xFF000000u:0x00FF0000u,(u8)g->tia[b][p][i]); } }

static void c8_mac(struct rtw89_softc* sc,u8 ch){
    k8_m8(sc,0xC010,3,0); sc->io->wr(sc,0xC088,0,4);
    k8_m8(sc,0xC628,(1u<<4)|3u,ch>14?3:(1u<<4)); }

static void c8_bb(struct rtw89_softc* sc,u8 ch){
    static const u32 com[12]={0x19999980,0x33333333,0x01209333,0x49249249,0x1C9C9C49,0x39393939,0x39393939,0x39393939,0x00003939,0x000007C0,0xE0000000,0};
    int g2=ch<=14; u16 fc=rtw89_chan_freq(ch);
    if(g2){ k8_pwm(sc,0x4A1C,0x7FFFFu,c8_barker[ch-1]); k8_pwm(sc,0x4A20,0x7FFFFu,c8_cckth[ch-1]); }
    c8_gain(sc,ch);
    k8_pwm(sc,0x4738,1u<<17,(u32)g2); k8_pwm(sc,0x4AA4,1u<<17,(u32)g2); k8_pwm(sc,0x4970,1u<<1,0);
    k8_pwm(sc,0x4E30,0x1FFFu,fc); k8_pwm(sc,0x4974,0x7Fu,((1u<<18)+fc/2)/fc);
    if(g2)for(int i=0;i<8;i++)k8_pwm(sc,c8_pco[i],0xFFFFFFu,ch==14?k8_fir14[i]:k8_fir[i]);
    k8_pwm(sc,0x0734,0xFF0000u,rtw89_encode_chan_idx(ch));
    k8_pwm(sc,0x4974,0xC0000000u,0); k8_pwm(sc,0x4978,0x3000u,0); k8_pwm(sc,0x4978,0xF00u,0);
    k8_pwm(sc,0xC0D4,0x0C000000u,3); k8_pwm(sc,0xC1D4,0x0C000000u,3); k8_pwm(sc,0xC0D8,0x1E0u,0xF); k8_pwm(sc,0xC1D8,0x1E0u,0xF);
    k8_pwm(sc,0x4E30,1u<<26,0); k8_pwm(sc,0x4424,1u<<2,0);
    k8_pwm(sc,0xC0EC,0x6000u,0); k8_pwm(sc,0xC0E4,0x30u,2); k8_pwm(sc,0xC1EC,0x6000u,0); k8_pwm(sc,0xC1E4,0x30u,2);
    k8_pwm(sc,0x0C9C,0xFF000u,0xFF); k8_pwm(sc,0x0CA0,0xFF000u,0);
    k8_pwm(sc,0x0700,1u<<5,(u32)g2); k8_pwm(sc,0x2320,1u,(u32)!g2); k8_pwm(sc,0x0C80,1u<<31,(u32)!g2);
    k8_pwm(sc,0x42C4,1u<<23,0);
    if(rtw8852c_spur_freq(ch)){ int t,f; k8_pwm(sc,0x42AC,0x7FFu,(u32)rtw8852c_csi_tone(ch)); k8_pwm(sc,0x42C4,1u<<23,1); rtw8852c_nbi_tone(ch,&t,&f);
        for(int p=0;p<2;p++){ u32 r=p?0x4CD8:0x4C14; k8_pwm(sc,r,0xFFu,(u32)t); k8_pwm(sc,r,0xC00u,(u32)f); k8_pwm(sc,r,0x1000u,0); k8_pwm(sc,r,0x1000u,1); k8_pwm(sc,r+0xC,0x1000u,0); } }
    else { k8_pwm(sc,0x4C14,0x1000u,0); k8_pwm(sc,0x4CD8,0x1000u,0); }
    k8_pwm(sc,0x49E8,1u<<7,1);
    k8_pwm(sc,0x4738,1u<<19,(u32)g2); k8_pwm(sc,0x4738,1u<<22,0); k8_pwm(sc,0x476C,0xFF000000u,g2?0x20:0x1a); k8_pwm(sc,0x4778,0xFFu,g2?0x30:0x2a);
    k8_pwm(sc,0x4AA4,1u<<19,(u32)g2); k8_pwm(sc,0x4AA4,1u<<22,(u32)g2); k8_pwm(sc,0x0980,0x1E0000u,g2?0:0xF); if(!g2)k8_pwm(sc,0x0980,0x3C000000u,4);
    k8_pwm(sc,0x4978,1u<<14,(u32)g2); k8_pwm(sc,0x4974,0x3C00000u,g2?2:0); k8_pwm(sc,0x441C,1u<<31,(u32)g2); k8_pwm(sc,0x0C6C,1u<<21,(u32)g2);
    k8_pwm(sc,0x4C4C,1u<<12,0); k8_pwm(sc,0x4D10,1u<<12,0); k8_pwm(sc,0x4450,1u<<24,0);
    k8_pwm(sc,0x46C0,1u,1);
    k8_pwm(sc,0x58DC,0xC0000000u,1); k8_pwm(sc,0x58DC,0xC0000000u,3); k8_pwm(sc,0x78DC,0xC0000000u,1); k8_pwm(sc,0x78DC,0xC0000000u,3);
    k8_pwm(sc,0x40F8,1u,0);
    k8_pwm(sc,0x09A4,0x1Cu,0); k8_pwm(sc,0x29A4,0x1Cu,0);
    for(u32 a=0xDC00;a<=0xDDFC;a+=4)sc->io->wr(sc,a,0,4);
    for(int i=0;i<12;i++)sc->io->wr(sc,0xD800+(u32)i*4u,com[i],4);
    k8_reset_all(sc); }

static int c8_rf(struct rtw89_softc* sc,u8 ch){
    for(int p=0;p<2;p++)for(int d=1;d>=0;d--){ u32 a=d?RR_CFGCH:RR_CFGCH_V1, r=rtw89_read_rf(sc,p,a,0xFFFFF); if(r==RTW89_INV_RF_DATA)return -1;
        r=(r&~(0x30000u|0x300u|0xFFu))|ch|(ch>14?(1u<<16)|(1u<<8):0); if(rtw89_phy_write_rf(sc,p,a,0xFFFFF,r)<0)return -1; sc->io->delay(100); }
    for(int p=0;p<2;p++){ for(int d=1;d>=0;d--){ u32 a=d?RR_CFGCH:RR_CFGCH_V1, r=rtw89_read_rf(sc,p,a,0xFFFFF); if(r==RTW89_INV_RF_DATA)return -1;
            k8_pwm(sc,0xC0D4|((u32)p<<8),0x0C000000u,3); k8_pwm(sc,0xC0D8|((u32)p<<8),0x1E0u,0xF);
            if(rtw89_phy_write_rf(sc,p,a,0xFFFFF,(r&~0xC00u)|(3u<<10))<0)return -1; }
        if(p==1&&sc->cv==0){ rtw89_phy_write_rf(sc,1,RR_RSV1,1u,0); u32 t=rtw89_read_rf(sc,0,RR_CFGCH,0xFFFFF);
            rtw89_phy_write_rf(sc,1,RR_APK,0x30u,3); if(t!=RTW89_INV_RF_DATA)rtw89_phy_write_rf(sc,1,RR_CFGCH,0xFFFFF,t); sc->io->delay(100); rtw89_phy_write_rf(sc,1,RR_RSV1,1u,1); } }
    for(int p=0;p<2;p++){ rtw89_phy_write_rf(sc,p,RR_LUTWE2,1u<<2,1); rtw89_phy_write_rf(sc,p,RR_LUTWA,0x1Fu,0xa); rtw89_phy_write_rf(sc,p,RR_LUTWD0,0x3Fu,0x1b); rtw89_phy_write_rf(sc,p,RR_LUTWE2,1u<<2,0);
        rtw89_phy_write_rf(sc,p,RR_RXBB2,0x60u,2); }
    return 0; }

static void c8_help(struct rtw89_softc* sc,int enter,int g2){
    static const u32 trk[2]={0x5818,0x7818}, ten[2]={0x5820,0x7820};
    if(enter){ k8_pwm(sc,0x0000,1u<<31,0); for(int p=0;p<2;p++){ k8_pwm(sc,trk[p],1u<<30,1); k8_pwm(sc,ten[p],1u<<31,1); }
        k8_pwm(sc,0x20fc,0xFF000000u,0xf); sc->io->delay(40);
        k8_pwm(sc,0x2320,1u,1); k8_pwm(sc,0x0C3C,1u<<9,1); k8_pwm(sc,0x1200,0x70000000u,7); k8_pwm(sc,0x3200,0x70000000u,7); sc->io->delay(1); k8_pwm(sc,0x0704,2,0); return; }
    k8_pwm(sc,0x20fc,0xFF000000u,0); k8_pwm(sc,0x0000,1u<<31,1);
    for(int p=0;p<2;p++){ k8_pwm(sc,trk[p],1u<<30,0); k8_pwm(sc,ten[p],1u<<31,0); }
    k8_pwm(sc,0x1200,0x70000000u,0); k8_pwm(sc,0x3200,0x70000000u,0); k8_pwm(sc,0x0704,2,1);
    if(g2)k8_pwm(sc,0x2320,1u,0);
    k8_pwm(sc,0x0C3C,1u<<9,0); }

int rtw8852c_set_channel(struct rtw89_softc* sc,u8 ch){
    if(!sc||!sc->io||!rtw89_chan_valid(ch))return -1;
    g_r89ch.ok=0; c8_help(sc,1,ch<=14);
    c8_mac(sc,ch); c8_bb(sc,ch); int r=c8_rf(sc,ch);
    c8_help(sc,0,ch<=14);
    if(r<0){ sc->err="RF register 0x18 unreadable during channel switch"; sc->err_reg=R_SWSI_V1; return -1; }
    g_r89ch.ch=ch; g_r89ch.band=rtw89_chan_band(ch); g_r89ch.ok=1; g_r89ch.switches++; return 0; }

void rtw8852c_bb_sethw(struct rtw89_softc* sc){
    k8_pwm(sc,0x0A10,1u,1); k8_pwm(sc,0x2A10,1u,1);
    for(int p=0;p<2;p++){ u32 b=p?0x7800:0x5800; k8_pwm(sc,b|0x68,2u,1); k8_pwm(sc,b|0x68,4u,0); k8_pwm(sc,b|0x68,0xE0u,0); k8_pwm(sc,b|0x80,0xFFFFFFFFu,0x77777777u); k8_pwm(sc,b|0x84,0xFFFFFFFFu,0x77777777u); }
    k8_pwm(sc,0x0334,0xFFFFFFFFu,0xFFFFFFFFu); k8_pwm(sc,0x0338,0xFFFFFFFFu,0); k8_pwm(sc,0x033C,0xFFFFFFFFu,0); k8_pwm(sc,0x0340,0xFFFFFFFFu,0);
    for(int p=0;p<2;p++){ u32 b=p?0x7868:0x5868; for(int k=0;k<8;k++)k8_pwm(sc,b,3u<<(16+k*2),(k&3)==0?1u:2u);
        for(int s=0;s<3;s++)k8_pwm(sc,p?0x7894:0x5894,0xFFu<<(s*8),(u32)s*4u); } }

int rtw89_set_channel(struct rtw89_softc* sc,u8 ch){
    if(!sc||sc->state!=RTW89_ST_READY||!sc->phy_ok)return -1;
    if(sc->chip!=RTW89_CHIP_8852B&&sc->chip!=RTW89_CHIP_8852C){ sc->err="channel switch not ported for this chip"; return -1; }
    if(g_r89ch.ok&&g_r89ch.ch==ch)return 0;
    if(sc->chip==RTW89_CHIP_8852C)return rtw8852c_set_channel(sc,ch);
    int r=rtw8852b_set_channel(sc,ch); if(r<0)return r;
    rtw8852b_set_txpwr(sc,ch); rtw8852b_rfk_channel(sc,ch); return 0; }
