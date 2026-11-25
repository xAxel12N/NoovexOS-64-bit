/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/efuse.c (rtw89_dump_physical_efuse_map_ddv, rtw89_dump_logical_efuse_map, rtw89_parse_efuse_map_ax)
 * and rtw8852b_common.c/.h (__rtw8852bx_read_efuse, struct rtw8852bx_efuse); GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"

#define R_AX_SYS_WL_EFUSE_CTRL 0x000A
#define B_AX_AUTOLOAD_SUS (1u<<5)
#define R_AX_EFUSE_CTRL 0x0030
#define B_AX_EF_RDY (1u<<29)
#define B_AX_EF_ADDR_MASK 0x07FF0000u
#define R_AX_EFUSE_CTRL_1_V1 0x0038
#define B_AX_EF_BURST (1u<<19)
#define R8852B_PHY_EFUSE 1216u
#define R8852B_LOG_EFUSE 2048u
#define R8852B_SEC_CTRL 4u
#define R8852B_EF_XTAL 0x2B9
#define R8852B_EF_RFE 0x2CA
#define R8852B_EF_COUNTRY 0x2CB
#define R8852B_EF_MAC_PCIE 0x400

static u8 e_phy[R8852B_PHY_EFUSE];
static u8 e_log[R8852B_LOG_EFUSE];

static void e_set(struct rtw89_softc* sc,u32 o,u32 b,int w){ sc->io->wr(sc,o,sc->io->rd(sc,o,w)|b,w); }
static void e_clr(struct rtw89_softc* sc,u32 o,u32 b,int w){ u32 m=w==1?0xFFu:w==2?0xFFFFu:0xFFFFFFFFu; sc->io->wr(sc,o,sc->io->rd(sc,o,w)&~b&m,w); }

static void e_pwr_cut(struct rtw89_softc* sc,int on){
    int burst=sc->chip==RTW89_CHIP_8852B&&sc->cv==0;
    if(on){ e_set(sc,R_AX_PMC_DBG_CTRL2,B_AX_SYSON_DIS_PMCR_AX_WRMSK,1); e_set(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B14,2); sc->io->delay(1000);
        e_set(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B15,2); e_clr(sc,R_AX_SYS_ISO_CTRL,B_AX_ISO_EB2CORE,2); if(burst)e_set(sc,R_AX_EFUSE_CTRL_1_V1,B_AX_EF_BURST,4); return; }
    if(burst)e_clr(sc,R_AX_EFUSE_CTRL_1_V1,B_AX_EF_BURST,4);
    e_set(sc,R_AX_SYS_ISO_CTRL,B_AX_ISO_EB2CORE,2); e_clr(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B15,2); sc->io->delay(1000);
    e_clr(sc,R_AX_SYS_ISO_CTRL,B_AX_PWC_EV2EF_B14,2); e_clr(sc,R_AX_PMC_DBG_CTRL2,B_AX_SYSON_DIS_PMCR_AX_WRMSK,1); }

static int e_dump_phy(struct rtw89_softc* sc,u8* map,u32 size){
    e_pwr_cut(sc,1);
    for(u32 a=0;a<size;a++){
        sc->io->wr(sc,R_AX_EFUSE_CTRL,(a<<16)&B_AX_EF_ADDR_MASK&~B_AX_EF_RDY,4);
        u32 v=0,t=0;
        for(;;t++){ sc->efuse_reads++; v=sc->io->rd(sc,R_AX_EFUSE_CTRL,4); if(v&B_AX_EF_RDY)break; if(t>=10000){ sc->err_reg=R_AX_EFUSE_CTRL; e_pwr_cut(sc,0); return -1; } sc->io->delay(1); }
        map[a]=(u8)v; }
    e_pwr_cut(sc,0);
    return 0; }

int rtw89_efuse_logical(const u8* phy,u32 phy_size,u32 sec_ctrl,u8* log,u32 log_size){
    for(u32 i=0;i<log_size;i++)log[i]=0xFF;
    u32 p=sec_ctrl;
    while(p<phy_size-sec_ctrl){
        u8 h1=phy[p],h2=phy[p+1]; if(h1==0xFF||h2==0xFF)break;
        u32 blk=((h2&0xF0u)>>4)|((h1&0x0Fu)<<4); u8 we=h2&0xF; p+=2;
        for(u32 i=0;i<4;i++){ if(we&(1u<<i))continue; u32 l=(blk<<3)+(i<<1);
            if(p+1>phy_size-sec_ctrl-1||l+1>log_size)return -1;
            log[l]=phy[p]; log[l+1]=phy[p+1]; p+=2; } }
    return 0; }

static int e_mac_ok(const u8* m){ int z=1,f=1; for(int i=0;i<6;i++){ if(m[i])z=0; if(m[i]!=0xFF)f=0; } return !z&&!f&&!(m[0]&1); }

int rtw89_efuse_read(struct rtw89_softc* sc){
    if(sc->chip!=RTW89_CHIP_8852B){ sc->err="efuse layout for this chip not ported"; return -1; }
    sc->efuse_ok=0;
    sc->efuse_valid=(sc->io->rd(sc,R_AX_SYS_WL_EFUSE_CTRL,2)&B_AX_AUTOLOAD_SUS)?1:0;
    int r=-1;
    for(int k=0;k<5&&r<0;k++)r=e_dump_phy(sc,e_phy,R8852B_PHY_EFUSE);
    if(r<0){ sc->err="efuse read timed out"; return -1; }
    if(rtw89_efuse_logical(e_phy,R8852B_PHY_EFUSE,R8852B_SEC_CTRL,e_log,R8852B_LOG_EFUSE)<0){ sc->err="efuse logical map is corrupt"; return -2; }
    const u8* m=e_log+R8852B_EF_MAC_PCIE;
    if(!e_mac_ok(m)){ sc->err="efuse has no valid MAC address"; return -3; }
    for(int i=0;i<6;i++)sc->mac[i]=m[i];
    sc->rfe_type=e_log[R8852B_EF_RFE]; sc->xtal_cap=e_log[R8852B_EF_XTAL]; sc->country[0]=e_log[R8852B_EF_COUNTRY]; sc->country[1]=e_log[R8852B_EF_COUNTRY+1];
    sc->efuse_ok=1; return 0; }
