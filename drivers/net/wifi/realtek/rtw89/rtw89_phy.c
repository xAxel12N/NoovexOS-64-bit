/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/phy.c (rtw89_phy_sel_headline, rtw89_phy_init_reg, rtw89_phy_config_bb_reg,
 * rtw89_phy_config_rf_reg_v1, rtw89_phy_write_rf_v1, rtw89_phy_config_bb_gain_ax, rtw89_phy_cfg_bb_gain_error, rtw89_phy_cfg_bb_rpl_ofst, rtw89_phy_cfg_bb_gain_bypass, rtw89_phy_cfg_bb_gain_op1db), fw.c and fw.h (scan offload H2C layout); GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"

static u32 y_g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void y_p32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static u32 y_prd(struct rtw89_softc* sc,u32 a){ return sc->io->rd(sc,a|RTW89_PHY_ADDR_OFFSET,4); }
static void y_pwr(struct rtw89_softc* sc,u32 a,u32 v){ sc->io->wr(sc,a|RTW89_PHY_ADDR_OFFSET,v,4); }
static void y_pmask(struct rtw89_softc* sc,u32 a,u32 m,u32 v){ if(m==0xFFFFFFFFu){ y_pwr(sc,a,v); return; } u32 s=0; while(!((m>>s)&1))s++; y_pwr(sc,a,(y_prd(sc,a)&~m)|((v<<s)&m)); }

struct rtw89_bb_gain rtw89_gain;
u8 rtw89_gain_band(u8 ch){ return ch<=14?0:ch<=64?1:ch<=144?2:3; }
void rtw89_bb_gain_cfg(struct rtw89_bb_gain* g,u32 addr,u32 d){
    u8 ty=(u8)addr, rs=ty&0xF, bw=ty>>4, p=(u8)(addr>>8), gb=(u8)(addr>>16), cf=(u8)(addr>>24);
    if(gb>=4||p>=2)return;
    if(cf==0){ if(ty==0)for(int i=0;i<4;i++,d>>=8)g->lna[gb][p][i]=(signed char)d; else if(ty==1)for(int i=4;i<7;i++,d>>=8)g->lna[gb][p][i]=(signed char)d; else if(ty==2)for(int i=0;i<2;i++,d>>=8)g->tia[gb][p][i]=(signed char)d; else return; g->n++; return; }
    if(cf==2){ if(ty==0)for(int i=0;i<4;i++,d>>=8)g->lnab[gb][p][i]=(signed char)d; else if(ty==1)for(int i=4;i<7;i++,d>>=8)g->lnab[gb][p][i]=(signed char)d; return; }
    if(cf==3){ if(ty<2)for(int i=ty*4;i<ty*4+4-ty;i++,d>>=8)g->op1[gb][p][i]=(signed char)d; else if(ty==2)for(int i=0;i<4;i++,d>>=8)g->tiaop1[gb][p][i]=(signed char)d; else if(ty==3)for(int i=4;i<8;i++,d>>=8)g->tiaop1[gb][p][i]=(signed char)d; return; }
    if(cf!=1)return;
    if(bw==0)g->rpl20[gb][p]=(signed char)d;
    else if(bw==1){ if(rs==0)g->rpl40[gb][p][0]=(signed char)d; else if(rs==1)for(int i=0;i<2;i++,d>>=8)g->rpl40[gb][p][1+i]=(signed char)d; else return; }
    else if(bw==2){ if(rs==0)g->rpl80[gb][p][0]=(signed char)d; else if(rs==1)for(int i=0;i<4;i++,d>>=8)g->rpl80[gb][p][1+i]=(signed char)d; else if(rs==9)for(int i=0;i<2;i++,d>>=8)g->rpl80[gb][p][9+i]=(signed char)d; else return; }
    else return;
    g->n++; }

int rtw89_phy_tables_parse(const u8* d,u32 len,struct rtw89_phy_tbls* out){
    out->nt=0; if(!d||len<8||d[0]!='R'||d[1]!='8'||d[2]!='9'||d[3]!='P')return -1;
    u32 nt=y_g32(d+4), off=8; if(nt>6)return -2;
    for(u32 i=0;i<nt;i++){ if(len-off<8)return -3; u8 k=d[off]; u32 n=y_g32(d+off+4); off+=8;
        if(n>(len-off)/8||!k||k>RTW89_TBL_NCTL)return -4;
        out->t[i].kind=k; out->t[i].n=n; out->t[i].regs=(const struct rtw89_reg2*)(const void*)(d+off); off+=n*8; }
    if(off!=len)return -5;
    out->nt=nt; return 0; }

int rtw89_phy_sel_headline(const struct rtw89_phy_tbl* t,u8 rfe,u8 cv,u32* hsize,u32* hidx){
    u32 i; *hidx=0;
    for(i=0;i<t->n;i++) if((t->regs[i].addr>>28)!=0xF)break;
    *hsize=i; if(!i)return 0;
    u32 cmp=((u32)rfe<<16)|cv;
    for(i=0;i<*hsize;i++) if((t->regs[i].addr&0x0FFFFFFFu)==cmp){ *hidx=i; return 0; }
    cmp=((u32)rfe<<16)|0xFF;
    for(i=0;i<*hsize;i++) if((t->regs[i].addr&0x0FFFFFFFu)==cmp){ *hidx=i; return 0; }
    for(int pass=0;pass<2;pass++){ u8 want=pass?0xFF:rfe; int hit=0; u8 cvmax=0;
        for(i=0;i<*hsize;i++){ u32 a=t->regs[i].addr; if(((a>>16)&0xFF)==want&&(a&0xFF)>=cvmax){ cvmax=(u8)(a&0xFF); *hidx=i; hit=1; } }
        if(hit)return 0; }
    return -1; }

int rtw89_phy_write_rf(struct rtw89_softc* sc,int path,u32 addr,u32 mask,u32 data){
    static const u32 base[2]={0xE000,0xF000};
    if(path<0||path>1)return -1;
    mask&=0xFFFFF;
    if(addr&RTW89_RF_ADDR_ADSEL){ y_pmask(sc,base[path]+((addr&0xFF)<<2),mask,data); sc->io->delay(1); return 0; }
    for(u32 t=0;;t++){ if(!(y_prd(sc,R_SWSI_V1)&(B_SWSI_W_BUSY_V1|(1u<<25))))break; if(t>=30){ sc->err="RF serial interface busy"; sc->err_reg=R_SWSI_V1; return -1; } sc->io->delay(1); }
    u32 en=0;
    if(mask!=0xFFFFF){ en=1; y_pmask(sc,R_SWSI_BIT_MASK_V1,0xFFFFF,mask); u32 s=0; while(!((mask>>s)&1))s++; data=(data<<s)&0xFFFFF; }
    y_pwr(sc,R_SWSI_DATA_V1,(en<<31)|((u32)(path&7)<<28)|((addr&0xFF)<<20)|(data&0xFFFFF));
    return 0; }

static void y_delay(struct rtw89_softc* sc,u32 a){
    static const u32 us[]={1,5,50,1000,5000,50000}; sc->io->delay(us[a-0xF9]); }

int rtw89_phy_init_reg(struct rtw89_softc* sc,const struct rtw89_phy_tbl* t,u8 rfe,u8 cv,int path){
    u32 hs,hi; if(rtw89_phy_sel_headline(t,rfe,cv,&hs,&hi)<0){ sc->err="PHY table has no entry for this board (rfe/cv)"; return -1; }
    u32 cfg=hs?(t->regs[hi].addr&0x0FFFFFFFu):0, target=0, wr=0; int matched=1, found=0;
    for(u32 i=hs;i<t->n;i++){ u32 a=t->regs[i].addr, v=t->regs[i].data;
        switch(a>>28){
        case 0x8: case 0x9: target=a&0x0FFFFFFFu; break;
        case 0xA: matched=0; if(!found){ sc->err="PHY table branch without match"; return -1; } break;
        case 0xB: matched=1; found=0; break;
        case 0x4: if(found){ matched=0; break; } matched=found=(target==cfg); break;
        default:
            if(!matched)break;
            if(path==-2){ rtw89_bb_gain_cfg(&rtw89_gain,a,v); wr++; }
            else if(path<0){ if(a>=0xF9&&a<=0xFE)y_delay(sc,a); else if(v!=RTW89_BYPASS_CR_DATA){ y_pwr(sc,a,v); wr++; } }
            else if(a==0xFE)sc->io->delay(50000);
            else { if(rtw89_phy_write_rf(sc,path,a,0xFFFFF,v)<0)return -1; wr++; }
        } }
    return (int)wr; }

int rtw89_phy_load_8852b(struct rtw89_softc* sc,const u8* blob,u32 len){
    struct rtw89_phy_tbls tb; if(rtw89_phy_tables_parse(blob,len,&tb)<0){ sc->err="PHY table file is corrupt"; return -1; }
    static const u8 order[]={RTW89_TBL_BB,RTW89_TBL_BB_GAIN,RTW89_TBL_RADIOA,RTW89_TBL_RADIOB,RTW89_TBL_NCTL};
    for(unsigned o=0;o<sizeof order;o++) for(u32 i=0;i<tb.nt;i++){ if(tb.t[i].kind!=order[o])continue;
        int path=order[o]==RTW89_TBL_BB_GAIN?-2:order[o]==RTW89_TBL_RADIOA?0:order[o]==RTW89_TBL_RADIOB?1:-1;
        if(path==-2){ u8* z=(u8*)&rtw89_gain; for(u32 k=0;k<sizeof rtw89_gain;k++)z[k]=0; }
        if(rtw89_phy_init_reg(sc,&tb.t[i],sc->rfe_type,sc->cv,path)<0)return -1; }
    return 0; }

int rtw89_chan_valid(u8 ch){
    if(ch>=1&&ch<=14)return 1;
    if(ch>=36&&ch<=64)return !(ch%4);
    if(ch>=100&&ch<=144)return !(ch%4);
    if(ch>=149&&ch<=177)return (ch%4)==1;
    return 0; }
u8 rtw89_chan_band(u8 ch){ return ch<=14?0:1; }
static int y_dfs(u8 ch){ return ch>=52&&ch<=144; }

u32 rtw89_build_chinfo(u8* out,u32 cap,const u8* ch,int n,u8 probe_id){
    u32 cnt=0; if(cap<4)return 0;
    for(int i=0;i<n&&cnt<RTW89_SCAN_MAX_CH;i++){ u8 c=ch[i]; if(!rtw89_chan_valid(c))continue; if(4+(cnt+1)*RTW89_CHINFO_ELEM_SIZE>cap)break;
        u8* e=out+4+cnt*RTW89_CHINFO_ELEM_SIZE; for(int k=0;k<RTW89_CHINFO_ELEM_SIZE;k++)e[k]=0;
        int dfs=y_dfs(c); u32 period=dfs?RTW89_SCAN_DFS_TIME:RTW89_SCAN_CH_TIME;
        y_p32(e,period|((u32)RTW89_SCAN_DWELL<<8)|((u32)c<<16)|((u32)c<<24));
        y_p32(e+4,0u|(0x1Fu<<3)|((u32)rtw89_chan_band(c)<<14)|((u32)(dfs?RTW89_SCANOFLD_PKT_NONE:probe_id)<<16)|((u32)dfs<<24));
        y_p32(e+8,0xFFFFFFFFu); y_p32(e+12,0xFFFFFFFFu);
        cnt++; }
    out[0]=(u8)cnt; out[1]=RTW89_CHINFO_ELEM_SIZE/4; out[2]=0; out[3]=0;
    return cnt?4+cnt*RTW89_CHINFO_ELEM_SIZE:0; }

u32 rtw89_build_scanofld(u8* out,u8 macid,u8 port,u8 band,int enable){
    for(int k=0;k<RTW89_SCANOFLD_SIZE;k++)out[k]=0;
    y_p32(out,(u32)macid|((u32)(port&7)<<16)|((u32)(band&1)<<19)|((u32)(enable?1:0)<<20));
    y_p32(out+4,1u);
    return RTW89_SCANOFLD_SIZE; }

static u8 y_scan_buf[4+RTW89_SCAN_MAX_CH*RTW89_CHINFO_ELEM_SIZE];
int rtw89_hw_scan_start(struct rtw89_softc* sc,const u8* ch,int n){
    u32 l=rtw89_build_chinfo(y_scan_buf,sizeof y_scan_buf,ch,n,RTW89_SCANOFLD_PKT_NONE); if(!l)return -2;
    if(rtw89_h2c_send(sc,RTW89_H2C_CAT_MAC,H2C_CL_MAC_FW_OFLD,H2C_FUNC_ADD_SCANOFLD_CH,1,1,y_scan_buf,l)<0)return -1;
    if(rtw89_h2c_wait_done(sc,H2C_CL_MAC_FW_OFLD,H2C_FUNC_ADD_SCANOFLD_CH,RTW89_SCAN_ACK_US)<0)return -3;
    u8 so[RTW89_SCANOFLD_SIZE]; rtw89_build_scanofld(so,0,0,0,1);
    return rtw89_h2c_send(sc,RTW89_H2C_CAT_MAC,H2C_CL_MAC_FW_OFLD,H2C_FUNC_SCANOFLD,1,1,so,sizeof so); }
int rtw89_hw_scan_stop(struct rtw89_softc* sc){ u8 so[RTW89_SCANOFLD_SIZE]; rtw89_build_scanofld(so,0,0,0,0); return rtw89_h2c_send(sc,RTW89_H2C_CAT_MAC,H2C_CL_MAC_FW_OFLD,H2C_FUNC_SCANOFLD,1,1,so,sizeof so); }
