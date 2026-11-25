/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Derived from Linux drivers/net/wireless/realtek/rtw89/pci.c, pci.h, txrx.h, core.c (TX WD / RX descriptor fill and parse)
 * and rtw8852be.c (ring and BD RAM layout); GPL-2.0 OR BSD-3-Clause, used under BSD-3-Clause.
 * Copyright(c) 2020-2022  Realtek Corporation
 * Licence text: drivers/net/wifi/realtek/rtw89/LICENCE.BSD-3-Clause
 */
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"
#include "include/nvx/net/wifi/wifi_hw.h"
#include "include/nvx/mem/pmm_vmm.h"

struct r89p_state g_r89p;
void* (*rtw89_dma_pages)(unsigned long pages)=dma_alloc_pages;
void (*rtw89_rx_frame)(const u8* frame,int len,int rssi)=wifi_hw_rx;

static const struct r89p_regs r89p_txr[R89_TX_N]={ {0x1024,0x1058,0x1200,0x1110},{0x1034,0x1078,0x1220,0x1150},{0x1038,0x1080,0x1228,0x1160} };
static const u8 r89p_bdram[R89_TX_N][3]={ {0,5,2},{20,4,1},{28,4,1} };
static const u8 r89p_txlen[R89_TX_N]={ 32,16,16 };
static const struct r89p_regs r89p_rxr[R89_RX_N]={ {0x1020,0x1050,0,0x1100},{0x1022,0x1054,0,0x1108} };
static const u8 r89p_rxlen[R89_RX_N]={ 32,16 };
static const struct r89p_regs r89p_txr_v1[R89_TX_N]={ {0x1024,0x1058,0x1300,0x1230},{0x1034,0x1078,0x1320,0x1270},{0x1038,0x1080,0x1328,0x1280} };
static const u8 r89p_bdram_v1[R89_TX_N][3]={ {0,5,2},{40,5,1},{60,4,1} };
static const struct r89p_regs r89p_rxr_v1[R89_RX_N]={ {0x1210,0x1218,0,0x1220},{0x1212,0x121C,0,0x1228} };
#define R89P_TXBUF 2304u

static u32 p_r32(struct rtw89_softc* sc,u32 o){ return sc->io->rd(sc,o,4); }
static void p_w32(struct rtw89_softc* sc,u32 o,u32 v){ sc->io->wr(sc,o,v,4); }
static void p_w16(struct rtw89_softc* sc,u32 o,u32 v){ sc->io->wr(sc,o,v&0xFFFF,2); }
static void p_set32(struct rtw89_softc* sc,u32 o,u32 b){ p_w32(sc,o,p_r32(sc,o)|b); }
static void p_clr32(struct rtw89_softc* sc,u32 o,u32 b){ p_w32(sc,o,p_r32(sc,o)&~b); }
static void p_le16(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); }
static void p_le32(u8* p,u32 v){ p[0]=(u8)v; p[1]=(u8)(v>>8); p[2]=(u8)(v>>16); p[3]=(u8)(v>>24); }
static u32 p_g32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }
static void p_cpy(u8* d,const u8* s,u32 n){ while(n--)*d++=*s++; }
static void p_zero(u8* d,u32 n){ while(n--)*d++=0; }

static int r89p_dma(u8** va,u64* pa,u32 bytes){
    u32 pg=(bytes+4095)/4096; u8* v=(u8*)rtw89_dma_pages(pg); if(!v)return -1;
    u64 a=(u64)(unsigned long)v; if(a>>32)return -1; p_zero(v,pg*4096); *va=v; *pa=a; return 0; }

u32 rtw89_fill_fwcmd_desc(u8 chip,u8* d,u32 pkt_len,int fw_dl){
    if(chip==RTW89_CHIP_8852C){ p_zero(d,16); p_le32(d,(pkt_len&0x3FFF)|((u32)(fw_dl?RTW89_RX_TYPE_FWDL:RTW89_RX_TYPE_H2C)<<24)); return 16; }
    p_zero(d,24); p_le32(d,((u32)RTW89_DMA_H2C<<16)|(fw_dl?(1u<<20):0)); p_le32(d+8,pkt_len&0x3FFF); return 24; }

u32 rtw89_fill_txwd(u8 chip,u8* d,const struct rtw89_txinfo* ti){
    u32 body=chip==RTW89_CHIP_8852C?32:24, n=body+(ti->en_info?24:0); p_zero(d,n);
    p_le32(d,((u32)ti->en_info<<22)|((u32)(ti->ch_dma&15)<<16)|((u32)(ti->hdr_len&31)<<11)|((u32)ti->wd_page<<7)|((u32)ti->fw_dl<<20));
    if(chip==RTW89_CHIP_8852C)p_le32(d+4,1u<<26);
    p_le32(d+8,((u32)(ti->macid&0x7F)<<24)|((u32)(ti->qsel&0x3F)<<17)|(ti->pkt_size&0x3FFF));
    p_le32(d+12,ti->seq&0xFFF);
    if(chip==RTW89_CHIP_8852C){ if(ti->use_rate)p_le32(d+28,(1u<<31)|((u32)ti->rate<<16)); if(ti->en_info)p_le32(d+body,1u<<10); }
    else if(ti->en_info)p_le32(d+body,(ti->use_rate?(1u<<30):0)|((u32)ti->rate<<16)|(1u<<10));
    return n; }

u32 rtw89_fill_txaddr(u8* d,u32 len,u64 dma){ p_le16(d,len); p_le16(d+2,RTW89_PCI_ADDR_MSDU_LS|1u|(((u32)(dma>>32)&0xFF)<<6)); p_le32(d+4,(u32)dma); return 8; }

int rtw89_parse_rxdesc(u8 chip,const u8* b,u32 len,struct rtw89_rxinfo* ri){
    (void)chip; if(len<4+16)return -1;
    u32 info=p_g32(b); ri->fs=(info>>15)&1; ri->ls=(info>>14)&1; ri->bd_len=(u16)(info&0x3FFF); ri->tag=(u16)((info>>16)&0x1FFF);
    if(ri->bd_len>len-4||ri->bd_len<16)return -2;
    u32 w0=p_g32(b+4), w1=p_g32(b+8);
    ri->pkt_size=w0&0x3FFF; ri->shift=(u8)((w0>>14)&3); ri->mac_info=(u8)((w0>>23)&1); ri->type=(u8)((w0>>24)&15); ri->drv_info=(u8)((w0>>28)&7); ri->long_rxd=(u8)(w0>>31);
    ri->data_rate=(u16)((w1>>16)&0x1FF); ri->bw=(u8)(w1>>30);
    ri->offset=4+(ri->long_rxd?32u:16u)+(u32)ri->shift*2+(u32)ri->drv_info*8;
    if(ri->offset>4u+ri->bd_len||ri->pkt_size>4u+ri->bd_len-ri->offset)return -3;
    return 0; }

int rtw89_pci_rings_alloc(struct rtw89_softc* sc){
    struct r89p_state* s=&g_r89p; int v1=sc&&sc->chip==RTW89_CHIP_8852C;
    for(int i=0;i<R89_TX_N;i++){ struct r89p_ring* r=&s->tx[i]; const u8* bd=v1?r89p_bdram_v1[i]:r89p_bdram[i]; r->r=v1?r89p_txr_v1[i]:r89p_txr[i]; r->sidx=bd[0]; r->max=bd[1]; r->min=bd[2]; }
    for(int i=0;i<R89_RX_N;i++)s->rx[i].r=v1?r89p_rxr_v1[i]:r89p_rxr[i];
    if(s->ready)return 0;
    for(int i=0;i<R89_TX_N;i++){ struct r89p_ring* r=&s->tx[i]; r->len=r89p_txlen[i]; r->bsz=R89P_TXBUF;
        if(r89p_dma(&r->va,&r->pa,(u32)r->len*8)||r89p_dma(&r->buf,&r->bpa,(u32)r->len*r->bsz))return -1; }
    for(int i=0;i<R89_RX_N;i++){ struct r89p_ring* r=&s->rx[i]; r->len=r89p_rxlen[i]; r->bsz=(RTW89_PCI_RX_BUF_SIZE+63)&~63u;
        if(r89p_dma(&r->va,&r->pa,(u32)r->len*8)||r89p_dma(&r->buf,&r->bpa,(u32)r->len*r->bsz))return -1;
        for(u32 k=0;k<r->len;k++){ u8* bd=r->va+k*8; u64 a=r->bpa+(u64)k*r->bsz; p_le16(bd,RTW89_PCI_RX_BUF_SIZE); p_le16(bd+2,((u32)(a>>32)&0xFF)<<6); p_le32(bd+4,(u32)a); } }
    s->ready=1; return 0; }

void rtw89_pci_rings_reset(struct rtw89_softc* sc){
    struct r89p_state* s=&g_r89p;
    for(int i=0;i<R89_TX_N;i++){ struct r89p_ring* r=&s->tx[i]; r->wp=r->rp=0; p_w16(sc,r->r.num,r->len);
        p_w32(sc,r->r.bdram,(u32)r->sidx|((u32)r->max<<8)|((u32)r->min<<16)); p_w32(sc,r->r.desa,(u32)r->pa); p_w32(sc,r->r.desa+4u,(u32)(r->pa>>32)); }
    for(int i=0;i<R89_RX_N;i++){ struct r89p_ring* r=&s->rx[i]; r->wp=r->rp=0; p_w16(sc,r->r.num,r->len); p_w32(sc,r->r.desa,(u32)r->pa); p_w32(sc,r->r.desa+4u,(u32)(r->pa>>32)); } }

static int r89p_poll(struct rtw89_softc* sc,u32 o,u32 m,u32 want,u32 step,u32 total){
    for(u32 t=0;;t+=step){ if((p_r32(sc,o)&m)==want)return 0; if(t>=total)break; sc->io->delay(step); }
    sc->err_reg=o; return -1; }

void rtw89_pci_txdma_all(struct rtw89_softc* sc,int on){ if(on)p_clr32(sc,R_AX_PCIE_DMA_STOP1,B_AX_TX_STOP1_MASK_V1); else p_set32(sc,R_AX_PCIE_DMA_STOP1,B_AX_TX_STOP1_MASK_V1); }

static int r89p_pre_fwdl_v1(struct rtw89_softc* sc){
    p_set32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_WPDMA);
    p_set32(sc,R_AX_HAXI_INIT_CFG1,B_AX_STOP_AXI_MST); p_clr32(sc,R_AX_HAXI_INIT_CFG1,B_AX_TXHCI_EN_V1|B_AX_RXHCI_EN_V1);
    if(r89p_poll(sc,R_AX_PCIE_DMA_BUSY1,DMA_BUSY1_CHECK_HAXI,0,10,100)<0||r89p_poll(sc,R_AX_HAXI_DMA_BUSY2,3u,0,10,100)<0||r89p_poll(sc,R_AX_HAXI_DMA_BUSY3,3u,0,10,100)<0){ sc->err="PCIe DMA did not go idle"; return -1; }
    p_set32(sc,R_AX_TXBD_RWPTR_CLR1,0x7FFu); p_set32(sc,R_AX_TXBD_RWPTR_CLR2_V1,3u); p_set32(sc,R_AX_RXBD_RWPTR_CLR_V1,3u);
    rtw89_pci_rings_reset(sc);
    p_set32(sc,R_AX_PCIE_INIT_CFG1,B_AX_RST_BDRAM);
    if(r89p_poll(sc,R_AX_PCIE_INIT_CFG1,B_AX_RST_BDRAM,0,1,1000)<0){ sc->err="PCIe BD RAM reset stuck"; return -1; }
    p_set32(sc,R_AX_PCIE_DMA_STOP1,B_AX_TX_STOP1_MASK_HAXI); p_set32(sc,R_AX_HAXI_DMA_STOP2,3u); p_clr32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_CH12);
    p_clr32(sc,R_AX_HAXI_INIT_CFG1,B_AX_STOP_AXI_MST); p_set32(sc,R_AX_HAXI_INIT_CFG1,B_AX_TXHCI_EN_V1|B_AX_RXHCI_EN_V1);
    return 0; }

void rtw89_pci_post_init(struct rtw89_softc* sc){
    if(sc->chip==RTW89_CHIP_8852C){ p_clr32(sc,R_AX_PCIE_DMA_STOP1,B_AX_TX_STOP1_MASK_HAXI); p_clr32(sc,R_AX_HAXI_DMA_STOP2,3u); }
    else rtw89_pci_txdma_all(sc,1);
    p_clr32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_WPDMA); }

int rtw89_pci_pre_fwdl(struct rtw89_softc* sc){
    if(sc->chip!=RTW89_CHIP_8852B&&sc->chip!=RTW89_CHIP_8852C){ sc->err="unknown rtw89 chip"; return -1; }
    if(rtw89_pci_rings_alloc(sc)<0){ sc->err="no DMA memory below 4 GB for rtw89 rings"; return -1; }
    if(sc->chip==RTW89_CHIP_8852C)return r89p_pre_fwdl_v1(sc);
    p_set32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_WPDMA);
    p_clr32(sc,R_AX_PCIE_INIT_CFG1,B_AX_TXHCI_EN|B_AX_RXHCI_EN); p_set32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_PCIEIO);
    if(r89p_poll(sc,R_AX_PCIE_DMA_BUSY1,DMA_BUSY1_CHECK_V1|3u,0,10,1000)<0){ sc->err="PCIe DMA did not go idle"; return -1; }
    p_w32(sc,R_AX_TXBD_RWPTR_CLR1,0x7FFu); p_w32(sc,R_AX_RXBD_RWPTR_CLR,3u);
    rtw89_pci_rings_reset(sc);
    p_set32(sc,R_AX_PCIE_INIT_CFG1,B_AX_RST_BDRAM);
    if(r89p_poll(sc,R_AX_PCIE_INIT_CFG1,B_AX_RST_BDRAM,0,1,1000)<0){ sc->err="PCIe BD RAM reset stuck"; return -1; }
    rtw89_pci_txdma_all(sc,0); p_clr32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_CH12);
    p_set32(sc,R_AX_PCIE_INIT_CFG1,B_AX_TXHCI_EN|B_AX_RXHCI_EN); p_clr32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_PCIEIO);
    p_clr32(sc,R_AX_PCIE_DMA_STOP1,B_AX_STOP_WPDMA);
    return 0; }

static int r89p_tx_slot(struct rtw89_softc* sc,struct r89p_ring* r){
    for(u32 t=0;;t+=10){ u32 hw=(p_r32(sc,r->r.idx)&TXBD_HW_IDX_MASK)>>16; if(hw<r->len)r->rp=(u16)hw; if((u16)((r->wp+1)%r->len)!=r->rp)return r->wp; if(t>=10000)break; sc->io->delay(10); }
    sc->err="rtw89 TX ring full (card not fetching)"; sc->err_reg=r->r.idx; return -1; }
static int r89p_tx_wait(struct rtw89_softc* sc,struct r89p_ring* r){
    for(u32 t=0;;t+=10){ u32 hw=(p_r32(sc,r->r.idx)&TXBD_HW_IDX_MASK)>>16; if(hw==r->wp){ r->rp=r->wp; return 0; } if(t>=20000)break; sc->io->delay(10); }
    sc->err="rtw89 TX DMA did not complete"; sc->err_reg=r->r.idx; return -1; }
static void r89p_kick(struct rtw89_softc* sc,struct r89p_ring* r,int slot,u32 len,u64 dma){
    u8* bd=r->va+(u32)slot*8; p_le16(bd,len); p_le16(bd+2,RTW89_PCI_TXBD_OPT_LS|(((u32)(dma>>32)&0xFF)<<6)); p_le32(bd+4,(u32)dma);
    __atomic_thread_fence(__ATOMIC_SEQ_CST); r->wp=(u16)((r->wp+1)%r->len); p_w16(sc,r->r.idx,r->wp); }

int rtw89_pci_h2c_tx(struct rtw89_softc* sc,const u8* pkt,u32 len,int kind){
    struct r89p_ring* r=&g_r89p.tx[R89_TX_CH12]; if(!g_r89p.ready||!pkt||!len||len+24>R89P_TXBUF)return -2;
    int slot=r89p_tx_slot(sc,r); if(slot<0)return -1;
    u8* b=r->buf+(u32)slot*r->bsz; u32 dl=rtw89_fill_fwcmd_desc(sc->chip,b,len,kind==RTW89_H2C_PKT_FWDL); p_cpy(b+dl,pkt,len);
    r89p_kick(sc,r,slot,dl+len,r->bpa+(u64)slot*r->bsz); g_r89p.h2c_sent++;
    return r89p_tx_wait(sc,r); }

int rtw89_pci_tx_frame(struct rtw89_softc* sc,const u8* frame,u32 len,int mgmt,u8 rate,u8 macid){
    struct r89p_ring* r=&g_r89p.tx[mgmt?R89_TX_CH8:R89_TX_ACH0]; if(!g_r89p.ready||!frame||len<10||len+80>R89P_TXBUF)return -2;
    int slot=r89p_tx_slot(sc,r); if(slot<0)return -1;
    u8* b=r->buf+(u32)slot*r->bsz; u64 pa=r->bpa+(u64)slot*r->bsz;
    struct rtw89_txinfo ti={0}; ti.ch_dma=mgmt?RTW89_DMA_B0MG:RTW89_DMA_ACH0; ti.qsel=mgmt?RTW89_QSEL_B0_MGMT:RTW89_QSEL_BE0; ti.macid=macid; ti.pkt_size=len; ti.wd_page=1; ti.en_info=1; ti.use_rate=(mgmt||rate!=RTW89_RATE_AUTO)?1:0; ti.rate=rate==RTW89_RATE_AUTO?0:rate;
    ti.hdr_len=(u8)(24+(((frame[0]&0x8C)==0x88)?2:0)+(((frame[1]&3)==3)?6:0));
    u32 wd=rtw89_fill_txwd(sc->chip,b,&ti); u16 seq=(u16)(g_r89p.tx_seq++&0x7FFF);
    p_le16(b+wd,seq|RTW89_PCI_TXWP_VALID); p_zero(b+wd+2,6);
    u32 dat=(wd+8+8+63)&~63u; p_cpy(b+dat,frame,len);
    rtw89_fill_txaddr(b+wd+8,len,pa+dat);
    r89p_kick(sc,r,slot,wd+16,pa);
    return r89p_tx_wait(sc,r); }

int rtw89_pci_rx_poll(struct rtw89_softc* sc,u8* c2h,u32 cap,u32 budget){
    int got=0;
    for(int q=0;q<R89_RX_N;q++){ struct r89p_ring* r=&g_r89p.rx[q]; if(!g_r89p.ready)return -2;
        u32 hw=(p_r32(sc,r->r.idx)&TXBD_HW_IDX_MASK)>>16; if(hw>=r->len)continue; u32 n=0;
        while(r->rp!=hw&&budget){ budget--; u8* b=r->buf+(u32)r->rp*r->bsz; struct rtw89_rxinfo ri;
            if(rtw89_parse_rxdesc(sc->chip,b,r->bsz,&ri)<0||!ri.fs||!ri.ls)g_r89p.rx_drop++;
            else if(ri.type==RTW89_RX_TYPE_C2H){ g_r89p.rx_c2h++; if(c2h&&!got&&ri.pkt_size<=cap){ p_cpy(c2h,b+ri.offset,ri.pkt_size); got=(int)ri.pkt_size; } else g_r89p.rx_drop++; }
            else if(ri.type==RTW89_RX_TYPE_WIFI&&ri.pkt_size>=24){ g_r89p.rx_frames++; if(rtw89_rx_frame)rtw89_rx_frame(b+ri.offset,(int)ri.pkt_size,-60); }
            r->rp=(u16)((r->rp+1)%r->len); n++;
            if(got&&c2h)break; }
        if(n){ r->wp=(u16)((r->wp+n)%r->len); p_w16(sc,r->r.idx,r->wp); }
        if(got)return got; }
    return got; }

int rtw89_pci_c2h_rx(struct rtw89_softc* sc,u8* buf,u32 cap){ return rtw89_pci_rx_poll(sc,buf,cap,64); }
static int r89p_pre_fwdl(struct rtw89_softc* sc){ return rtw89_pci_pre_fwdl(sc); }
const struct rtw89_hci_ops rtw89_pci_hci={ rtw89_pci_h2c_tx,rtw89_pci_c2h_rx,r89p_pre_fwdl };
int rtw89_pci_attach(struct rtw89_softc* sc){ if(!sc||(sc->chip!=RTW89_CHIP_8852B&&sc->chip!=RTW89_CHIP_8852C))return -1; rtw89_hci=&rtw89_pci_hci; return 0; }
