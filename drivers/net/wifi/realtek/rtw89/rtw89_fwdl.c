/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/fw.c (rtw89_fw_download, rtw89_fw_check_rdy, rtw89_h2c_pkt_set_hdr, rtw89_fw_c2h_cmd_handle)
 * and mac.c (rtw89_mac_disable_cpu_ax, rtw89_mac_enable_cpu_ax, rtw89_fwdl_check_path_ready_ax, rtw89_mac_c2h_rec_ack/done_ack);
 * GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2019-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"

#define R_AX_SYS_CLK_CTRL 0x0008
#define B_AX_CPU_CLK_EN (1u<<14)
#define B_AX_APB_WRAP_EN (1u<<2)
#define B_AX_WCPU_EN (1u<<1)
#define R_AX_HALT_H2C_CTRL 0x0160
#define R_AX_HALT_C2H_CTRL 0x0164
#define R_AX_HALT_H2C 0x0168
#define R_AX_HALT_C2H 0x016C
#define R_AX_WCPU_FW_CTRL 0x01E0
#define B_AX_WCPU_FWDL_STS_MASK 0xE0u
#define B_AX_FWDL_PATH_RDY (1u<<2)
#define B_AX_H2C_PATH_RDY (1u<<1)
#define B_AX_WCPU_FWDL_EN 1u
#define R_AX_BOOT_REASON 0x01E6
#define B_AX_BOOT_REASON_MASK 0x7u
#define R_AX_UDM1 0x01F4
#define R_AX_UDM2 0x01F8
#define R_AX_SEC_CTRL 0x0C00
#define B_AX_SEC_IDMEM_SIZE_CONFIG_MASK (3u<<16)
#define R_AX_BOOT_DBG 0x83F0
#define FWDL_WAIT_CNT 400000u
#define H2C_CL_MAC_FWDL 3
#define H2C_FUNC_MAC_FWHDR_DL 0
#define RTW89_FWDL_CHECKSUM_FAIL 2
#define RTW89_FWDL_SECURITY_FAIL 3
#define RTW89_FWDL_CV_NOT_MATCH 4
#define RTW89_FWDL_WCPU_FW_INIT_RDY 7

const struct rtw89_hci_ops* rtw89_hci;
rtw89_c2h_cb rtw89_c2h_hook;
static u8 r89_pkt[RTW89_H2C_MAX_LEN];
static u8 r89_c2h[RTW89_H2C_MAX_LEN];

static u32 f_r32(struct rtw89_softc* sc,u32 o){ return sc->io->rd(sc,o,4); }
static void f_w32(struct rtw89_softc* sc,u32 o,u32 v){ sc->io->wr(sc,o,v,4); }
static void f_set32(struct rtw89_softc* sc,u32 o,u32 b){ f_w32(sc,o,f_r32(sc,o)|b); }
static void f_clr32(struct rtw89_softc* sc,u32 o,u32 b){ f_w32(sc,o,f_r32(sc,o)&~b); }
static void f_mask32(struct rtw89_softc* sc,u32 o,u32 m,u32 v){ u32 s=0; while(!((m>>s)&1))s++; f_w32(sc,o,(f_r32(sc,o)&~m)|((v<<s)&m)); }
static void f_put32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static u32 f_le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }

static void r89_h2c_hdr(u8* p,u8 cat,u8 cls,u8 func,u8 seq,u32 len,int rack,int dack){
    f_put32(p,(u32)(cat&3)|((u32)(cls&0x3F)<<2)|((u32)func<<8)|(0u<<16)|((u32)seq<<24));
    f_put32(p+4,((len+RTW89_H2C_HDR_LEN)&0x3FFF)|(rack?(1u<<14):0)|(dack?(1u<<15):0)); }

int rtw89_h2c_send(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,int rack,int dack,const u8* payload,u32 len){
    if(!rtw89_hci||!rtw89_hci->h2c_tx){ sc->err="H2C ring not available"; return -1; }
    if(len>RTW89_H2C_MAX_LEN-RTW89_H2C_HDR_LEN||(len&&!payload))return -2;
    if(!sc->fw_rdy){ sc->err="H2C before firmware ready"; return -3; }
    if(!(sc->h2c_seq%4))rack=1;
    r89_h2c_hdr(r89_pkt,cat,cls,func,sc->h2c_seq,len,rack,dack);
    for(u32 i=0;i<len;i++)r89_pkt[RTW89_H2C_HDR_LEN+i]=payload[i];
    int r=rtw89_hci->h2c_tx(sc,r89_pkt,len+RTW89_H2C_HDR_LEN,RTW89_H2C_PKT_CMD);
    if(r<0){ sc->err="H2C transmit failed"; return -4; }
    sc->h2c_seq++; sc->h2c_cnt++; return 0; }

int rtw89_c2h_handle(struct rtw89_softc* sc,const u8* c,u32 len){
    if(!c||len<RTW89_C2H_HDR_LEN)return -1;
    u32 w0=f_le32(c), w1=f_le32(c+4); u8 cat=(u8)(w0&3), cls=(u8)((w0>>2)&0x3F), func=(u8)(w0>>8); u32 l=w1&0x3FFF;
    if(l<RTW89_C2H_HDR_LEN||l>len)l=len;
    sc->c2h_cnt++;
    if(cat==RTW89_C2H_CAT_MAC&&cls==RTW89_C2H_CL_INFO){
        if((func==RTW89_C2H_FUNC_REC_ACK||func==RTW89_C2H_FUNC_DONE_ACK)&&l<12)return -2;
        if(func==RTW89_C2H_FUNC_REC_ACK)sc->rec_seq=(u8)(f_le32(c+8)>>16);
        else if(func==RTW89_C2H_FUNC_DONE_ACK){ u32 w2=f_le32(c+8); sc->done_cat=(u8)(w2&3); sc->done_cls=(u8)((w2>>2)&0x3F); sc->done_func=(u8)(w2>>8); sc->done_ret=(u8)(w2>>16); sc->done_seq=(u8)(w2>>24); sc->done_cnt++; }
        else if(func==RTW89_C2H_FUNC_LOG){ u32 n=l-RTW89_C2H_HDR_LEN, k=0; for(u32 i=0;i<n&&k<sizeof sc->fwlog-1;i++){ u8 ch=c[RTW89_C2H_HDR_LEN+i]; if(!ch)break; if(ch>=32&&ch<127)sc->fwlog[k++]=(char)ch; } sc->fwlog[k]=0; } }
    if(rtw89_c2h_hook)rtw89_c2h_hook(sc,cat,cls,func,c+RTW89_C2H_HDR_LEN,l-RTW89_C2H_HDR_LEN);
    return 0; }

int rtw89_c2h_poll(struct rtw89_softc* sc,u32 budget){
    if(!rtw89_hci||!rtw89_hci->c2h_rx)return -1;
    int n=0;
    while(budget--){ int r=rtw89_hci->c2h_rx(sc,r89_c2h,sizeof r89_c2h); if(r<0)return r; if(!r)break; if((u32)r>sizeof r89_c2h)r=(int)sizeof r89_c2h; rtw89_c2h_handle(sc,r89_c2h,(u32)r); n++; }
    return n; }

int rtw89_h2c_wait_done(struct rtw89_softc* sc,u8 cls,u8 func,u32 timeout_us){
    u32 start=sc->done_cnt;
    for(u32 t=0;;t+=100){
        if(rtw89_c2h_poll(sc,16)<0)return -1;
        if(sc->done_cnt!=start&&sc->done_cat==RTW89_H2C_CAT_MAC&&sc->done_cls==cls&&sc->done_func==func)return sc->done_ret?-3:0;
        if(t>=timeout_us)break;
        sc->io->delay(100); }
    sc->err="no H2C done-ack from firmware"; return -2; }

static u8 r89_fwdl_sts(struct rtw89_softc* sc){ return (u8)((sc->io->rd(sc,R_AX_WCPU_FW_CTRL,1)&B_AX_WCPU_FWDL_STS_MASK)>>5); }

static int r89_path_ready(struct rtw89_softc* sc,u8 bit){
    for(u32 t=0;;t++){ sc->polls++; if(sc->io->rd(sc,R_AX_WCPU_FW_CTRL,1)&bit)return 0; if(t>=FWDL_WAIT_CNT)break; sc->io->delay(1); }
    sc->err_reg=R_AX_WCPU_FW_CTRL; return -1; }

int rtw89_fw_check_rdy(struct rtw89_softc* sc){
    u8 v=0;
    for(u32 t=0;;t++){ sc->polls++; v=r89_fwdl_sts(sc); if(v==RTW89_FWDL_WCPU_FW_INIT_RDY){ sc->fwdl_sts=v; sc->fw_rdy=1; return 0; } if(t>=FWDL_WAIT_CNT)break; sc->io->delay(1); }
    sc->fwdl_sts=v; sc->err_reg=R_AX_WCPU_FW_CTRL;
    sc->err=v==RTW89_FWDL_CHECKSUM_FAIL?"firmware checksum failed":v==RTW89_FWDL_SECURITY_FAIL?"firmware security check failed":v==RTW89_FWDL_CV_NOT_MATCH?"firmware does not match chip revision":"firmware did not report ready";
    return v==RTW89_FWDL_CHECKSUM_FAIL||v==RTW89_FWDL_SECURITY_FAIL||v==RTW89_FWDL_CV_NOT_MATCH?-2:-1; }

static void r89_disable_cpu(struct rtw89_softc* sc){
    sc->fw_rdy=0;
    f_clr32(sc,R_AX_PLATFORM_ENABLE,B_AX_WCPU_EN);
    f_clr32(sc,R_AX_WCPU_FW_CTRL,B_AX_WCPU_FWDL_EN|B_AX_H2C_PATH_RDY|B_AX_FWDL_PATH_RDY);
    f_clr32(sc,R_AX_SYS_CLK_CTRL,B_AX_CPU_CLK_EN);
    f_clr32(sc,R_AX_PLATFORM_ENABLE,B_AX_APB_WRAP_EN);
    f_set32(sc,R_AX_PLATFORM_ENABLE,B_AX_APB_WRAP_EN);
    f_clr32(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN);
    f_set32(sc,R_AX_PLATFORM_ENABLE,B_AX_PLATFORM_EN); }

static int r89_enable_cpu_dl(struct rtw89_softc* sc){
    if(f_r32(sc,R_AX_PLATFORM_ENABLE)&B_AX_WCPU_EN){ sc->err="WCPU still running after disable"; sc->err_reg=R_AX_PLATFORM_ENABLE; return -1; }
    f_w32(sc,R_AX_UDM1,0); f_w32(sc,R_AX_UDM2,0);
    f_w32(sc,R_AX_HALT_H2C_CTRL,0); f_w32(sc,R_AX_HALT_C2H_CTRL,0); f_w32(sc,R_AX_HALT_H2C,0); f_w32(sc,R_AX_HALT_C2H,0);
    f_set32(sc,R_AX_SYS_CLK_CTRL,B_AX_CPU_CLK_EN);
    u32 v=f_r32(sc,R_AX_WCPU_FW_CTRL); v&=~(B_AX_WCPU_FWDL_EN|B_AX_H2C_PATH_RDY|B_AX_FWDL_PATH_RDY|B_AX_WCPU_FWDL_STS_MASK); v|=B_AX_WCPU_FWDL_EN;
    f_w32(sc,R_AX_WCPU_FW_CTRL,v);
    f_mask32(sc,R_AX_SEC_CTRL,B_AX_SEC_IDMEM_SIZE_CONFIG_MASK,2);
    sc->io->wr(sc,R_AX_BOOT_REASON,(sc->io->rd(sc,R_AX_BOOT_REASON,2)&~B_AX_BOOT_REASON_MASK)&0xFFFF,2);
    f_set32(sc,R_AX_PLATFORM_ENABLE,B_AX_WCPU_EN);
    return 0; }

static int r89_dl_hdr(struct rtw89_softc* sc,const u8* img){
    const struct rtw89_fw_info* fi=&sc->fw; u32 len=32+16*(u32)fi->nsec;
    if(len+RTW89_H2C_HDR_LEN>sizeof r89_pkt)return -1;
    for(u32 i=0;i<len;i++)r89_pkt[RTW89_H2C_HDR_LEN+i]=img[i];
    u8* w7=r89_pkt+RTW89_H2C_HDR_LEN+28; u32 v=(f_le32(w7)&0xFFFF0000u)|RTW89_FWDL_PART_SIZE; f_put32(w7,v);
    r89_h2c_hdr(r89_pkt,RTW89_H2C_CAT_MAC,H2C_CL_MAC_FWDL,H2C_FUNC_MAC_FWHDR_DL,sc->h2c_seq,len,0,0);
    if(rtw89_hci->h2c_tx(sc,r89_pkt,len+RTW89_H2C_HDR_LEN,RTW89_H2C_PKT_CMD)<0){ sc->err="firmware header H2C transmit failed"; return -1; }
    if(r89_path_ready(sc,B_AX_FWDL_PATH_RDY)<0){ sc->err="firmware download path not ready"; return -1; }
    f_w32(sc,R_AX_HALT_H2C_CTRL,0); f_w32(sc,R_AX_HALT_C2H_CTRL,0);
    return 0; }

static int r89_dl_main(struct rtw89_softc* sc,const u8* img){
    for(int i=0;i<sc->fw.nsec;i++){ const struct rtw89_fw_section* s=&sc->fw.sec[i]; const u8* p=img+s->off; u32 left=s->len;
        while(left){ u32 n=left<RTW89_FWDL_PART_SIZE?left:RTW89_FWDL_PART_SIZE;
            for(u32 k=0;k<n;k++)r89_pkt[k]=p[k];
            if(rtw89_hci->h2c_tx(sc,r89_pkt,n,RTW89_H2C_PKT_FWDL)<0){ sc->err="firmware section transmit failed"; return -1; }
            p+=n; left-=n; } }
    return 0; }

static int r89_fwdl_once(struct rtw89_softc* sc,const u8* img){
    r89_disable_cpu(sc);
    if(r89_enable_cpu_dl(sc)<0)return -1;
    if(r89_path_ready(sc,B_AX_H2C_PATH_RDY)<0){ sc->err="H2C path not ready"; return -1; }
    if(r89_dl_hdr(sc,img)<0||r89_dl_main(sc,img)<0)return -1;
    sc->h2c_seq=0; sc->rec_seq=0; sc->h2c_cnt=0; sc->c2h_cnt=0;
    sc->io->delay(5000);
    return rtw89_fw_check_rdy(sc); }

int rtw89_fw_download(struct rtw89_softc* sc,const u8* d,u32 len){
    if(!rtw89_hci||!rtw89_hci->h2c_tx){ sc->err="firmware download needs the PCIe H2C ring (not set up)"; return -1; }
    if(!sc->fw.nsec||sc->fw.off>len||sc->fw.size>len-sc->fw.off){ sc->err="firmware not parsed"; return -1; }
    if(sc->fw.hdr_ver){ sc->err="firmware header v1 download not ported"; return -1; }
    const u8* img=d+sc->fw.off;
    for(int i=0;i<sc->fw.nsec;i++) if(sc->fw.sec[i].mssc){ sc->err="secure-boot firmware sections not supported"; return -1; }
    if(rtw89_hci->pre_fwdl&&rtw89_hci->pre_fwdl(sc)<0){ if(!sc->err)sc->err="DMA pre-init for firmware download failed"; return -1; }
    int r=-1;
    for(sc->fwdl_tries=0;sc->fwdl_tries<5;){ sc->fwdl_tries++; sc->err=0; sc->err_reg=0; r=r89_fwdl_once(sc,img); if(!r)return 0; }
    sc->fw_ctrl_dump=f_r32(sc,R_AX_WCPU_FW_CTRL); sc->boot_dbg_dump=f_r32(sc,R_AX_BOOT_DBG);
    if(!sc->err)sc->err="firmware download failed";
    return r; }
