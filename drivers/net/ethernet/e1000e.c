#include "include/kapi.h"
#include "include/drivers.h"
#define NRX 256
#define NTX 64
#define BUF 2048
#define R_CTRL 0x0000
#define R_STATUS 0x0008
#define R_EERD 0x0014
#define R_CTRLEXT 0x0018
#define R_FWSM 0x5B54
#define R_MDIC 0x0020
#define R_ICR 0x00C0
#define R_IMC 0x00D8
#define R_RCTL 0x0100
#define R_TCTL 0x0400
#define R_TIPG 0x0410
#define R_EXTCNF 0x0F00
#define R_IOSFPC 0x0F28
#define R_RDBAL 0x2800
#define R_RDBAH 0x2804
#define R_RDLEN 0x2808
#define R_RDH 0x2810
#define R_RDT 0x2818
#define R_TDBAL 0x3800
#define R_TDBAH 0x3804
#define R_TDLEN 0x3808
#define R_TDH 0x3810
#define R_TDT 0x3818
#define R_TXDCTL 0x3828
#define R_TARC0 0x3840
#define R_MTA 0x5200
#define R_RAL 0x5400
#define R_RAH 0x5404
struct rxd { volatile u64 addr; volatile u16 len, csum; volatile u8 status, errors; volatile u16 special; };
struct txd { volatile u64 addr; volatile u16 len; volatile u8 cso, cmd, status, css; volatile u16 special; };
static volatile u32* M; static struct rxd* rx; static struct txd* tx; static u8* rxb; static u8* txb; static int rxi, txi, txc, txn, pch, spt, swf, skip;
static void w(u32 r,u32 v){ M[r>>2]=v; }
static u32 r(u32 reg){ return M[reg>>2]; }
static int ee_wait(u32 reg,u32 mask,u32 want,int us){ for(int t=0;t<us;t+=10){ if((r(reg)&mask)==want)return 1; kapi_delay_us(10); } return (r(reg)&mask)==want; }
static int ee_swflag(int take){
    if(!pch&&!swf)return 1;
    if(!take){ w(R_EXTCNF,r(R_EXTCNF)&~(1u<<5)); return 1; }
    for(int t=0;t<100;t++){ if(!(r(R_EXTCNF)&(1u<<5)))break; kapi_delay_us(1000); }
    w(R_EXTCNF,r(R_EXTCNF)|(1u<<5));
    for(int t=0;t<100;t++){ if(r(R_EXTCNF)&(1u<<5))return 1; kapi_delay_us(1000); w(R_EXTCNF,r(R_EXTCNF)|(1u<<5)); }
    w(R_EXTCNF,r(R_EXTCNF)&~(1u<<5)); return 0;
}
static int ee_mdic(int write,int reg,u16 val,u16* out){
    u32 phy=pch?2u:1u;
    if(!ee_swflag(1))return 0;
    w(R_MDIC,((write?1u:2u)<<26)|(phy<<21)|((u32)(reg&31)<<16)|val);
    int ok=0; for(int t=0;t<2000;t++){ u32 v=r(R_MDIC); if(v&(1u<<28)){ if(!(v&(1u<<30))){ ok=1; if(out)*out=(u16)v; } break; } kapi_delay_us(5); }
    ee_swflag(0); return ok;
}
static int ee_phy_ok(void){ u16 a=0,b=0; return ee_mdic(0,2,0,&a)&&ee_mdic(0,3,0,&b)&&a!=0xFFFF&&a!=0&&b!=0xFFFF; }
static void ee_wake_phy(void){
    if(!pch||ee_phy_ok())return;
    w(R_CTRLEXT,r(R_CTRLEXT)|(1u<<11)); kapi_delay_us(50);
    if(ee_phy_ok()){ w(R_CTRLEXT,r(R_CTRLEXT)&~(1u<<11)); kapi_log("e1000e: PHY answered over SMBus"); return; }
    if(!(r(R_FWSM)&(1u<<6))){ w(R_CTRLEXT,r(R_CTRLEXT)&~(1u<<11)); kapi_log("e1000e: PHY reset blocked by the management engine"); return; }
    w(R_CTRL,(r(R_CTRL)|(1u<<16))&~(1u<<17)); kapi_delay_us(1000);
    w(R_CTRL,r(R_CTRL)&~(1u<<16));
    if(!ee_wait(R_CTRLEXT,1u<<2,1u<<2,100000))kapi_delay_us(30000);
    kapi_delay_us(30000);
    w(R_CTRLEXT,r(R_CTRLEXT)&~(1u<<11));
    kapi_log(ee_phy_ok()?"e1000e: PHY woken from low-power mode":"e1000e: PHY still not answering after the power toggle");
}
static void ee_reap(void){ while(txn>0&&(tx[txc].status&1)){ txc=(txc+1)%NTX; txn--; } }
static void ee_pm_d0(int b,int d,int f){
    if(!(kapi_pci_rd32(b,d,f,4)&(1u<<20)))return;
    int c=(int)(kapi_pci_rd32(b,d,f,0x34)&0xFC);
    for(int g=0;g<48&&c>=0x40;g++){ u32 h=kapi_pci_rd32(b,d,f,c); if((h&0xFF)==1){ u32 pm=kapi_pci_rd32(b,d,f,c+4); if(pm&3){ kapi_pci_wr32(b,d,f,c+4,pm&~3u); kapi_delay_us(10000); } return; } c=(int)((h>>8)&0xFC); }
}
static int ee_send(const u8* d,int len){
    if(len<=0||len>BUF)return -1;
    if(!(r(R_STATUS)&2))return -1;
    ee_reap();
    if(txn>=NTX-1){ u64 t0=kapi_ns(); while(txn>=NTX-1&&kapi_ns()-t0<100000000ULL){ kapi_delay_us(5); ee_reap(); } if(txn>=NTX-1)return -1; }
    struct txd* t=&tx[txi];
    u8* b=txb+txi*BUF; for(int i=0;i<len;i++)b[i]=d[i]; for(int i=len;i<60;i++)b[i]=0; if(len<60)len=60;
    t->addr=kapi_phys(b); t->len=(u16)len; t->cso=0; t->css=0; t->special=0; t->status=0; t->cmd=0x0B; txn++;
    txi=(txi+1)%NTX;
    kapi_mb(); w(R_TDT,(u32)txi); kapi_mb();
    return len;
}
static int ee_recv(u8* out,int max){
    for(int guard=0;guard<NRX;guard++){
        struct rxd* x=&rx[rxi];
        u8 st=x->status; if(!(st&1))return 0;
        int n=0, eop=(st&2)!=0;
        if(!skip&&eop&&!x->errors){ n=x->len; if(n>max)n=max; const u8* b=rxb+rxi*BUF; for(int i=0;i<n;i++)out[i]=b[i]; }
        skip=!eop;
        x->status=0; x->addr=kapi_phys(rxb+rxi*BUF);
        kapi_mb(); w(R_RDT,(u32)rxi);
        rxi=(rxi+1)%NRX;
        if(n>0)return n;
    }
    return 0;
}
static int ee_link(void){ return (r(R_STATUS)&2)!=0; }
static const u16 e8257x_ids[]={0x105E,0x105F,0x10A4,0x10BC,0x10D9,0x10DA,0x10A5,0x10D5,0x107D,0x107E,0x107F,0x10B9,0x108B,0x108C,0x109A,0x10D3,0x10F6,0x150C,0};
static const u16 pch_ids[]={0x1502,0x1503,0x153A,0x153B,0x155A,0x1559,0x15A0,0x15A1,0x15A2,0x15A3,0};
static const u16 spt_ids[]={0x156F,0x1570,0x15B7,0x15B8,0x15B9,0x15D7,0x15D8,0x15E3,0x15D6,0x15BD,0x15BE,0x15BB,0x15BC,0x15DF,0x15E0,0x15E1,0x15E2,0x0D4E,0x0D4F,0x0D4C,0x0D4D,0x0D53,0x0D55,0x15FB,0x15FC,0x15F9,0x15FA,0x15F4,0x15F5,0x1A1E,0x1A1F,0x1A1C,0x1A1D,0x550A,0x550B,0x550C,0x550D,0x550E,0x550F,0x5510,0x5511,0};
int e1000e_probe(struct nvx_nic* out){
    int b,d,f, kind=0;
    for(int k=0;!kind&&kapi_pci_find(0x8086,e8257x_ids,k,&b,&d,&f);k++) if((kapi_pci_rd32(b,d,f,8)>>16)==0x0200)kind=1;
    for(int k=0;!kind&&kapi_pci_find(0x8086,pch_ids,k,&b,&d,&f);k++) if((kapi_pci_rd32(b,d,f,8)>>16)==0x0200)kind=2;
    for(int k=0;!kind&&kapi_pci_find(0x8086,spt_ids,k,&b,&d,&f);k++) if((kapi_pci_rd32(b,d,f,8)>>16)==0x0200)kind=3;
    if(!kind)return 0;
    pch=kind>=2; spt=kind==3; { u16 dv=(u16)(kapi_pci_rd32(b,d,f,0)>>16); swf=dv==0x108B||dv==0x108C||dv==0x109A||dv==0x10D3||dv==0x10F6||dv==0x150C; }
    u32 lo=kapi_pci_rd32(b,d,f,0x10); if(lo&1){ kapi_log("e1000e: BAR0 is not memory"); return 0; }
    u64 bar=lo&0xFFFFFFF0u; if(((lo>>1)&3)==2)bar|=(u64)kapi_pci_rd32(b,d,f,0x14)<<32;
    if(!bar){ kapi_log("e1000e: BAR0 not assigned"); return 0; }
    M=(volatile u32*)kapi_mmio(bar,0x20000); if(!M){ kapi_log("e1000e: BAR0 cannot be mapped"); return 0; }
    ee_pm_d0(b,d,f);
    kapi_pci_enable(b,d,f);
    if(r(R_STATUS)==0xFFFFFFFFu){ kapi_log("e1000e: device does not answer"); return 0; }
    u32 ral0=r(R_RAL), rah0=r(R_RAH);
    w(R_IMC,0xFFFFFFFFu); w(R_RCTL,0); w(R_TCTL,1u<<3); (void)r(R_STATUS); kapi_delay_us(10000);
    ee_wake_phy();
    w(R_CTRL,r(R_CTRL)|(1u<<2)); ee_wait(R_STATUS,1u<<19,0,10000);
    u32 ctrl=r(R_CTRL)|(1u<<26);
    if(pch){ if(r(R_FWSM)&(1u<<6))ctrl|=(1u<<31); if(!ee_swflag(1))kapi_log("e1000e: software flag busy, resetting anyway"); }
    w(R_CTRL,ctrl);
    kapi_delay_us(20000);
    if(pch)w(R_EXTCNF,r(R_EXTCNF)&~(1u<<5));
    if(!ee_wait(R_CTRL,1u<<26,0,200000)){ kapi_log("e1000e: reset did not finish"); return 0; }
    if(pch){ if(ee_wait(R_STATUS,1u<<9,1u<<9,150000))w(R_STATUS,r(R_STATUS)&~(1u<<9)); kapi_delay_us(10000); ee_wake_phy(); }
    w(R_IMC,0xFFFFFFFFu); (void)r(R_ICR);
    w(R_CTRLEXT,r(R_CTRLEXT)|(1u<<28));
    w(R_CTRL,(r(R_CTRL)|(1u<<6))&~((1u<<3)|(1u<<31)));
    { u16 c=0; if(ee_mdic(0,0,0,&c)&&c!=0xFFFF)ee_mdic(1,0,(u16)((c&~(1u<<11))|(1u<<9)|(1u<<12)),0); }
    u32 ral=r(R_RAL), rah=r(R_RAH);
    if(!(rah&0x80000000u)||((ral|(rah&0xFFFF))==0)){ ral=ral0; rah=rah0; }
    if((rah&0x80000000u)&&((ral|(rah&0xFFFF))!=0)){ for(int i=0;i<4;i++)out->mac[i]=(u8)(ral>>(8*i)); out->mac[4]=(u8)rah; out->mac[5]=(u8)(rah>>8); }
    else {
        int good=!pch; u16 wd[3]={0,0,0};
        for(int k=0;k<3&&good;k++){ w(R_EERD,((u32)k<<2)|1u); good=ee_wait(R_EERD,2,2,10000); wd[k]=(u16)(r(R_EERD)>>16); }
        if(good&&((wd[0]|wd[1]|wd[2])!=0)&&((wd[0]&wd[1]&wd[2])!=0xFFFF)){ for(int k=0;k<3;k++){ out->mac[k*2]=(u8)wd[k]; out->mac[k*2+1]=(u8)(wd[k]>>8); } }
        else { u64 s=kapi_ns()*0x9E3779B97F4A7C15ULL; for(int i=0;i<6;i++)out->mac[i]=(u8)(s>>(8*i+3)); out->mac[0]=(u8)((out->mac[0]&0xFE)|0x02); kapi_log("e1000e: no MAC address stored, using a random local one"); }
    }
    w(R_RAL,(u32)out->mac[0]|((u32)out->mac[1]<<8)|((u32)out->mac[2]<<16)|((u32)out->mac[3]<<24));
    w(R_RAH,(u32)out->mac[4]|((u32)out->mac[5]<<8)|0x80000000u);
    for(int i=0;i<128;i++)w(R_MTA+(u32)i*4,0);
    if(!rx){ rx=(struct rxd*)kapi_dma_pages(1); tx=(struct txd*)kapi_dma_pages(1); rxb=(u8*)kapi_dma_pages(NRX*BUF/4096); txb=(u8*)kapi_dma_pages(NTX*BUF/4096); }
    if(!rx||!tx||!rxb||!txb){ kapi_log("e1000e: no memory for the rings"); return 0; }
    for(int i=0;i<NRX;i++){ rx[i].addr=kapi_phys(rxb+i*BUF); rx[i].status=0; }
    for(int i=0;i<NTX;i++){ tx[i].addr=0; tx[i].cmd=0; tx[i].status=1; }
    rxi=0; txi=0; txc=0; txn=0; skip=0;
    kapi_mb();
    w(R_RDBAL,(u32)kapi_phys(rx)); w(R_RDBAH,(u32)(kapi_phys(rx)>>32)); w(R_RDLEN,NRX*16); w(R_RDH,0); w(R_RDT,NRX-1);
    if(spt)w(R_IOSFPC,r(R_IOSFPC)|0x00010000u);
    w(R_RCTL,(1u<<1)|(1u<<3)|(1u<<4)|(1u<<15)|(1u<<26));
    w(R_TDBAL,(u32)kapi_phys(tx)); w(R_TDBAH,(u32)(kapi_phys(tx)>>32)); w(R_TDLEN,NTX*16); w(R_TDH,0); w(R_TDT,0);
    w(R_TXDCTL,(1u<<24)|(1u<<22)|(1u<<16)|(1u<<8)|0x1Fu);
    if(spt)w(R_TARC0,r(R_TARC0)|(3u<<28));
    w(R_TIPG,0x00602008u);
    w(R_TCTL,(1u<<1)|(1u<<3)|(0x0Fu<<4)|(0x3Fu<<12)|(1u<<24));
    out->name=kind==3?"Intel I219 (e1000e)":kind==2?"Intel I217/I218/82579 (e1000e)":"Intel 82571-82574/82583 (e1000e)"; out->send=ee_send; out->recv=ee_recv; out->link=ee_link;
    kapi_log("e1000e: up");
    return 1;
}
