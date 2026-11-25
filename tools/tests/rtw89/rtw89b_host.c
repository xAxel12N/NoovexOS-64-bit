#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"
const struct rtw89_hci_ops* rtw89_hci;
void* dma_alloc_pages(unsigned long pg){ void* p=mmap(0,pg*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0); return p==MAP_FAILED?0:p; }
static int rx_n, rx_len; static u8 rx_last[64];
void wifi_hw_rx(const u8* f,int len,int rssi){ (void)rssi; rx_n++; rx_len=len; memcpy(rx_last,f,len<64?len:64); }
static int h2c_n; static u8 h2c_last[64]; static u32 h2c_len;
static int ack_n,ack_fail; int rtw89_h2c_wait_done(struct rtw89_softc* sc,u8 cls,u8 func,u32 us){ (void)sc; assert(cls==9&&func==0x16&&us>0&&h2c_n>0); ack_n++; return ack_fail?-2:0; }
int rtw89_h2c_send(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,int rack,int dack,const u8* p,u32 len){ (void)sc;(void)rack;(void)dack; assert(cat==1&&cls==9&&(func==0x16||func==0x17)); h2c_n++; h2c_len=len; memcpy(h2c_last,p,len<64?len:64); return 0; }
int rtw89_write_xtal_si(struct rtw89_softc* sc,u8 off,u8 val,u8 mask){ (void)sc;(void)off;(void)val;(void)mask; return 0; }
static u8 regs[0x20000]; static int dead, nw, stuck_busy, noidx; static u32 wl[512][3];
static u32 g32(u32 o){ return (u32)regs[o]|((u32)regs[o+1]<<8)|((u32)regs[o+2]<<16)|((u32)regs[o+3]<<24); }
static void p32(u32 o,u32 v){ for(int i=0;i<4;i++)regs[o+i]=(u8)(v>>(8*i)); }
static u32 rd(struct rtw89_softc* sc,u32 o,int w){ (void)sc; u32 v=0; for(int i=0;i<w;i++)v|=(u32)regs[o+i]<<(8*i); if(o==R_AX_SYS_PW_CTRL&&!dead)v|=B_AX_RDY_SYSPWR; if(o==R_AX_PCIE_DMA_BUSY1&&stuck_busy)v|=1u<<18; return v; }
static void wr(struct rtw89_softc* sc,u32 o,u32 v,int w){ (void)sc; if(nw<512){ wl[nw][0]=o; wl[nw][1]=v; wl[nw][2]=(u32)w; } nw++;
    for(int i=0;i<w;i++)regs[o+i]=(u8)(v>>(8*i));
    if(dead)return;
    if(o==R_AX_SYS_PW_CTRL)p32(o,v&~B_AX_APFN_ONMAC);
    if(o==R_AX_PCIE_INIT_CFG1)p32(o,g32(o)&~B_AX_RST_BDRAM);
    if(w==2&&!noidx&&(o==0x1058||o==0x1078||o==0x1080))p32(o,(g32(o)&0xFFFF)|((v&0xFFF)<<16)); }
static u32 dl; static void dly(u32 us){ dl+=us; }
static const struct rtw89_io IO={ rd,wr,dly };
static int wrote(u32 o,u32 v){ for(int i=0;i<nw&&i<512;i++) if(wl[i][0]==o&&wl[i][1]==v)return 1; return 0; }
static int find(u32 o){ for(int i=0;i<nw&&i<512;i++) if(wl[i][0]==o)return i; return -1; }
static u32 le32(const u8* p){ return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24); }

int main(int argc,char** argv){
    struct rtw89_softc sc; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=RTW89_CHIP_8852C;
    p32(0xF4,3u<<3);
    assert(rtw8852c_pwr_on(&sc)==0);
    assert(find(0x218)>=0&&(g32(0x218)&(1u<<16)));
    assert(((g32(0x200)>>13)&7)==7);
    assert(!(g32(0x80)&(1u<<30))&&(g32(0x80)&(1u<<5))&&!(g32(0x24)&0x1F));
    assert(g32(R_AX_DMAC_FUNC_EN)==0x7FFFC000u&&g32(R_AX_CMAC_FUNC_EN)==RTW89_CMAC_FUNC_ALL);
    assert(((g32(0x2DC)>>24)&15)==1&&(g32(0x2E4)&0x70000u)==0x70000u);
    assert(find(0x200)<find(R_AX_PLATFORM_ENABLE)&&find(0x80)>find(R_AX_SYS_SDIO_CTRL));
    memset(regs,0,sizeof regs); nw=0; dead=1; dl=0; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=RTW89_CHIP_8852C;
    assert(rtw8852c_pwr_on(&sc)<0&&sc.err_reg==R_AX_SYS_PW_CTRL&&dl<=21000); dead=0;
    printf("8852C power-on ok\n");

    u8 d[64]; assert(rtw89_fill_fwcmd_desc(RTW89_CHIP_8852B,d,100,1)==24&&le32(d)==((12u<<16)|(1u<<20))&&le32(d+8)==100);
    assert(rtw89_fill_fwcmd_desc(RTW89_CHIP_8852C,d,77,0)==16&&le32(d)==(77u|(13u<<24)));
    assert(rtw89_fill_fwcmd_desc(RTW89_CHIP_8852C,d,77,1)==16&&le32(d)==(77u|(14u<<24)));

    memset(regs,0,sizeof regs); nw=0; memset(&sc,0,sizeof sc); sc.io=&IO; sc.chip=RTW89_CHIP_8852C;
    stuck_busy=1; dl=0; assert(rtw89_pci_pre_fwdl(&sc)<0&&strstr(sc.err,"idle")&&dl<=110); stuck_busy=0;
    nw=0; assert(rtw89_pci_pre_fwdl(&sc)==0);
    assert(g32(0x1328)==(60u|(4u<<8)|(1u<<16))&&g32(0x1320)==(40u|(5u<<8)|(1u<<16))&&g32(0x1300)==(0u|(5u<<8)|(2u<<16)));
    assert(g32(0x1280)==(u32)g_r89p.tx[R89_TX_CH12].pa&&g32(0x1230)==(u32)g_r89p.tx[R89_TX_ACH0].pa&&g32(0x1220)==(u32)g_r89p.rx[0].pa&&g32(0x1228)==(u32)g_r89p.rx[1].pa&&(g32(0x1210)&0xFFFF)==32&&(g32(0x1212)&0xFFFF)==16);
    assert(wrote(0x1014,0x7FF)&&wrote(0x11C4,3)&&wrote(0x1200,3)&&find(0x1200)<find(0x1300));
    { u32 s1=g32(0x1010); assert((s1&B_AX_STOP_WPDMA)&&!(s1&B_AX_STOP_CH12)&&(s1&0x7FF00u)==0x3FF00u&&(g32(0x11C0)&3)==3); }
    assert((g32(0x1000)&(B_AX_TXHCI_EN_V1|B_AX_RXHCI_EN_V1|B_AX_STOP_AXI_MST))==(B_AX_TXHCI_EN_V1|B_AX_RXHCI_EN_V1));
    rtw89_pci_post_init(&sc); assert(!(g32(0x1010)&(0x7FF00u|B_AX_STOP_WPDMA))&&!(g32(0x11C0)&3));
    assert(rtw89_pci_attach(&sc)==0&&rtw89_hci==&rtw89_pci_hci); rtw89_hci=0;
    sc.chip=9; assert(rtw89_pci_pre_fwdl(&sc)<0&&rtw89_pci_attach(&sc)<0&&!rtw89_hci);
    printf("8852C HAXI DMA ok\n");
    memset(regs,0,sizeof regs);
    sc.chip=RTW89_CHIP_8852B; stuck_busy=1; dl=0; assert(rtw89_pci_pre_fwdl(&sc)<0&&strstr(sc.err,"idle")&&dl<=1010); stuck_busy=0;
    nw=0; assert(rtw89_pci_pre_fwdl(&sc)==0);
    struct r89p_ring* h=&g_r89p.tx[R89_TX_CH12];
    assert(g32(0x1160)==(u32)h->pa&&g32(0x1164)==0&&(g32(0x1038)&0xFFFF)==16&&g32(0x1228)==(28u|(4u<<8)|(1u<<16)));
    assert(g32(0x1200)==(0u|(5u<<8)|(2u<<16))&&g32(0x1220)==(20u|(4u<<8)|(1u<<16))&&g32(0x1100)==(u32)g_r89p.rx[0].pa&&(g32(0x1020)&0xFFFF)==32);
    assert(wrote(0x1014,0x7FF)&&wrote(0x1018,3));
    u32 st=g32(0x1010); assert(!(st&(B_AX_STOP_CH12|B_AX_STOP_WPDMA|B_AX_STOP_PCIEIO))&&(st&0x70F00u)==0x30F00u);
    assert((g32(0x1000)&(B_AX_TXHCI_EN|B_AX_RXHCI_EN))==(B_AX_TXHCI_EN|B_AX_RXHCI_EN));
    assert(find(0x1018)<find(0x1160));
    assert(rtw89_pci_attach(&sc)==0&&rtw89_hci==&rtw89_pci_hci);
    u8 pkt[40]; for(int i=0;i<40;i++)pkt[i]=(u8)(i+1);
    assert(rtw89_hci->h2c_tx(&sc,pkt,40,RTW89_H2C_PKT_CMD)==0);
    u8* bd=h->va; assert((bd[0]|(bd[1]<<8))==64&&((bd[2]|(bd[3]<<8))&RTW89_PCI_TXBD_OPT_LS)&&le32(bd+4)==(u32)h->bpa);
    assert(le32(h->buf)==(12u<<16)&&le32(h->buf+8)==40&&!memcmp(h->buf+24,pkt,40)&&h->wp==1&&(g32(0x1080)&0xFFFF)==1);
    assert(rtw89_hci->h2c_tx(&sc,pkt,40,RTW89_H2C_PKT_FWDL)==0&&(le32(h->buf+h->bsz)&(1u<<20)));
    for(int i=0;i<40;i++)assert(rtw89_hci->h2c_tx(&sc,pkt,20,RTW89_H2C_PKT_CMD)==0);
    assert(h->wp==(2+40)%16);
    noidx=1; dl=0; assert(rtw89_pci_h2c_tx(&sc,pkt,20,0)<0&&strstr(sc.err,"did not complete")&&dl<=20010);
    dl=0; assert(rtw89_pci_h2c_tx(&sc,pkt,20,0)<0&&dl<=30020); noidx=0;
    p32(0x1080,(g32(0x1080)&0xFFFF)|((u32)h->wp<<16)); h->rp=h->wp;
    assert(rtw89_pci_h2c_tx(&sc,pkt,3000,0)==-2);

    u8 fr[60]; memset(fr,0,60); fr[0]=0xB0; fr[4]=0x11;
    assert(rtw89_pci_tx_frame(&sc,fr,60,1,RTW89_HW_RATE_OFDM6,0)==0);
    struct r89p_ring* m=&g_r89p.tx[R89_TX_CH8]; u8* w=m->buf;
    assert(le32(w)==((1u<<22)|(8u<<16)|(24u<<11)|(1u<<7))&&le32(w+8)==((0x12u<<17)|60u)&&le32(w+24)==((1u<<30)|(4u<<16)|(1u<<10)));
    assert((w[48]|(w[49]<<8))==(0x8000|0)&&(w[56]|(w[57]<<8))==60&&((w[58]|(w[59]<<8))&0x8001)==0x8001&&le32(w+60)==(u32)m->bpa+64&&!memcmp(w+64,fr,60));
    assert((m->va[0]|(m->va[1]<<8))==64&&le32(m->va+4)==(u32)m->bpa);
    assert(rtw89_pci_tx_frame(&sc,fr,60,0,0,3)==0&&le32(g_r89p.tx[0].buf+8)==((3u<<24)|60u)&&(le32(g_r89p.tx[0].buf)>>16&15)==0);

    struct r89p_ring* r=&g_r89p.rx[0]; u8* b=r->buf;
    u8 c2h[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    p32(0,0); memset(b,0,64); u32 tot=16+16+8; *(u32*)b=(tot&0x3FFF)|(1u<<15)|(1u<<14); *(u32*)(b+4)=16u|(10u<<24)|(1u<<28); memcpy(b+4+16+8,c2h,16);
    u8* b1=r->buf+r->bsz; memset(b1,0,128); u8 wf[30]; memset(wf,0,30); wf[0]=0x80; wf[29]=0x5A;
    *(u32*)b1=(16+2+30)|(3u<<14); *(u32*)(b1+4)=30u|(1u<<14); memcpy(b1+4+16+2,wf,30);
    p32(0x1050,2u<<16);
    u8 out[64]; int n=rtw89_hci->c2h_rx(&sc,out,sizeof out); assert(n==16&&!memcmp(out,c2h,16)&&r->rp==1&&(g32(0x1050)&0xFFFF)==1);
    n=rtw89_hci->c2h_rx(&sc,out,sizeof out); assert(n==0&&rx_n==1&&rx_len==30&&rx_last[29]==0x5A&&r->rp==2&&(g32(0x1050)&0xFFFF)==2);
    u8* b2=r->buf+2*r->bsz; *(u32*)b2=0x3FFF|(3u<<14); p32(0x1050,3u<<16); assert(rtw89_hci->c2h_rx(&sc,out,sizeof out)==0&&g_r89p.rx_drop==1&&r->rp==3);
    u8* b3=r->buf+3*r->bsz; *(u32*)b3=40|(3u<<14); *(u32*)(b3+4)=200u|(10u<<24); p32(0x1050,4u<<16); assert(rtw89_hci->c2h_rx(&sc,out,sizeof out)==0&&g_r89p.rx_drop==2);
    p32(0x1050,0xFFFu<<16); assert(rtw89_hci->c2h_rx(&sc,out,sizeof out)==0);
    printf("PCIe rings ok\n");

    FILE* f=fopen(argv[1],"rb"); assert(f); fseek(f,0,SEEK_END); long bl=ftell(f); fseek(f,0,SEEK_SET); u8* blob=malloc(bl); assert(fread(blob,1,bl,f)==(size_t)bl); fclose(f);
    struct rtw89_phy_tbls tb; assert(rtw89_phy_tables_parse(blob,(u32)bl,&tb)==0&&tb.nt==5&&tb.t[0].n==1013&&tb.t[2].kind==RTW89_TBL_RADIOA&&tb.t[3].n==8392);
    assert(rtw89_phy_tables_parse(blob,(u32)bl-1,&tb)<0); blob[0]='X'; assert(rtw89_phy_tables_parse(blob,(u32)bl,&tb)<0); blob[0]='R'; assert(rtw89_phy_tables_parse(blob,(u32)bl,&tb)==0);
    u32 hs,hi;
    for(int k=0;k<5;k++){ assert(rtw89_phy_sel_headline(&tb.t[k],1,1,&hs,&hi)==0); if(hs)assert(((tb.t[k].regs[hi].addr>>16)&0xFF)==1||((tb.t[k].regs[hi].addr>>16)&0xFF)==0xFF); }
    struct rtw89_reg2 syn[]={ {0xF0010001,0},{0xF0020001,0},{0x80020001,0},{0x40000000,0},{0x100,1},{0x90010001,0},{0x40000000,0},{0x104,2},{0xA0000000,0},{0x108,3},{0xB0000000,0},{0x10C,4},{0xFE,0},{0x110,RTW89_BYPASS_CR_DATA} };
    struct rtw89_phy_tbl st2={syn,14,1};
    assert(rtw89_phy_sel_headline(&st2,2,1,&hs,&hi)==0&&hs==2&&hi==1); assert(rtw89_phy_sel_headline(&st2,1,9,&hs,&hi)==0&&hi==0); assert(rtw89_phy_sel_headline(&st2,7,1,&hs,&hi)<0);
    memset(regs,0,sizeof regs); nw=0; dl=0;
    assert(rtw89_phy_init_reg(&sc,&st2,2,1,-1)==2&&g32(0x10100)==1&&g32(0x10104)==0&&g32(0x10108)==0&&g32(0x1010C)==4&&g32(0x10110)==0&&dl==50000);
    memset(regs,0,sizeof regs); assert(rtw89_phy_init_reg(&sc,&st2,1,1,-1)==2&&g32(0x10104)==2&&g32(0x10100)==0);
    struct rtw89_reg2 bad[]={ {0x80000005,0},{0x40000000,0},{0xA0000000,0} }; struct rtw89_phy_tbl bt={bad,3,1}; assert(rtw89_phy_init_reg(&sc,&bt,0,0,-1)<0);
    memset(regs,0,sizeof regs); nw=0; assert(rtw89_phy_write_rf(&sc,1,0x18,0xFFFFF,0x12345)==0&&g32(0x10370)==((1u<<28)|(0x18u<<20)|0x12345u));
    assert(rtw89_phy_write_rf(&sc,0,0x10018,0xFFFFF,0xABCDE)==0&&g32(0x1E060)==0xABCDE);
    assert(rtw89_phy_write_rf(&sc,0,0x00,0x3F0,0x5)==0&&g32(0x10374)==0x3F0&&g32(0x10370)==((1u<<31)|0x50u));
    p32(0x1174C,1u<<24); dl=0; assert(rtw89_phy_write_rf(&sc,0,0x00,0xFFFFF,1)<0&&dl<=31); p32(0x1174C,0);
    memset(regs,0,sizeof regs); nw=0; sc.rfe_type=1; sc.cv=1; sc.err=0; { int lr=rtw89_phy_load_8852b(&sc,blob,(u32)bl); assert(lr==0&&nw>4000); }
    printf("PHY tables ok (%d writes)\n",nw);

    assert(rtw89_chan_valid(1)&&rtw89_chan_valid(14)&&rtw89_chan_valid(36)&&!rtw89_chan_valid(37)&&rtw89_chan_valid(149)&&rtw89_chan_valid(165)&&!rtw89_chan_valid(0)&&!rtw89_chan_valid(15)&&!rtw89_chan_valid(181));
    u8 cl[]={1,6,0,37,36,52,149}; u8 ci[4+8*28]; u32 l=rtw89_build_chinfo(ci,sizeof ci,cl,7,0xFF);
    assert(l==4+5*28&&ci[0]==5&&ci[1]==7);
    assert(le32(ci+4)==(45u|(20u<<8)|(1u<<16)|(1u<<24))&&le32(ci+8)==((0x1Fu<<3)|(0xFFu<<16)));
    u8* e52=ci+4+3*28; assert(le32(e52)==(105u|(20u<<8)|(52u<<16)|(52u<<24))&&le32(e52+4)==((0x1Fu<<3)|(1u<<14)|(0xFFu<<16)|(1u<<24)));
    assert(rtw89_build_chinfo(ci,4+28,cl,7,0xFF)==4+28);
    assert(rtw89_build_chinfo(ci,sizeof ci,cl+2,2,0xFF)==0);
    u8 so[28]; assert(rtw89_build_scanofld(so,3,1,1,1)==28&&le32(so)==(3u|(1u<<16)|(1u<<19)|(1u<<20))&&le32(so+4)==1);
    assert(rtw89_hw_scan_start(&sc,cl,7)==0&&h2c_n==2&&ack_n==1&&h2c_len==28&&(le32(h2c_last)>>20&3)==1);
    assert(rtw89_hw_scan_stop(&sc)==0&&h2c_n==3&&(le32(h2c_last)>>20&3)==0);
    ack_fail=1; assert(rtw89_hw_scan_start(&sc,cl,7)==-3&&h2c_n==4&&ack_n==2); ack_fail=0; h2c_n=3;
    assert(rtw89_hw_scan_start(&sc,cl+2,2)==-2);
    free(blob); printf("scan offload ok\nrtw89b host ok\n");
    return 0; }
