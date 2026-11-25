/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/phy.c (rtw89_phy_load_txpwr_byrate, rtw89_phy_raw_byr_seek, rtw89_phy_read_txpwr_byrate,
 * rtw89_channel_to_idx, rtw89_phy_read_txpwr_limit, rtw89_phy_read_txpwr_limit_ru, rtw89_phy_fill_txpwr_limit_20m_ax,
 * rtw89_phy_fill_txpwr_limit_ru_20m_ax, rtw89_phy_set_txpwr_byrate_ax, rtw89_phy_set_txpwr_offset_ax, rtw89_phy_set_txpwr_limit_ax,
 * rtw89_phy_set_txpwr_limit_ru_ax), core.h (rtw89_regd_get), regd.c (rtw89_regd_map), rtw8852b_common.c (rtw8852bx_bb_cal_txpwr_ref,
 * rtw8852bx_set_txpwr_ref, __rtw8852bx_set_txpwr) and rtw8852b_table.c via rtw8852b_txpwr_tbl.h; GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2024  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_txpwr.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_chan.h"
#include "include/nvx/net/wifi/regdb.h"
#include "drivers/net/wifi/realtek/rtw89/rtw8852b_txpwr_tbl.h"

struct rtw89_txpwr_state g_r89tx;

void rtw8852b_byr_load(struct rtw89_byr b[2]){
    u8* z=(u8*)b; for(u32 i=0;i<2*sizeof *b;i++)z[i]=0;
    for(u32 i=0;i<sizeof rtw8852b_byr_cfg/sizeof rtw8852b_byr_cfg[0];i++){ const u32* c=rtw8852b_byr_cfg[i]; u32 d=c[5];
        if(c[0]>1||c[1]>1)continue;
        struct rtw89_byr* h=&b[c[0]];
        for(u32 k=0;k<c[4];k++,d>>=8){ u32 x=c[3]+k; signed char v=(signed char)(d&0xFF);
            switch(c[2]){ case 0: if(x<4)h->cck[x]=v; break; case 1: if(x<8)h->ofdm[x]=v; break; case 2: if(x<12)h->mcs[c[1]][x]=v; break;
            case 3: if(x<4)h->hedcm[c[1]][x]=v; break; case 4: if(x<5)h->offset[x]=v; break; } } } }

u8 rtw89_txpwr_regd_of(const char* cc,u8 band){
    if(!cc||!cc[0]||!cc[1]||band>1)return RTW89_TXP_WW;
    for(u32 i=0;rtw8852b_cc_regd[i];i+=4) if(rtw8852b_cc_regd[i]==cc[0]&&rtw8852b_cc_regd[i+1]==cc[1]){ u8 r=(u8)(rtw8852b_cc_regd[i+2+band]-'A'); if(r>=RTW89_TXP_NREGD)return RTW89_TXP_WW; return r==RTW89_TXP_UK?RTW89_TXP_ETSI:r; }
    return RTW89_TXP_WW; }
u8 rtw89_txpwr_regd(u8 band){ return rtw89_txpwr_regd_of(regdb_country(),band); }

static int t9_chidx(u8 band,u8 ch){
    if(!rtw89_chan_valid(ch)||(ch>14)!=(band==1))return -1;
    if(!band)return ch>=1&&ch<=14?ch-1:-1;
    if(ch>=36&&ch<=64)return (ch-36)/2;
    if(ch>=100&&ch<=144)return (ch-100)/2+15;
    if(ch>=149&&ch<=177)return (ch-149)/2+38;
    return -1; }
static signed char t9_raw(u8 band,u8 bw,u8 ntx,u8 rs,u8 bf,u8 r,int ci){
    if(!band)return rtw8852b_lmt_2g[((((bw*2+ntx)*3+rs)*2+bf)*RTW89_TXP_NREGD+r)*14+ci];
    return bw?0:rtw8852b_lmt_5g[(((ntx*3+rs)*2+bf)*RTW89_TXP_NREGD+r)*53+ci]; }
static int t9_bad(signed char v){ return v==0||v==127; }
static signed char t9_mac(int v,u8 ch){
    v>>=1;
    int d=regdb_max_dbm(ch); if(d>0&&d*2<v)v=d*2;
    return (signed char)(v>63?63:v<-64?-64:v); }
signed char rtw8852b_txpwr_limit(u8 regd,u8 band,u8 bw,u8 ntx,u8 rs,u8 bf,u8 ch){
    int ci=t9_chidx(band,ch); if(ci<0||band>1||ntx>1||rs>2||bf>1||bw>(band?0:1))return 0;
    if(regd>=RTW89_TXP_NREGD)regd=RTW89_TXP_WW;
    signed char v=t9_raw(band,bw,ntx,rs,bf,regd,ci);
    if(t9_bad(v))v=t9_raw(band,bw,ntx,rs,bf,RTW89_TXP_WW,ci);
    if(t9_bad(v)){ int n=band?53:14, m=127; for(int i=0;i<n;i++){ signed char w=t9_raw(band,bw,ntx,rs,bf,RTW89_TXP_WW,i); if(!t9_bad(w)&&w<m)m=w; } v=(signed char)(m==127?0:m); }
    return t9_mac(v,ch); }
signed char rtw8852b_txpwr_limit_ru(u8 regd,u8 band,u8 ru,u8 ntx,u8 ch){
    int ci=t9_chidx(band,ch); if(ci<0||band>1||ru>2||ntx>1)return 0;
    if(regd>=RTW89_TXP_NREGD)regd=RTW89_TXP_WW;
    #define T9RU(r,i) (band?rtw8852b_lmt_ru_5g[((ru*2+ntx)*RTW89_TXP_NREGD+(r))*53+(i)]:rtw8852b_lmt_ru_2g[((ru*2+ntx)*RTW89_TXP_NREGD+(r))*14+(i)])
    signed char v=T9RU(regd,ci);
    if(t9_bad(v))v=T9RU(RTW89_TXP_WW,ci);
    if(t9_bad(v)){ int n=band?53:14, m=127; for(int i=0;i<n;i++){ signed char w=T9RU(RTW89_TXP_WW,i); if(!t9_bad(w)&&w<m)m=w; } v=(signed char)(m==127?0:m); }
    #undef T9RU
    return t9_mac(v,ch); }

void rtw8852b_fill_limit(u8 regd,u8 ch,u8 ntx,signed char o[RTW89_TXP_LMT_PAGE]){
    u8 band=ch>14; for(int i=0;i<RTW89_TXP_LMT_PAGE;i++)o[i]=0;
    for(u8 bf=0;bf<2;bf++){ o[bf]=rtw8852b_txpwr_limit(regd,band,0,ntx,0,bf,ch); o[2+bf]=rtw8852b_txpwr_limit(regd,band,1,ntx,0,bf,ch);
        o[4+bf]=rtw8852b_txpwr_limit(regd,band,0,ntx,1,bf,ch); o[6+bf]=rtw8852b_txpwr_limit(regd,band,0,ntx,2,bf,ch); } }
void rtw8852b_fill_limit_ru(u8 regd,u8 ch,u8 ntx,signed char o[RTW89_TXP_RU_PAGE]){
    u8 band=ch>14; for(int i=0;i<RTW89_TXP_RU_PAGE;i++)o[i]=0;
    for(u8 r=0;r<3;r++)o[r*8]=rtw8852b_txpwr_limit_ru(regd,band,r,ntx,ch); }

u32 rtw8852b_txpwr_ref_val(int ref,u16 dec){
    int s=(ref<<1)+(0x27<<3)-dec; u32 bb=(u32)s&7; int rf=(int)(((u32)s>>3)&0x3F); if(rf<15)rf=15; if(rf>63)rf=63;
    u32 pcw=((u32)rf<<3)|bb, tcw=(u32)(0x12c+(ref<<1)-(16<<3))-dec;
    return ((tcw&0x1FFu)<<18)|((pcw&0x1FFu)<<9)|((u32)ref&0x1FFu); }

static u32 t9_pack(const signed char* p){ return (u32)(u8)p[0]|((u32)(u8)p[1]<<8)|((u32)(u8)p[2]<<16)|((u32)(u8)p[3]<<24); }
static signed char t9_min(signed char a,signed char b){ return a<b?a:b; }
static void t9_m32(struct rtw89_softc* sc,u32 a,u32 m,u32 v){ sc->io->wr(sc,a,(sc->io->rd(sc,a,4)&~m)|(v&m),4); }
static void t9_pm(struct rtw89_softc* sc,u32 a,u32 m,u32 v){ a|=RTW89_PHY_ADDR_OFFSET; sc->io->wr(sc,a,(sc->io->rd(sc,a,4)&~m)|(v&m),4); }

int rtw8852b_set_txpwr(struct rtw89_softc* sc,u8 ch){
    if(!sc||!sc->io||!rtw89_chan_valid(ch))return -1;
    u8 band=ch>14, regd=rtw89_txpwr_regd(band); struct rtw89_txpwr_state* s=&g_r89tx; s->ok=0;
    static struct rtw89_byr byr[2]; rtw8852b_byr_load(byr);
    for(u8 n=0;n<2;n++){ rtw8852b_fill_limit(regd,ch,n,s->lmt[n]); rtw8852b_fill_limit_ru(regd,ch,n,s->ru[n]); }
    struct rtw89_byr* b=&s->byr; *b=byr[band];
    for(int i=0;i<4;i++)b->cck[i]=(signed char)(byr[0].cck[i]>>1);
    for(int i=0;i<8;i++)b->ofdm[i]=(signed char)(b->ofdm[i]>>1);
    for(int n=0;n<2;n++){ for(int i=0;i<12;i++)b->mcs[n][i]=(signed char)(b->mcs[n][i]>>1); for(int i=0;i<4;i++)b->hedcm[n][i]=(signed char)(b->hedcm[n][i]>>1); }
    for(int i=0;i<5;i++)b->offset[i]=(signed char)(b->offset[i]>>1);
    signed char lc=s->lmt[0][0], lo=s->lmt[0][4];
    for(int i=0;i<4;i++)b->cck[i]=t9_min(b->cck[i],lc);
    for(int i=0;i<8;i++)b->ofdm[i]=t9_min(b->ofdm[i],lo);
    for(int n=0;n<2;n++){ signed char lm=s->lmt[n][6]; for(int i=0;i<12;i++)b->mcs[n][i]=t9_min(b->mcs[n][i],lm); for(int i=0;i<4;i++)b->hedcm[n][i]=t9_min(b->hedcm[n][i],lm); }
    u32 a=R_AX_PWR_BY_RATE;
    sc->io->wr(sc,a,t9_pack(b->cck),4); a+=4;
    for(int i=0;i<8;i+=4,a+=4)sc->io->wr(sc,a,t9_pack(b->ofdm+i),4);
    for(int n=0;n<2;n++){ for(int i=0;i<12;i+=4,a+=4)sc->io->wr(sc,a,t9_pack(b->mcs[n]+i),4); sc->io->wr(sc,a,t9_pack(b->hedcm[n]),4); a+=4; }
    u32 ov=0; for(int i=0;i<5;i++)ov|=((u32)b->offset[i]&0xF)<<(4*i);
    t9_m32(sc,R_AX_PWR_RATE_OFST_CTRL,0xFFFFFu,ov);
    a=R_AX_PWR_LMT; for(int n=0;n<2;n++)for(int j=0;j<RTW89_TXP_LMT_PAGE;j+=4,a+=4)sc->io->wr(sc,a,t9_pack(s->lmt[n]+j),4);
    a=R_AX_PWR_RU_LMT; for(int n=0;n<2;n++)for(int j=0;j<RTW89_TXP_RU_PAGE;j+=4,a+=4)sc->io->wr(sc,a,t9_pack(s->ru[n]+j),4);
    t9_m32(sc,R_AX_PWR_RATE_CTRL,0x0FFFFC00u|(1u<<9),0);
    u32 rv=rtw8852b_txpwr_ref_val(0,0);
    for(int p=0;p<2;p++){ t9_pm(sc,(p?0x7800:0x5800)+4,0x7FFFFFFu,rv); t9_pm(sc,(p?0x7800:0x5800)+8,0x7FFFFFFu,rv); }
    s->ch=ch; s->regd=regd; s->ok=1; s->applies++;
    return 0; }

int rtw8852b_rx_dck(struct rtw89_softc* sc){
    int bad=0;
    for(int p=0;p<2;p++){ u32 t=0,m; while((m=rtw89_read_rf(sc,p,0x00,0xF0000u))==2&&t<5000){ sc->io->delay(2); t+=2; } if(m==RTW89_INV_RF_DATA)bad=1; }
    for(int p=0;p<2&&!bad;p++){
        u32 r5=rtw89_read_rf(sc,p,0x05,0xFFFFF), tune=rtw89_read_rf(sc,p,0x92,2u);
        if(r5==RTW89_INV_RF_DATA||tune==RTW89_INV_RF_DATA){ bad=1; break; }
        rtw89_phy_write_rf(sc,p,0x05,1u,0); rtw89_phy_write_rf(sc,p,0x92,2u,0); rtw89_phy_write_rf(sc,p,0x00,0xF0000u,3);
        rtw89_phy_write_rf(sc,p,0x93,0xFu,0); rtw89_phy_write_rf(sc,p,0x92,1u,0); rtw89_phy_write_rf(sc,p,0x92,1u,1); sc->io->delay(1000);
        rtw89_phy_write_rf(sc,p,0x92,2u,tune); rtw89_phy_write_rf(sc,p,0x05,0xFFFFF,r5); }
    return bad?-1:0; }

int rtw8852b_rfk_channel(struct rtw89_softc* sc,u8 ch){
    (void)ch; g_r89tx.rfk_on=0;
    if(!sc||!sc->io)return -1;
    if(rtw8852b_rx_dck(sc)<0){ g_r89tx.rfk_off="RX DCK skipped: RF registers unreadable; IQK, DPK and TSSI not ported"; return -1; }
    g_r89tx.rfk_on=1; g_r89tx.rfk_off="IQK, DPK and TSSI not ported: not run (RX DCK done)";
    return 0; }
