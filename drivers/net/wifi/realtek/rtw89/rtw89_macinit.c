/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/mac.c (rtw89_mac_dmac_pre_init, rtw89_mac_hci_func_en_ax/dmac_func_pre_en_ax incl. the RTL8852C HAXI path, rtw89_mac_dle_init, rtw89_mac_hfc_init,
 * sys_init_ax, dmac_init_ax, cmac_init_ax (scheduler, addr CAM, RX filter, CCA + _patch_dis_resp_chk, NAV, spatial reuse, TMAC, TRXPTCL, RMAC incl. BA CAM reset, CMAC common, PTCL, CMAC DMA), set_host_rpr_ax, rtw89_mac_set_rx_fltr), rtw8852b.c and rtw8852c.c (DLE/HFC quota tables, rtw89_mac_size wde_size18/19, ple_size18/19, wde_qt17/18, ple_qt44-47) and
 * rtw8852b_common.c (__rtw8852bx_mac_enable_bb_rf); GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_mac.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"

struct m3_dle { u16 wpg,wlnk,wunl,ppg,plnk,punl; u16 wmin[4],wmax[4],pmin[12],pmax[12]; };
static const struct m3_dle m3_dle_8852b[2]={
    { 64,510,2,128,496,16,{446,48,0,16},{446,48,0,16},{147,0,16,20,17,13,89,0,32,14,8},{147,0,16,20,157,13,229,0,172,14,24} },
    { 64,0,1024,128,64,960,{0,0,0,0},{0,0,0,0},{0,0,16,48,0,0,0,0,0,0,0},{0,0,16,48,0,0,0,0,0,0,0} } };
static const struct m3_dle m3_dle_8852c[2]={
    { 64,3328,0,128,1904,16,{3228,60,0,40},{3228,60,0,40},{525,0,16,20,13,13,178,0,32,62,8,16},{525,0,32,20,1034,13,1199,0,1053,62,160,1037} },
    { 64,0,2048,128,2544,16,{0,0,0,0},{0,0,0,0},{0,0,16,256,0,0,0,0,0,0,0,0},{0,0,32,256,0,0,0,0,0,0,0,0} } };
static const u16 m3_hfc_ch[12][3]={ {5,341,0},{5,341,0},{4,342,0},{4,342,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{4,342,0},{4,342,0},{0,0,0},{0,0,0} };
#define M3_DMA_CH_MASK ((1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<10)|(1u<<11))
#define M3_PUB_G0 446
#define M3_PUB_MAX 446
#define M3_PREC_CH011 2
#define M3_PREC_H2C 40
#define M3_FULL_CH011 1
#define M3_FIFO 196608u
#define M3_SCC_RSVD 98304u
#define M3_FIFO_C 458752u
#define M3_HCI_FC_CTRL_V1 0x1700
#define M3_CH_PAGE_CTRL_V1 0x1704
#define M3_H_AXIDMA_EN (1u<<14)
#define M3_AXIDMA_EN (1u<<3)
#define M3_DMA_MODE_MASK 0xC0000u
#define M3_HAXI_STOP1 0x1010
#define M3_HAXI_STOP1_ALL 0x7FF00u
#define M3_PUB_C 1614u
#define M3_PUB_MAX_C 3228u
#define M3_SEC_DEBUG1 0x9D1C
#define M3_PRELD_CFG0 0x9F48
#define M3_PRELD_CFG1 0x9F4C
#define M3_AFE_OFF_CTRL1 0x0444

static u32 m3_r32(struct rtw89_softc* sc,u32 o){ return sc->io->rd(sc,o,4); }
static void m3_w32(struct rtw89_softc* sc,u32 o,u32 v){ sc->io->wr(sc,o,v,4); }
static void m3_set(struct rtw89_softc* sc,u32 o,u32 b){ m3_w32(sc,o,m3_r32(sc,o)|b); }
static void m3_clr(struct rtw89_softc* sc,u32 o,u32 b){ m3_w32(sc,o,m3_r32(sc,o)&~b); }
static u32 m3_rep(u32 v,u32 m,u32 x){ u32 s=0; while(!((m>>s)&1))s++; return (v&~m)|((x<<s)&m); }
static void m3_mask(struct rtw89_softc* sc,u32 o,u32 m,u32 x){ m3_w32(sc,o,m3_rep(m3_r32(sc,o),m,x)); }
static int m3_poll(struct rtw89_softc* sc,u32 o,u32 m,u32 want,u32 step,u32 total,const char* why){
    for(u32 t=0;;t+=step){ sc->polls++; u32 v=m3_r32(sc,o); if(v!=0xFFFFFFFFu&&(v&m)==want)return 0; if(t>=total)break; sc->io->delay(step); }
    sc->err_reg=o; sc->err=why; return -1; }

u32 rtw89_bar_size(u32 probe){
    u32 m=probe&0xFFFFFFF0u; if(!m||(probe&1))return 0;
    u32 sz=~m+1u; if(sz&(sz-1))return 0;
    return sz<RTW89_BAR_MIN?0:sz>RTW89_BAR_MAX?RTW89_BAR_MAX:sz; }

static int m3_dmac_on(struct rtw89_softc* sc){ u32 v=m3_r32(sc,R_AX_DMAC_FUNC_EN); if(v==0xFFFFFFFFu||(v&(B_AX_MAC_FUNC_EN|B_AX_DMAC_FUNC_EN))!=(B_AX_MAC_FUNC_EN|B_AX_DMAC_FUNC_EN)){ sc->err="MAC init: DMAC is not enabled"; sc->err_reg=R_AX_DMAC_FUNC_EN; return -1; } return 0; }

int rtw89_mac_dle_init(struct rtw89_softc* sc,int mode){
    if(mode!=RTW89_QTA_SCC&&mode!=RTW89_QTA_DLFW)return -1;
    int cc=sc->chip==RTW89_CHIP_8852C; const struct m3_dle* t=cc?m3_dle_8852c:m3_dle_8852b; const struct m3_dle* c=&t[mode];
    u32 used=(u32)c->wpg*(c->wlnk+c->wunl)+(u32)c->ppg*(c->plnk+c->punl);
    if(used!=(cc?M3_FIFO_C:mode==RTW89_QTA_SCC?M3_FIFO-M3_SCC_RSVD:M3_FIFO)){ sc->err="MAC init: DLE memory layout does not fit the FIFO"; return -1; }
    if(m3_dmac_on(sc)<0)return -1;
    m3_clr(sc,R_AX_DMAC_FUNC_EN,RTW89_DLE_EN);
    m3_set(sc,R_AX_DMAC_CLK_EN,RTW89_DLE_EN);
    u32 v=m3_r32(sc,R_AX_WDE_PKTBUF_CFG); v=m3_rep(v,0x3u,0); v=m3_rep(v,0x3F00u,0); v=m3_rep(v,0x1FFF0000u,c->wlnk); m3_w32(sc,R_AX_WDE_PKTBUF_CFG,v);
    u32 bound=(u32)(c->wlnk+c->wunl)*c->wpg/8192u;
    v=m3_r32(sc,R_AX_PLE_PKTBUF_CFG); v=m3_rep(v,0x3u,1); v=m3_rep(v,0x3F00u,bound); v=m3_rep(v,0x1FFF0000u,c->plnk); m3_w32(sc,R_AX_PLE_PKTBUF_CFG,v);
    static const u8 widx[4]={0,1,3,4};
    for(int i=0;i<4;i++){ u16 mn=c->wmin[i]; if(mode==RTW89_QTA_DLFW&&i==1)mn=t[RTW89_QTA_SCC].wmin[1]; m3_w32(sc,R_AX_WDE_QTA0_CFG+widx[i]*4u,(mn&0xFFFu)|((u32)(c->wmax[i]&0xFFFu)<<16)); }
    for(int i=0;i<(cc?12:11);i++)m3_w32(sc,R_AX_PLE_QTA0_CFG+(u32)i*4u,(c->pmin[i]&0xFFFu)|((u32)(c->pmax[i]&0xFFFu)<<16));
    m3_set(sc,R_AX_DMAC_FUNC_EN,RTW89_DLE_EN);
    if(m3_poll(sc,R_AX_WDE_INI_STATUS,3,3,1,2000,"MAC init: WDE did not become ready")<0||m3_poll(sc,R_AX_PLE_INI_STATUS,3,3,1,2000,"MAC init: PLE did not become ready")<0){ m3_clr(sc,R_AX_DMAC_FUNC_EN,RTW89_DLE_EN); return -1; }
    return 0; }

static u32 m3_fc(struct rtw89_softc* sc){ return sc->chip==RTW89_CHIP_8852C?M3_HCI_FC_CTRL_V1:R_AX_HCI_FC_CTRL; }
static void m3_hfc_func(struct rtw89_softc* sc,int en,int h2c){ u32 r=m3_fc(sc), v=m3_r32(sc,r); v=en?(v|B_AX_HCI_FC_EN):(v&~B_AX_HCI_FC_EN); v=h2c?(v|B_AX_HCI_FC_CH12_EN):(v&~B_AX_HCI_FC_CH12_EN); m3_w32(sc,r,v); }

int rtw89_mac_hfc_init(struct rtw89_softc* sc,int reset,int en,int h2c_en){
    (void)reset; if(m3_dmac_on(sc)<0)return -1;
    m3_hfc_func(sc,0,0);
    if(!en&&h2c_en){ m3_w32(sc,sc->chip==RTW89_CHIP_8852C?M3_CH_PAGE_CTRL_V1:R_AX_CH_PAGE_CTRL,(u32)M3_PREC_H2C<<16); m3_mask(sc,m3_fc(sc),B_AX_HCI_FC_CH12_FULL_COND_MASK,0); m3_hfc_func(sc,en,h2c_en); return 0; }
    if(sc->chip!=RTW89_CHIP_8852B&&sc->chip!=RTW89_CHIP_8852C){ sc->err="MAC init: host flow control not ported for this chip"; return -1; }
    int cc=sc->chip==RTW89_CHIP_8852C; u32 o=cc?0x1710u-R_AX_ACH0_PAGE_CTRL:0;
    for(u32 ch=0;ch<12;ch++){ if(!cc&&(M3_DMA_CH_MASK&(1u<<ch)))continue; u32 mn=cc?13u:m3_hfc_ch[ch][0], mx=cc?M3_PUB_C:m3_hfc_ch[ch][1], g=cc?((ch>=4&&ch<=7)||ch>=10):m3_hfc_ch[ch][2];
        m3_w32(sc,R_AX_ACH0_PAGE_CTRL+o+ch*4u,(mn&0x1FFFu)|((mx&0x1FFFu)<<16)|(g?(1u<<31):0)); }
    m3_w32(sc,R_AX_PUB_PAGE_CTRL1+o,cc?(M3_PUB_C|(M3_PUB_C<<16)):M3_PUB_G0);
    m3_w32(sc,R_AX_WP_PAGE_CTRL2+o,0);
    m3_w32(sc,cc?M3_CH_PAGE_CTRL_V1:R_AX_CH_PAGE_CTRL,M3_PREC_CH011|((u32)M3_PREC_H2C<<16));
    m3_w32(sc,R_AX_PUB_PAGE_CTRL2+o,cc?M3_PUB_MAX_C:M3_PUB_MAX);
    m3_w32(sc,R_AX_WP_PAGE_CTRL1+o,0);
    u32 v=m3_r32(sc,m3_fc(sc)); v=m3_rep(v,B_AX_HCI_FC_MODE_MASK,0); v=m3_rep(v,B_AX_HCI_FC_WD_FULL_COND_MASK,M3_FULL_CH011); v=m3_rep(v,B_AX_HCI_FC_CH12_FULL_COND_MASK,0);
    v=m3_rep(v,B_AX_HCI_FC_WP_CH07_FULL_COND_MASK,0); v=m3_rep(v,B_AX_HCI_FC_WP_CH811_FULL_COND_MASK,0); m3_w32(sc,m3_fc(sc),v);
    if(en||h2c_en){ m3_hfc_func(sc,en,h2c_en); sc->io->delay(10); }
    return 0; }

int rtw89_mac_dmac_pre_init(struct rtw89_softc* sc){
    if(sc->chip!=RTW89_CHIP_8852B&&sc->chip!=RTW89_CHIP_8852C){ sc->err="MAC init for this chip not ported yet"; return -1; }
    int cc=sc->chip==RTW89_CHIP_8852C;
    m3_w32(sc,R_AX_DMAC_FUNC_EN,RTW89_DMAC_FUNC_PRE|(cc?M3_H_AXIDMA_EN:0));
    m3_w32(sc,R_AX_DMAC_CLK_EN,B_AX_DISPATCHER_EN);
    if(cc){ m3_w32(sc,R_AX_HAXI_INIT_CFG1,(m3_r32(sc,R_AX_HAXI_INIT_CFG1)&~(M3_DMA_MODE_MASK|B_AX_STOP_AXI_MST))|B_AX_TXHCI_EN_V1|B_AX_RXHCI_EN_V1);
        m3_clr(sc,M3_HAXI_STOP1,M3_HAXI_STOP1_ALL); m3_clr(sc,R_AX_HAXI_DMA_STOP2,3u); m3_set(sc,R_AX_PLATFORM_ENABLE,M3_AXIDMA_EN); }
    if(rtw89_mac_dle_init(sc,RTW89_QTA_DLFW)<0)return -1;
    return rtw89_mac_hfc_init(sc,1,0,1); }

int rtw89_mac_enable_bb_rf(struct rtw89_softc* sc){
    if(sc->chip==RTW89_CHIP_8852C){ sc->io->wr(sc,R_AX_SYS_FUNC_EN,(sc->io->rd(sc,R_AX_SYS_FUNC_EN,1)|B_AX_FEN_BBRSTB|B_AX_FEN_BB_GLB_RSTN)&0xFF,1);
        m3_set(sc,R_AX_WLRF_CTRL,B_AX_AFC_AFEDIG); m3_clr(sc,R_AX_WLRF_CTRL,B_AX_AFC_AFEDIG); m3_set(sc,R_AX_WLRF_CTRL,B_AX_AFC_AFEDIG);
        m3_mask(sc,M3_AFE_OFF_CTRL1,0x600000u,1); m3_mask(sc,M3_AFE_OFF_CTRL1,0x3000000u,1);
        static const u8 xs[5][2]={{0x00,0x07},{XTAL_SI_ANAPAR_WL,0x6C},{XTAL_SI_WL_RFC_S0,0xC7},{XTAL_SI_WL_RFC_S1,0xC7},{0x03,0x0D}};
        for(int i=0;i<5;i++) if(rtw89_write_xtal_si(sc,xs[i][0],xs[i][1],0xFF)<0)return -1;
        return 0; }
    sc->io->wr(sc,R_AX_SYS_FUNC_EN,(sc->io->rd(sc,R_AX_SYS_FUNC_EN,1)|B_AX_FEN_BBRSTB|B_AX_FEN_BB_GLB_RSTN)&0xFF,1);
    m3_mask(sc,R_AX_SPS_DIG_ON_CTRL0,B_AX_REG_ZCDC_H_MASK,1);
    m3_set(sc,R_AX_WLRF_CTRL,B_AX_AFC_AFEDIG); m3_clr(sc,R_AX_WLRF_CTRL,B_AX_AFC_AFEDIG); m3_set(sc,R_AX_WLRF_CTRL,B_AX_AFC_AFEDIG);
    if(rtw89_write_xtal_si(sc,XTAL_SI_WL_RFC_S0,0xC7,0xFF)<0||rtw89_write_xtal_si(sc,XTAL_SI_WL_RFC_S1,0xC7,0xFF)<0)return -1;
    sc->io->wr(sc,R_AX_PHYREG_SET,PHYREG_SET_XYN_CYCLE,1);
    return 0; }

int rtw89_mac_sys_init(struct rtw89_softc* sc){
    int cc=sc->chip==RTW89_CHIP_8852C;
    m3_w32(sc,R_AX_DMAC_FUNC_EN,RTW89_DMAC_FUNC_RUN|(cc?M3_H_AXIDMA_EN:0));
    m3_w32(sc,R_AX_DMAC_CLK_EN,RTW89_DMAC_CLK_ALL);
    m3_set(sc,R_AX_CK_EN,RTW89_CMAC_CK_RUN);
    m3_set(sc,R_AX_CMAC_FUNC_EN,RTW89_CMAC_FUNC_RUN);
    if(!cc)m3_set(sc,R_AX_SPS_DIG_ON_CTRL0,B_AX_OCP_L1_MASK);
    return 0; }

static int m3_dmac_init(struct rtw89_softc* sc){
    int cc=sc->chip==RTW89_CHIP_8852C;
    if(rtw89_mac_dle_init(sc,RTW89_QTA_SCC)<0)return -1;
    if(cc){ m3_mask(sc,M3_PRELD_CFG0,0x3FF0000u,10u*52u); m3_set(sc,M3_PRELD_CFG0,1u<<31); m3_mask(sc,M3_PRELD_CFG1,0xF00u,1); m3_mask(sc,M3_PRELD_CFG1,0xFFu,52); }
    if(rtw89_mac_hfc_init(sc,1,1,1)<0)return -1;
    sc->io->wr(sc,R_AX_SS_CTRL,(sc->io->rd(sc,R_AX_SS_CTRL,1)|B_AX_SS_EN)&0xFF,1);
    if(m3_poll(sc,R_AX_SS_CTRL,B_AX_SS_INIT_DONE_1,B_AX_SS_INIT_DONE_1,1,2000,"MAC init: STA scheduler did not start")<0)return -1;
    m3_set(sc,R_AX_SS_CTRL,B_AX_SS_WARM_INIT_FLG); m3_clr(sc,R_AX_SS_CTRL,B_AX_SS_NONEMPTY_SS2FINFO_EN);
    m3_w32(sc,R_AX_ACTION_FWD0,TRXCFG_MPDU_PROC_ACT_FRWD); m3_w32(sc,R_AX_TF_FWD,TRXCFG_MPDU_PROC_TF_FRWD);
    m3_set(sc,R_AX_MPDU_PROC,3u); m3_w32(sc,R_AX_CUT_AMSDU_CTRL,TRXCFG_MPDU_PROC_CUT_CTRL);
    u32 v=m3_r32(sc,R_AX_SEC_ENG_CTRL); v|=(1u<<10)|(1u<<9)|(1u<<8)|1u|2u|8u|4u; if(cc)v|=1u<<4; else v&=~(1u<<11); m3_w32(sc,R_AX_SEC_ENG_CTRL,v);
    m3_set(sc,R_AX_SEC_MPDU_PROC,3u);
    if(cc)m3_mask(sc,M3_SEC_DEBUG1,0xC0000000u,2);
    return 0; }

static int m3_cmac_init(struct rtw89_softc* sc){
    u32 v=m3_r32(sc,R_AX_CMAC_FUNC_EN); if(v==0xFFFFFFFFu||!(v&(1u<<30))){ sc->err="MAC init: CMAC is not enabled"; sc->err_reg=R_AX_CMAC_FUNC_EN; return -1; }
    int cc=sc->chip==RTW89_CHIP_8852C;
    m3_mask(sc,0xC33C,0x7Fu,cc?0x41:0x47); if(!cc)m3_set(sc,0xC3FC,1u<<1); m3_clr(sc,0xC340,1u<<5); if(!cc||!(m3_r32(sc,R_AX_SEC_ENG_CTRL)&(1u<<11)))m3_mask(sc,0xC338,0x1Fu,0x18);
    m3_w32(sc,R_AX_ADDR_CAM_CTRL,m3_r32(sc,R_AX_ADDR_CAM_CTRL)|(0x7Fu<<16)|B_AX_ADDR_CAM_CLR|B_AX_ADDR_CAM_EN);
    if(m3_poll(sc,R_AX_ADDR_CAM_CTRL,B_AX_ADDR_CAM_CLR,0,1,2000,"MAC init: address CAM reset did not finish")<0)return -1;
    m3_w32(sc,R_AX_MGNT_FLTR,RX_FLTR_FRAME_TO_HOST); m3_w32(sc,R_AX_CTRL_FLTR,RX_FLTR_FRAME_TO_HOST); m3_w32(sc,R_AX_DATA_FLTR,RX_FLTR_FRAME_TO_HOST);
    m3_w32(sc,R_AX_RX_FLTR_OPT,RTW89_DEFAULT_RX_FLTR);
    sc->io->wr(sc,R_AX_PLCP_HDR_FLTR,RTW89_PLCP_FLTR,2);
    m3_w32(sc,R_AX_CCA_CONTROL,(m3_r32(sc,R_AX_CCA_CONTROL)|RTW89_CCA_SET)&~RTW89_CCA_CLR);
    if(cc){ m3_set(sc,0xCC00,(1u<<19)|(1u<<20)|(1u<<21)); m3_set(sc,0xCC04,0x1F800000u); } else { m3_clr(sc,0xCC00,(1u<<19)|(1u<<20)|(1u<<21)); m3_clr(sc,0xCC04,0x1F800000u); }
    m3_set(sc,0xCC80,(1u<<17)|(1u<<16)|(1u<<26)); m3_mask(sc,0xCC80,0xFF00u,0xC4);
    sc->io->wr(sc,0xCE4A,sc->io->rd(sc,0xCE4A,1)&0xFEu,1); sc->io->wr(sc,0xCE4B,(sc->io->rd(sc,0xCE4B,1)|1u)&0xFFu,1);
    m3_clr(sc,0xCC20,1u); m3_mask(sc,0xCA00,0x7F0000u,6); m3_mask(sc,0xCA1C,0xF000u,7); m3_mask(sc,0xCA1C,0xF00u,7);
    m3_w32(sc,0xCC04,m3_rep(m3_rep(m3_r32(sc,0xCC04),0xFFu,0xA),0xFF00u,0x11)); m3_set(sc,0xCCB0,1u<<20); m3_mask(sc,0xCC08,1u<<9,0); m3_mask(sc,0xCC08,0xC00u,2);
    m3_mask(sc,0xCE3C,3u,2); { const char* e=sc->err; u32 er=sc->err_reg; if(m3_poll(sc,0xCE3C,3u,0,1,1000,0)<0)sc->bacam_to++; sc->err=e; sc->err_reg=er; } sc->io->wr(sc,0xCE3C,(sc->io->rd(sc,0xCE3C,1)|4u)&0xFFu,1);
    sc->io->wr(sc,0xCE02,(sc->io->rd(sc,0xCE02,2)&~0xFFF0u)|(15u<<4)|(32u<<8),2);
    sc->io->wr(sc,R_AX_RCR,(sc->io->rd(sc,R_AX_RCR,1)&~0xFu)|1u,1);
    u32 q=(cc?m3_dle_8852c:m3_dle_8852b)[RTW89_QTA_SCC].pmin[6]; if(q>127)q=127; u32 ml=q*128u; if(ml>11454u+512u)ml=11454u+512u;
    m3_mask(sc,R_AX_RX_FLTR_OPT,B_AX_RX_MPDU_MAX_LEN_MASK,ml/512u);
    sc->io->wr(sc,R_AX_PLCP_HDR_FLTR,sc->io->rd(sc,R_AX_PLCP_HDR_FLTR,1)&~0x10u&0xFF,1);
    m3_clr(sc,0xC088,0xFFFu); if(!cc)m3_mask(sc,0xC090,0xF00u,3);
    m3_w32(sc,0xC624,m3_rep(m3_rep(m3_r32(sc,0xC624),0xFF000000u,4),0xFC0000u,1)|(1u<<16));
    m3_w32(sc,0xC6E8,m3_rep(m3_r32(sc,0xC6E8),0x3Fu,0x3F)&~(1u<<6));
    sc->io->wr(sc,0xC600,(sc->io->rd(sc,0xC600,1)|3u)&~0x1Cu&0xFFu,1); sc->io->wr(sc,0xC660,m3_rep(sc->io->rd(sc,0xC660,1),0x30u,1)&0xFFu,1);
    if(!cc){ m3_mask(sc,0xC618,0xFFFFFu,0x3FF80); sc->io->wr(sc,0xC804,sc->io->rd(sc,0xC804,1)&~0x3Fu&0xFFu,1); }
    return 0; }

int rtw89_mac_trx_init(struct rtw89_softc* sc){
    if(m3_dmac_init(sc)<0||m3_cmac_init(sc)<0)return -1;
    m3_mask(sc,0x9408,3u,0); m3_set(sc,0x9410,0x0F000000u); m3_mask(sc,0x9414,0xFFu,30); m3_mask(sc,0x9414,0xFF0000u,255);
    if(sc->chip==RTW89_CHIP_8852C)m3_clr(sc,0xCC00,1u<<30);
    return 0; }

void rtw89_mac_set_rx_fltr(struct rtw89_softc* sc,u32 f){ u32 v=m3_r32(sc,R_AX_RX_FLTR_OPT); m3_w32(sc,R_AX_RX_FLTR_OPT,(v&B_AX_RX_MPDU_MAX_LEN_MASK)|(f&~B_AX_RX_MPDU_MAX_LEN_MASK)); }

int rtw89_mac_init_post(struct rtw89_softc* sc){
    if(sc->chip!=RTW89_CHIP_8852B&&sc->chip!=RTW89_CHIP_8852C){ sc->err="MAC init for this chip not ported yet"; return -1; }
    if(rtw89_mac_enable_bb_rf(sc)<0)return -1;
    if(rtw89_mac_sys_init(sc)<0)return -1;
    return rtw89_mac_trx_init(sc); }
