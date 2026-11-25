#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "drivers/net/wifi/realtek/rtw89/rtw89.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_core.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_pci.h"
#include "include/nvx/drivers/net/wifi/realtek/rtw89/rtw89_mac.h"
void klog(const char* s){ (void)s; }
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
void hr_delay_us(u32 us){ (void)us; }
void* dma_alloc_pages(unsigned long pg){ (void)pg; return 0; }
void wifi_hw_rx(const u8* f,int l,int r){ (void)f;(void)l;(void)r; }

static u8 regs[0x20010]; static int dle_dead, ss_dead, cam_dead, ba_dead; static u32 delays, nwr;
static u32 g32(u32 o){ return (u32)regs[o]|((u32)regs[o+1]<<8)|((u32)regs[o+2]<<16)|((u32)regs[o+3]<<24); }
static void p32(u32 o,u32 v){ for(int i=0;i<4;i++)regs[o+i]=(u8)(v>>(8*i)); }
static u32 rd(struct rtw89_softc* sc,u32 o,int w){ (void)sc; assert(o+(u32)w<=sizeof regs); u32 v=0; for(int i=0;i<w;i++)v|=(u32)regs[o+i]<<(8*i);
    if((o==0x8D00||o==0x9100)&&!dle_dead)v|=3; if(o==0x9E10&&w==4&&!ss_dead)v|=1u<<31; return v; }
static void wr(struct rtw89_softc* sc,u32 o,u32 v,int w){ (void)sc; assert(o+(u32)w<=sizeof regs); nwr++; for(int i=0;i<w;i++)regs[o+i]=(u8)(v>>(8*i));
    if(o==R_AX_WLAN_XTAL_SI_CTRL)p32(o,v&~B_AX_WL_XTAL_SI_CMD_POLL); if(o==0xCE34&&!cam_dead)p32(o,v&~0x100u); if(o==0xCE3C&&w==4&&!ba_dead)p32(o,v&~3u); }
static void dl(u32 us){ delays+=us; }
static const struct rtw89_io IO={ rd,wr,dl };
static void reset(struct rtw89_softc* sc){ memset(regs,0,sizeof regs); memset(sc,0,sizeof *sc); sc->io=&IO; sc->chip=RTW89_CHIP_8852B; sc->cv=1; dle_dead=ss_dead=cam_dead=ba_dead=0; delays=0; nwr=0; }

static void test_bar(void){
    assert(rtw89_bar_size(0xFFF00000u)==0x100000u); assert(rtw89_bar_size(0xFFFF0004u)==0x10000u); assert(rtw89_bar_size(0xFFFFF000u)==0);
    assert(rtw89_bar_size(0xFC000000u)==RTW89_BAR_MAX); assert(rtw89_bar_size(0)==0); assert(rtw89_bar_size(0xFFFFFF01u)==0); assert(rtw89_bar_size(0xFFE80000u)==0);
    printf("bar: BAR2 sizing 1 MB / 64 KB, too small / IO / non-power-of-two rejected, clamp at 2 MB\n"); }

static void test_dle_hfc(void){
    struct rtw89_softc sc; reset(&sc);
    assert(rtw89_mac_dle_init(&sc,RTW89_QTA_DLFW)<0&&strstr(sc.err,"DMAC")); reset(&sc);
    assert(rtw89_mac_dmac_pre_init(&sc)==0&&!sc.err);
    assert(g32(R_AX_DMAC_FUNC_EN)==(RTW89_DMAC_FUNC_PRE|RTW89_DLE_EN)); assert(g32(R_AX_DMAC_CLK_EN)==((1u<<18)|RTW89_DLE_EN));
    assert(g32(R_AX_WDE_PKTBUF_CFG)==0); assert(g32(R_AX_PLE_PKTBUF_CFG)==(1u|(8u<<8)|(64u<<16)));
    assert(g32(R_AX_WDE_QTA0_CFG+4)==48); assert(g32(R_AX_WDE_QTA0_CFG)==0&&g32(R_AX_WDE_QTA0_CFG+12)==0);
    assert(g32(R_AX_PLE_QTA0_CFG+8)==(16u|(16u<<16))&&g32(R_AX_PLE_QTA0_CFG+12)==(48u|(48u<<16))&&g32(R_AX_PLE_QTA0_CFG+16)==0);
    assert(g32(R_AX_CH_PAGE_CTRL)==(40u<<16)); assert((g32(R_AX_HCI_FC_CTRL)&0xF)==8u); assert(g32(R_AX_ACH0_PAGE_CTRL)==0);
    assert(rtw89_mac_dle_init(&sc,RTW89_QTA_SCC)==0);
    assert(g32(R_AX_WDE_PKTBUF_CFG)==(510u<<16)); assert(g32(R_AX_PLE_PKTBUF_CFG)==(1u|(4u<<8)|(496u<<16)));
    assert(g32(R_AX_WDE_QTA0_CFG)==(446u|(446u<<16))&&g32(R_AX_WDE_QTA0_CFG+4)==(48u|(48u<<16))&&g32(R_AX_WDE_QTA0_CFG+16)==(16u|(16u<<16)));
    assert(g32(R_AX_PLE_QTA0_CFG)==(147u|(147u<<16))&&g32(R_AX_PLE_QTA0_CFG+16)==(17u|(157u<<16))&&g32(R_AX_PLE_QTA0_CFG+24)==(89u|(229u<<16))&&g32(R_AX_PLE_QTA0_CFG+40)==(8u|(24u<<16)));
    assert(rtw89_mac_hfc_init(&sc,1,1,1)==0);
    assert(g32(R_AX_ACH0_PAGE_CTRL)==(5u|(341u<<16))&&g32(R_AX_ACH0_PAGE_CTRL+8)==(4u|(342u<<16))&&g32(R_AX_ACH0_PAGE_CTRL+16)==0&&g32(R_AX_ACH0_PAGE_CTRL+32)==(4u|(342u<<16))&&g32(R_AX_ACH0_PAGE_CTRL+40)==0);
    assert(g32(R_AX_PUB_PAGE_CTRL1)==446&&g32(R_AX_PUB_PAGE_CTRL2)==446&&g32(R_AX_CH_PAGE_CTRL)==(2u|(40u<<16))&&g32(R_AX_HCI_FC_CTRL)==(1u|8u|(1u<<4)));
    reset(&sc); dle_dead=1; assert(rtw89_mac_dmac_pre_init(&sc)<0&&strstr(sc.err,"WDE")&&sc.err_reg==0x8D00&&!(g32(R_AX_DMAC_FUNC_EN)&RTW89_DLE_EN)&&delays==2000);
    reset(&sc); sc.chip=RTW89_CHIP_8852C; p32(R_AX_HAXI_INIT_CFG1,0xC0000u|B_AX_STOP_AXI_MST|1u); p32(0x1010,0xFFFFFFFFu); p32(R_AX_HAXI_DMA_STOP2,3u); p32(0x906C,0x12345678u);
    assert(rtw89_mac_dmac_pre_init(&sc)==0&&!sc.err);
    assert(g32(R_AX_DMAC_FUNC_EN)==(RTW89_DMAC_FUNC_PRE|(1u<<14)|RTW89_DLE_EN)&&g32(R_AX_HAXI_INIT_CFG1)==(1u|B_AX_TXHCI_EN_V1|(1u<<15))&&g32(0x1010)==~0x7FF00u&&g32(R_AX_HAXI_DMA_STOP2)==0&&(regs[0x88]&8));
    assert(g32(R_AX_WDE_PKTBUF_CFG)==0&&g32(R_AX_PLE_PKTBUF_CFG)==(1u|(16u<<8)|(2544u<<16))&&g32(R_AX_WDE_QTA0_CFG+4)==60&&g32(R_AX_WDE_QTA0_CFG)==0);
    assert(g32(R_AX_PLE_QTA0_CFG+8)==(16u|(32u<<16))&&g32(R_AX_PLE_QTA0_CFG+12)==(256u|(256u<<16))&&g32(R_AX_PLE_QTA0_CFG+16)==0&&g32(0x906C)==0);
    assert(g32(0x1704)==(40u<<16)&&(g32(0x1700)&0xF)==8u&&g32(R_AX_CH_PAGE_CTRL)==0&&g32(R_AX_HCI_FC_CTRL)==0);
    assert(rtw89_mac_dle_init(&sc,RTW89_QTA_SCC)==0&&g32(R_AX_WDE_PKTBUF_CFG)==(3328u<<16)&&g32(R_AX_PLE_PKTBUF_CFG)==(1u|(26u<<8)|(1904u<<16))&&g32(R_AX_WDE_QTA0_CFG)==(3228u|(3228u<<16))&&g32(0x906C)==(16u|(1037u<<16)));
    assert(rtw89_mac_hfc_init(&sc,1,1,1)==0&&!sc.err);
    assert(g32(0x1710)==(13u|(1614u<<16))&&g32(0x171C)==(13u|(1614u<<16))&&g32(0x1720)==(13u|(1614u<<16)|(1u<<31))&&g32(0x1730)==(13u|(1614u<<16))&&g32(0x173C)==(13u|(1614u<<16)|(1u<<31)));
    assert(g32(0x1790)==(1614u|(1614u<<16))&&g32(0x1794)==3228u&&g32(0x1704)==(2u|(40u<<16))&&g32(0x1700)==(1u|8u|(1u<<4))&&g32(R_AX_ACH0_PAGE_CTRL)==0&&g32(R_AX_PUB_PAGE_CTRL1)==0&&g32(R_AX_HCI_FC_CTRL)==0);
    reset(&sc); sc.chip=3; assert(rtw89_mac_dmac_pre_init(&sc)<0&&nwr==0);
    printf("dle/hfc: DLFW quota (WDE 0/1024, PLE 64/960, bound 8, wcpu min 48), H2C-only flow control before FWDL, SCC quota tables, full HCI FC page table, DLE timeout bounded + DLE off; 8852C: HAXI pre-enable, DLFW WDE 0/2048 PLE 2544/16 bound 16 wcpu 60, 12 PLE quotas, V1 flow-control registers, SCC sizes, SCC HFC V1 page table (13/1614, groups, public 1614/1614/3228); unknown chip untouched\n"); }

static void test_post(void){
    struct rtw89_softc sc; reset(&sc); assert(rtw89_mac_dmac_pre_init(&sc)==0);
    assert(rtw89_mac_init_post(&sc)==0&&!sc.err);
    assert(regs[R_AX_SYS_FUNC_EN]==3&&(g32(R_AX_SPS_DIG_ON_CTRL0)&0x7E000u)==(0xE000u|0x20000u)&&(g32(R_AX_WLRF_CTRL)&(1u<<17))&&regs[R_AX_PHYREG_SET]==0xE);
    assert(g32(R_AX_DMAC_FUNC_EN)==(RTW89_DMAC_FUNC_RUN|RTW89_DLE_EN)&&g32(R_AX_DMAC_CLK_EN)==(RTW89_DMAC_CLK_ALL|RTW89_DLE_EN));
    assert(g32(R_AX_CMAC_FUNC_EN)==RTW89_CMAC_FUNC_RUN&&g32(R_AX_CK_EN)==RTW89_CMAC_CK_RUN);
    assert((g32(0x9E10)&0x7FFFFFFFu)==(1u|(1u<<29))&&g32(0x9C04)==0x02A95A95u&&g32(0x9C14)==0xAA55u&&g32(0x9C00)==3&&g32(0x9C40)==0x010E05F0u&&g32(0x9D00)==0x70Fu&&g32(0x9D04)==3);
    assert(g32(0xCE34)==((0x7Fu<<16)|1u)&&g32(0xCE28)==0x55555555u&&g32(0xCE24)==0x55555555u&&g32(0xCE2C)==0x55555555u);
    assert(g32(R_AX_RX_FLTR_OPT)==(RTW89_DEFAULT_RX_FLTR|(22u<<16))&&(g32(R_AX_PLCP_HDR_FLTR)&0xFFFF)==0x6F&&regs[R_AX_RCR]==1&&g32(R_AX_CCA_CONTROL)==RTW89_CCA_SET);
    assert((g32(0xC33C)&0x7F)==0x47&&(g32(0xC3FC)&2)&&(g32(0xC338)&0x1F)==0x18&&g32(0xCC80)==((1u<<17)|(1u<<16)|(1u<<26)|(0xC4u<<8))&&(regs[0xCE4B]&1)&&!(regs[0xCE4A]&1));
    assert(((g32(0xCA00)>>16)&0x7F)==6&&(g32(0xCA1C)&0xFF00)==0x7700&&(g32(0xCC04)&0xFFFF)==0x110A&&((g32(0xCC08)>>10)&3)==2&&(g32(0xCCB0)>>20&1)&&regs[0xCE3C]==4&&sc.bacam_to==0&&(g32(0xCE02)&0xFFF0)==0x20F0);
    assert(((g32(0xC090)>>8)&0xF)==3&&g32(0xC624)==((4u<<24)|(1u<<18)|(1u<<16))&&g32(0xC6E8)==0x3F&&regs[0xC600]==3&&regs[0xC660]==0x10&&g32(0xC618)==0x3FF80&&(g32(0xC088)&0xFFF)==0);
    assert((g32(0x9408)&3)==0&&(g32(0x9410)>>24&0xF)==0xF&&(g32(0x9414)&0xFF)==30&&(g32(0x9414)>>16&0xFF)==255);
    rtw89_mac_set_rx_fltr(&sc,0); assert(g32(R_AX_RX_FLTR_OPT)==(22u<<16));
    reset(&sc); assert(rtw89_mac_dmac_pre_init(&sc)==0); ba_dead=1; assert(rtw89_mac_init_post(&sc)==0&&!sc.err&&sc.bacam_to==1);
    reset(&sc); assert(rtw89_mac_dmac_pre_init(&sc)==0); ss_dead=1; assert(rtw89_mac_init_post(&sc)<0&&strstr(sc.err,"STA scheduler"));
    reset(&sc); assert(rtw89_mac_dmac_pre_init(&sc)==0); cam_dead=1; assert(rtw89_mac_init_post(&sc)<0&&strstr(sc.err,"address CAM")&&sc.err_reg==0xCE34);
    printf("mac init: BB/RF enable + XTAL RFC, DMAC/CMAC function + clock bits, SCC DLE, STA scheduler, MPDU/security engine, address CAM, RX filters (max MPDU 22x512), CCA + resp check patch, scheduler/NAV/SR/TMAC/TRXPTCL/RMAC BA CAM/CMAC com/PTCL/CMAC DMA (BA CAM timeout warn-only), scheduler/CAM timeouts named\n"); }

static void test_post_c(void){
    struct rtw89_softc sc; reset(&sc); sc.chip=RTW89_CHIP_8852C; assert(rtw89_mac_dmac_pre_init(&sc)==0); p32(0x9D00,1u<<11);
    assert(rtw89_mac_init_post(&sc)==0&&!sc.err);
    assert(regs[R_AX_SYS_FUNC_EN]==3&&(g32(R_AX_SPS_DIG_ON_CTRL0)&0xE000u)==0&&(g32(R_AX_WLRF_CTRL)&(1u<<17))&&((g32(0x0444)>>21)&3)==1&&((g32(0x0444)>>24)&3)==1&&regs[R_AX_PHYREG_SET]==0);
    assert(g32(R_AX_DMAC_FUNC_EN)==(RTW89_DMAC_FUNC_RUN|(1u<<14)|RTW89_DLE_EN)&&g32(R_AX_CMAC_FUNC_EN)==RTW89_CMAC_FUNC_RUN);
    assert(g32(0x9F48)==((520u<<16)|(1u<<31))&&g32(0x9F4C)==((1u<<8)|52u)&&g32(0x1790)==(1614u|(1614u<<16))&&(g32(0x1700)&1));
    assert(g32(0x9D00)==(0x70Fu|(1u<<4)|(1u<<11))&&(g32(0x9D1C)>>30)==2);
    assert((g32(0xC33C)&0x7F)==0x41&&!(g32(0xC3FC)&2)&&(g32(0xC338)&0x1F)==0&&(g32(0xCC00)&0x380000u)==0x380000u&&!(g32(0xCC00)&(1u<<30))&&(g32(0xCC04)&0x1F800000u)==0x1F800000u);
    assert(g32(R_AX_RX_FLTR_OPT)==(RTW89_DEFAULT_RX_FLTR|(23u<<16))&&((g32(0xC090)>>8)&0xF)==0&&g32(0xC618)==0&&(g32(0x9414)&0xFF)==30);
    reset(&sc); sc.chip=RTW89_CHIP_8852C; assert(rtw89_mac_dmac_pre_init(&sc)==0); assert(rtw89_mac_init_post(&sc)==0&&(g32(0xC338)&0x1F)==0x18&&!(g32(0x9D00)&(1u<<11)));
    reset(&sc); sc.chip=3; assert(rtw89_mac_init_post(&sc)<0&&nwr==0);
    printf("mac init c: RTL8852C BB/RF enable (AFE LDO, XTAL 0/0x90/0x80/0x81/3), DMAC func + AXIDMA, no OCP, preload 520/1/52, security engine UC mgmt dec + partial mode kept + TX timeout, SIFS 0x41, no port TSF adv, prebackoff only without partial mode, resp check set, RTS serv-BW check off, max MPDU 23x512, 885xb-only RRSR/AGG/RXDMA skipped; unknown chip untouched\n"); }

static u8 h2c[8][256]; static u32 h2cl[8]; static int h2cn; static u8 c2q[8][64]; static u32 c2l[8]; static int c2n,c2i; static int prev_calls;
static int no_ack, pre_calls, pre_fail; static int pre_hook(struct rtw89_softc* sc){ (void)sc; pre_calls++; return pre_fail?-1:0; }
static void q_ack(u8 cls,u8 func,u8 seq){ u8* c=c2q[c2n]; memset(c,0,64); c[0]=1; c[1]=1; c[4]=12; u32 w2=1u|((u32)cls<<2)|((u32)func<<8)|((u32)seq<<24); c[8]=(u8)w2; c[9]=(u8)(w2>>8); c[10]=(u8)(w2>>16); c[11]=(u8)(w2>>24); c2l[c2n++]=12; }
static int ftx(struct rtw89_softc* sc,const u8* p,u32 l,int k){ (void)sc;(void)k; if(h2cn<8&&l<=256){ memcpy(h2c[h2cn],p,l); h2cl[h2cn]=l; } h2cn++;
    if(l>=2&&(p[0]>>2)==9&&p[1]==0x16&&!no_ack&&c2n<8){ if(c2i>=c2n)c2n=c2i=0; q_ack(9,0x16,p[2]); } return 0; }
static int frx(struct rtw89_softc* sc,u8* b,u32 cap){ (void)sc; if(c2i>=c2n)return 0; u32 n=c2l[c2i]<cap?c2l[c2i]:cap; memcpy(b,c2q[c2i],n); c2i++; return (int)n; }
static const struct rtw89_hci_ops FH={ ftx,frx,0 };
static void q_scan(u8 rsn,u8 ch){ u8* c=c2q[c2n]; memset(c,0,64); c[0]=(u8)(1|(1<<2)); c[1]=9; c[4]=8+28; u32 w2=ch|((u32)rsn<<16); c[8]=(u8)w2; c[9]=(u8)(w2>>8); c[10]=(u8)(w2>>16); c[11]=(u8)(w2>>24); c2l[c2n++]=8+28; }
static void prev_hook(struct rtw89_softc* sc,u8 cat,u8 cls,u8 func,const u8* p,u32 l){ (void)sc;(void)cat;(void)cls;(void)func;(void)p;(void)l; prev_calls++; }

static void test_scan(void){
    struct rtw89_softc sc; reset(&sc); assert(rtw89_mac_dmac_pre_init(&sc)==0&&rtw89_mac_init_post(&sc)==0);
    rtw89_hci=&FH; rtw89_c2h_hook=prev_hook; sc.fw_rdy=1; u8 chs[4]={1,6,200,36};
    assert(rtw89_scan_begin(&sc,chs,4)==-1); sc.state=RTW89_ST_READY;
    u8 bad[2]={0,200}; assert(rtw89_scan_begin(&sc,bad,2)==-2&&h2cn==0);
    assert(rtw89_scan_begin(&sc,chs,4)==0&&g_r89scan.active&&g_r89scan.nch==3&&h2cn==2);
    assert((h2c[0][0]>>2)==H2C_CL_MAC_FW_OFLD&&h2c[0][1]==H2C_FUNC_ADD_SCANOFLD_CH&&(h2c[1][0]>>2)==H2C_CL_MAC_FW_OFLD&&h2c[1][1]==H2C_FUNC_SCANOFLD);
    assert((g32(R_AX_RX_FLTR_OPT)&(B_AX_A_BCN_CHK_EN|B_AX_A_BC|B_AX_A_A1_MATCH))==0&&(g32(R_AX_RX_FLTR_OPT)>>16&0x3F)==22);
    assert(rtw89_scan_finished(&sc)==0); assert(rtw89_scan_begin(&sc,chs,4)==0&&h2cn==2);
    c2n=c2i=0; q_scan(RTW89_SCAN_ENTER_CH_NOTIFY,1); q_scan(RTW89_SCAN_LEAVE_CH_NOTIFY,1); q_scan(RTW89_SCAN_ENTER_CH_NOTIFY,6);
    { u8* c=c2q[c2n]; memset(c,0,64); c[0]=1; c[1]=2; c[4]=12; memcpy(c+8,"hi\n",3); c2l[c2n++]=12; }
    rtw89_scan_poll(&sc,32); assert(g_r89scan.enter==2&&g_r89scan.leave==1&&g_r89scan.cur_ch==6&&g_r89scan.active&&prev_calls==2);
    c2n=c2i=0; q_scan(RTW89_SCAN_END_SCAN_NOTIFY,36); rtw89_scan_poll(&sc,32);
    assert(!g_r89scan.active&&g_r89scan.ended&&rtw89_scan_finished(&sc)==1&&g_r89scan.last_rsn==RTW89_SCAN_END_SCAN_NOTIFY);
    assert((g32(R_AX_RX_FLTR_OPT)&~0x3F0000u)==RTW89_DEFAULT_RX_FLTR&&(g32(R_AX_RX_FLTR_OPT)>>16&0x3F)==22);
    h2cn=0; c2n=c2i=0; no_ack=1; u32 d0=delays; assert(rtw89_scan_begin(&sc,chs,4)==-1&&h2cn==1&&!g_r89scan.active&&delays-d0==RTW89_SCAN_ACK_US&&(g32(R_AX_RX_FLTR_OPT)&B_AX_A_A1_MATCH)); no_ack=0; sc.err=0;
    rtw89_scan_pre_hook=pre_hook; pre_fail=1; h2cn=0; assert(rtw89_scan_begin(&sc,chs,4)==-1&&h2cn==0&&pre_calls==1); pre_fail=0;
    h2cn=0; c2n=c2i=0; assert(rtw89_scan_begin(&sc,0,0)==0&&g_r89scan.nch==22&&h2cn==2&&pre_calls==2); rtw89_scan_pre_hook=0;
    int k=0; while(g_r89scan.active&&k<100000){ rtw89_scan_poll(&sc,4); k++; }
    assert(!g_r89scan.active&&k==22*40+101&&h2cn==3&&h2c[2][1]==H2C_FUNC_SCANOFLD&&(g32(R_AX_RX_FLTR_OPT)&B_AX_A_A1_MATCH));
    rtw89_hci=0; rtw89_c2h_hook=0;
    printf("scan: offload H2C pair (FW_OFLD 0x16/0x17) with add-channel done-ack wait (bounded, fails closed), role setup hook before scan, invalid channels dropped, RX filter opened during scan and restored, C2H enter/leave/end notify, other C2H chained, default channel set, missing end-notify bounded + stop H2C\n"); }

static void test_fw_c(const char* fn){
    FILE* f=fopen(fn,"rb"); assert(f); static u8 b[3000000]; u32 n=(u32)fread(b,1,sizeof b,f); fclose(f); assert(n==2375560);
    struct rtw89_fw_info fi; for(u8 cv=1;cv<=2;cv++){ memset(&fi,0,sizeof fi); assert(rtw89_fw_parse(b,n,cv,&fi)==0&&fi.cv==cv&&fi.nsec>0&&fi.off+fi.size<=n); }
    printf("fw c: bundled rtw8852c_fw-1.bin MFW pick for CV 1/2 + section parse (v%u.%u.%u.%u)\n",fi.major,fi.minor,fi.sub,fi.idx); }

int main(int argc,char** argv){ if(argc>1)test_fw_c(argv[1]); test_bar(); test_dle_hfc(); test_post(); test_post_c(); test_scan(); printf("rtw89c host ok\n"); return 0; }
