#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "drivers/net/wifi/mediatek/mt7921/usb/mt7921_usb.h"
#include "drivers/net/wifi/mediatek/mt7921/mcu/mcu.h"
#include "include/nvx/net/wifi/wifi_hw.h"
void mt7921u_wifi_hw_probe(void);

struct mt7921_softc g_mt7921;
void (*mt7921_rx_deliver)(const u8* frame,u32 len,int rssi);
int g_wpa_akm, g_wifi_scanning;
static u64 now_ns; static int fail_n;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); fail_n++; } }while(0)
u64 hr_ns(void){ now_ns+=20000; return now_ns; }
void hr_delay_us(u32 us){ now_ns+=(u64)us*1000; }
void hr_delay_ms(u32 ms){ now_ns+=(u64)ms*1000000; }
void klog(const char* s){ printf("  klog: %s\n",s); }
void klog2(const char* a,const char* b){ printf("  klog: %s%s\n",a,b); }
void* kmalloc(unsigned long n){ return malloc(n); }
void kfree(void* p){ free(p); }
u32 pci_cfg(int b,int s,int f,int off){ (void)b;(void)s;(void)f;(void)off; return 0; }
void pci_cfgw(int b,int s,int f,int off,u32 v){ (void)b;(void)s;(void)f;(void)off;(void)v; }
void* dma_alloc_pages(unsigned long pages){ void* p=mmap(0,pages*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT,-1,0); return p==MAP_FAILED?0:p; }
static int nrx, nreg; static const struct wifi_hw_ops* reg_ops;
void wifi_hw_rx(const u8* f,int len,int rssi){ (void)f;(void)len;(void)rssi; nrx++; }
int wifi_hw_register(const struct wifi_hw_ops* ops,void* ctx){ (void)ctx; reg_ops=ops; nreg++; return 0; }
int wifi_scan_parse(const u8* fr,int flen,u8 ch,int rssi,int onch){ (void)fr;(void)flen;(void)ch;(void)rssi;(void)onch; return 0; }
int mt7921_scan_list(const u8* ch,int n,u8* out,int cap,int six){ (void)six; int k=0; for(int i=0;i<n&&k<cap;i++)out[k++]=ch[i]; return k; }

int regdb_ch_allowed(u8 ch){ return ch!=14; }
int regdb_is_dfs(u8 ch){ return ch>=52&&ch<=140; }
int regdb_no_ir(u8 ch){ (void)ch; return 0; }
const char* regdb_country(void){ return "SE"; }
int mt7921u_xhci_open(void){ return 0; }
static int feat_on=1, feat_fail, feat_ok, feat_begin;
int wifi_feat_on(int f){ (void)f; return feat_on; }
int wifi_feat_begin(int f){ (void)f; feat_begin++; return feat_on; }
void wifi_feat_ok(int f){ (void)f; feat_ok++; }
void wifi_feat_fail(int f,const char* why){ (void)f; (void)why; feat_fail++; }
static const char* fw_dir;
static int rd(const char* path,u8** out,u32* len){
    const char* nm=strrchr(path,'/'); char p[512]; snprintf(p,sizeof p,"%s%s",fw_dir,nm);
    FILE* f=fopen(p,"rb"); if(!f)return -1; fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    u8* b=malloc((size_t)n); if(fread(b,1,(size_t)n,f)!=(size_t)n){ fclose(f); free(b); return -1; } fclose(f); *out=b; *len=(u32)n; return 0;
}
int (*mt7921_firmware_read)(const char* path,u8** out,u32* len)=rd;

static struct { u32 a,v; } regs[512]; static int nregs;
static u32 rget(u32 a){ for(int i=0;i<nregs;i++) if(regs[i].a==a)return regs[i].v; return 0; }
static void rset(u32 a,u32 v){ for(int i=0;i<nregs;i++) if(regs[i].a==a){ regs[i].v=v; return; } regs[nregs].a=a; regs[nregs].v=v; nregs++; }
static int ctrl_fail, nctrl, bad_frames, ncmds, fwdl_bytes, fwdl_while_off, ndata, data_ok, pwr_on, sem_get, sem_rel, fw_started, wf_reset;
static u32 cids[256];
static u8 q[2][16][2048]; static int qlen[2][16], qh[2], qt[2];
static void push(int w,const u8* b,int n){ int t=qt[w]; memcpy(q[w][t],b,(size_t)n); qlen[w][t]=n; qt[w]=(t+1)%16; }
static void event(u8 seq,u8 eid,const u8* pl,int pn){ u8 b[512]; memset(b,0,36); int n=36+pn; mt_put32(b,(u32)n|((u32)PKT_TYPE_RX_EVENT<<27)); b[28]=eid; b[29]=seq; memcpy(b+36,pl,(size_t)pn); push(MTU_EP_IN_EVT,b,n); }
static int sim_ctrl(void* ctx,u8 rt,u8 req,u16 val,u16 idx,u8* buf,u16 len){
    (void)ctx; nctrl++; if(ctrl_fail)return -1; u32 a=((u32)val<<16)|idx;
    if(rt==0xDF&&req==0x63&&len==4){ mt_put32(buf,rget(a)); return 4; }
    if(rt==0x5F&&req==0x66&&len==4){ rset(a,mt_get32(buf)); return 4; }
    if(rt==0xDE&&req==0x01&&len==4){ mt_put32(buf,rget(a)); return 4; }
    if(rt==0x5E&&req==0x02&&len==4){ u32 v=mt_get32(buf); if(a==MTU_CBTOP_RGU_WF_RST&&(rget(a)&1)&&!(v&1)){ wf_reset++; rset(MTU_CONN_INFRA_STATUS,MTU_WFSYS_INIT_DONE); rset(MTU_CONN_ON_MISC,0); } rset(a,v); return 4; }
    if(rt==0x5F&&req==0x04&&len==0&&val==0&&idx==1){ pwr_on++; rset(MTU_CONN_ON_MISC,rget(MTU_CONN_ON_MISC)|1); return 0; }
    return -1;
}
static int sim_out(void* ctx,u8 ep,const u8* b,u32 n,u32 ms){
    (void)ctx;(void)ms; u32 body=mt_get32(b)&0xffff;
    if(n%4||n<4+body+4||n>4+body+8){ bad_frames++; return (int)n; }
    for(u32 i=4+body;i<n;i++) if(b[i])bad_frames++;
    const u8* t=b+4;
    if(ep==0x08){
        u32 d0=mt_get32(t); if((d0&0xffff)!=body||((d0>>23)&3)!=MT_TX_TYPE_CMD)bad_frames++;
        u8 cid=t[36],seq=t[39]; u16 ucid=mt_get16(t+34);
        int isuni=(mt_get32(t+4)&MT_TXD1_LONG_FORMAT)&&body>=48&&t[43]==MCU_CMD_UNI_EXT_ACK&&t[36]==0;
        if(ncmds<256)cids[ncmds++]=isuni?(0x10000u|ucid):cid;
        if(isuni){ u8 pl[8]={(u8)ucid,0,0,0,0,0,0,0}; event(seq,MCU_EVENT_GENERIC,pl,8); return (int)n; }
        if(cid==MCU_CMD_PATCH_SEM_CONTROL){ u8 pl[4]={0}; u8 e[36+4]; (void)e; if(t[64]==PATCH_SEM_GET){ sem_get++; } else sem_rel++;
            u8 bb[64]; memset(bb,0,40); mt_put32(bb,40u|((u32)PKT_TYPE_RX_EVENT<<27)); bb[28]=MCU_EVENT_MT_PATCH_SEM; bb[29]=seq; bb[32]=t[64]==PATCH_SEM_GET?PATCH_NOT_DL_SEM_SUCCESS:PATCH_REL_SEM_SUCCESS; push(MTU_EP_IN_EVT,bb,40); (void)pl; return (int)n; }
        if(cid==MCU_CMD_FW_START_REQ){ fw_started++; rset(MTU_CONN_ON_MISC,3); }
        if(cid==MCU_CE_CMD_GET_NIC_CAPAB&&t[41]==0){ u8 pl[20]={1,0,0,0}; mt_put32(pl+4,MT_NIC_CAP_MAC_ADDR); mt_put32(pl+8,6); u8 m[6]={0x00,0x0c,0x43,0x79,0x61,0x01}; memcpy(pl+12,m,6); event(seq,MCU_EVENT_GENERIC,pl,20); return (int)n; }
        if(seq){ u8 pl[4]={0}; event(seq,MCU_EVENT_GENERIC,pl,4); }
        return (int)n;
    }
    if(ep==0x04){
        if(rget(MTU_UDMA_TX_QSEL)&MTU_FW_DL_EN){ fwdl_bytes+=(int)body; return (int)n; }
        if(fw_started&&!(rget(MTU_UDMA_TX_QSEL)&MTU_FW_DL_EN)){ ndata++; u32 d0=mt_get32(t); if((d0&0xffff)==body&&((d0>>23)&3)==MT_TX_TYPE_SF&&mt_get32(t+28)==0){ int z=1; for(int i=32;i<64;i++)if(t[i])z=0; if(z)data_ok++; } }
        else fwdl_while_off++;
        return (int)n;
    }
    bad_frames++; return -1;
}
static int sim_in(void* ctx,u8 ep,u8* b,u32 cap,u32 ms){
    (void)ctx;(void)ms; int w=ep==0x85?MTU_EP_IN_EVT:ep==0x84?MTU_EP_IN_DATA:-1; if(w<0||qh[w]==qt[w])return 0;
    int n=qlen[w][qh[w]]; if((u32)n>cap)n=(int)cap; memcpy(b,q[w][qh[w]],(size_t)n); qh[w]=(qh[w]+1)%16; return n;
}
static struct mt7921u_hc hc={0,sim_ctrl,sim_out,sim_in,{0},{0}};
static void reset_dev(void){ memset(regs,0,sizeof regs); nregs=0; rset(MTU_HW_CHIPID,0x7961); rset(MTU_HW_REV,0x8a10); rset(MTU_CONN_ON_MISC,3); ctrl_fail=0; ncmds=0; fwdl_bytes=0; sem_get=sem_rel=fw_started=pwr_on=wf_reset=0; qh[0]=qt[0]=qh[1]=qt[1]=0; }

int main(int argc,char** argv){
    setvbuf(stdout,0,_IONBF,0);
    fw_dir=argc>1?argv[1]:getenv("MT_FW_DIR"); if(!fw_dir){ printf("usage: usb_main <fwdir>\n"); return 2; }
    CHECK(mt7921u_match(0x0e8d,0x7961)&&mt7921u_match(0x35bc,0x0107)&&mt7921u_match(0x0846,0x9060)&&!mt7921u_match(0x0e8d,0x7902)&&!mt7921u_match(0x14c3,0x7961));
    static const u8 cfg[]={9,2,0,0,1,1,0,0x80,250, 9,4,0,0,8,0xff,0xff,0xff,0,
        7,5,0x84,2,0,2,0, 7,5,0x85,2,0,2,0, 7,5,0x08,2,0,2,0, 7,5,0x04,2,0,2,0, 7,5,0x05,2,0,2,0, 7,5,0x06,2,0,2,0, 7,5,0x07,2,0,2,0, 7,5,0x09,2,0,2,0,
        9,4,1,0,1,0xe0,1,1,0, 7,5,0x81,3,16,0,1};
    int ifn=-1; CHECK(mt7921u_parse_eps(cfg,(int)sizeof cfg,&hc,&ifn)==0&&ifn==0);
    CHECK(hc.in_ep[0]==0x84&&hc.in_ep[1]==0x85&&hc.out_ep[0]==0x08&&hc.out_ep[1]==0x04&&hc.out_ep[5]==0x09);
    { struct mt7921u_hc h2; CHECK(mt7921u_parse_eps(cfg,9+9+7*3,&h2,0)<0); }
    { u8 in[40],out[128]; for(int i=0;i<40;i++)in[i]=(u8)(i+1); u32 n=mt7921u_frame(MT_Q_MCU,in,37,out,sizeof out); CHECK(n==48&&mt_get32(out)==37&&out[4]==1&&!out[41]&&!out[47]);
      u8 tx[32+30]; memset(tx,0,32); mt_put32(tx,62u|((u32)MT_TX_TYPE_CT<<23)|(1u<<25)); mt_put32(tx+28,0x123); for(int i=0;i<30;i++)tx[32+i]=0xA0; n=mt7921u_frame(MT_Q_TX,tx,62,out,sizeof out);
      CHECK(mt_get32(out)==94&&(mt_get32(out+4)&0xffff)==94&&((mt_get32(out+4)>>23)&3)==MT_TX_TYPE_SF&&((mt_get32(out+4)>>25)&0x7f)==1&&mt_get32(out+32)==0&&out[4+64]==0xA0&&n%4==0&&n>=4+94+4);
      CHECK(mt7921u_frame(MT_Q_TX,tx,32,out,sizeof out)==0&&mt7921u_frame(MT_Q_MCU,in,40,out,40)==0); }
    CHECK(mt7921u_start()<0);
    reset_dev();
    CHECK(mt7921u_attach(&hc,0x1234,0x5678)<0);
    CHECK(mt7921u_attach(&hc,0x0e8d,0x7961)==0&&g_mt7921u.state==MT_ST_FOUND);
    mt7921u_wifi_hw_probe();
    struct mt7921_softc* sc=&g_mt7921u;
    CHECK(sc->state==MT_ST_READY); if(sc->err)printf("  err: %s\n",sc->err);
    CHECK(sc->hw_rev==((0x7961u<<16)|0x10));
    CHECK(wf_reset==1&&pwr_on==1);
    CHECK(rget(MTU_UWFDMA0_TX_EXT(0))==((0x080u<<16)|4)&&rget(MTU_UWFDMA0_TX_EXT(17))==((0x2c0u<<16)|4));
    CHECK((rget(MTU_UWFDMA0_GLO_CFG)&(MTU_GLO_TX_DMA_EN|MTU_GLO_RX_DMA_EN|MTU_GLO_OMIT_TX_INFO))==(MTU_GLO_TX_DMA_EN|MTU_GLO_RX_DMA_EN|MTU_GLO_OMIT_TX_INFO)&&!(rget(MTU_UWFDMA0_GLO_CFG)&MTU_GLO_OMIT_RX_INFO));
    CHECK(rget(MTU_WFDMA_HOST_CONFIG)&MTU_HOST_RXEVT_EP4_EN);
    CHECK((rget(MTU_UDMA_WLCFG_0)&(MTU_WL_RX_EN|MTU_WL_TX_EN|MTU_WL_TX_TMOUT_FUNC_EN))==(MTU_WL_RX_EN|MTU_WL_TX_EN|MTU_WL_TX_TMOUT_FUNC_EN)&&((rget(MTU_UDMA_WLCFG_1)>>8)&0xfffff)==50000);
    CHECK(sem_get==1&&sem_rel==1&&fw_started==1&&fwdl_bytes>100000&&fwdl_while_off==0);
    CHECK(!(rget(MTU_UDMA_TX_QSEL)&MTU_FW_DL_EN));
    CHECK(cids[0]==MCU_CMD_PATCH_SEM_CONTROL&&cids[1]==MCU_CMD_PATCH_START_REQ);
    CHECK(sc->mac[0]==0x00&&sc->mac[1]==0x0c&&sc->mac[5]==0x01);
    CHECK(nreg==1&&reg_ops&&!strcmp(reg_ops->name,"mt7921u")&&feat_begin==1&&feat_ok==1&&feat_fail==0);
    CHECK(bad_frames==0&&sc->mcu_timeouts==0);
    printf("  mcu cmds=%d fwdl=%d events=%u\n",ncmds,fwdl_bytes,sc->mcu_events);
    if(!reg_ops){ printf("mt7921u: not registered, %d failures\n",fail_n); return 1; }
    { u8 f[60]; memset(f,0,sizeof f); f[0]=0x08; f[1]=0x01; int d0=ndata; CHECK(reg_ops->tx(sc,f,(int)sizeof f)==0&&ndata==d0+1&&data_ok==ndata&&sc->tx_pkts==1); }
    { u8 bad[16]; memset(bad,0,16); mt_put32(bad,200); push(MTU_EP_IN_DATA,bad,16); u32 rb=sc->rx_pkts; reg_ops->poll(sc); CHECK(sc->rx_pkts==rb); }
    { u8 m[6]; CHECK(reg_ops->get_mac(sc,m)==0&&m[2]==0x43); CHECK(reg_ops->join(sc,(const u8*)"\1\2\3\4\5\6",MT7921_VCH6+3,"x",0,0)<0); }
    { char s[600]; mt7921u_status(s,sizeof s); CHECK(strstr(s,"State=ready")&&strstr(s,"0e8d:7961")&&strstr(s,"MAC=00:0c:43:79:61:01")); }
    mt7921u_detach(); CHECK(sc->state==MT_ST_NONE&&mt7921u_start()<0);
    reset_dev(); CHECK(mt7921u_attach(&hc,0x0e8d,0x7961)==0); ctrl_fail=1; int c0=nctrl;
    CHECK(mt7921u_start()<0&&sc->state==MT_ST_HWFAIL&&sc->err&&strstr(sc->err,"control"));
    CHECK(nctrl-c0==3&&feat_fail==1); c0=nctrl; CHECK(mt7921u_start()<0&&nctrl==c0);
    reset_dev(); mt7921u_attach(&hc,0x0e8d,0x7961); rset(MTU_CONN_ON_MISC,0);
    { const char* save=fw_dir; fw_dir="/nonexistent"; CHECK(mt7921u_start()<0&&sc->state==MT_ST_NOFW); CHECK(mt7921u_start()<0); CHECK(mt7921u_start()<0); fw_dir=save;
      CHECK(mt7921u_start()<0&&strstr(sc->err,"3 times")); }
    feat_on=0; reset_dev(); mt7921u_attach(&hc,0x0e8d,0x7961); CHECK(mt7921u_start()<0&&strstr(sc->err,"Settings")); feat_on=1;
    mt7921u_enable=0; { char s[200]; mt7921u_detach(); mt7921u_status(s,sizeof s); CHECK(strstr(s,"turned off")!=0); }
    if(fail_n){ printf("mt7921u: %d failures\n",fail_n); return 1; }
    printf("mt7921u usb: all passed\n"); return 0;
}
