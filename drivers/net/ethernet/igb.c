#include "include/kapi.h"
#include "include/drivers.h"
#define NRX 256
#define NTX 64
#define BUF 2048
#define R_CTRL 0x0000
#define R_STATUS 0x0008
#define R_EECD 0x0010
#define R_EERD 0x0014
#define R_CTRLEXT 0x0018
#define R_MDIC 0x0020
#define R_ICR 0x00C0
#define R_IMC 0x00D8
#define R_RCTL 0x0100
#define R_TCTL 0x0400
#define R_EIMC 0x1528
#define R_MTA 0x5200
#define R_RAL 0x5400
#define R_RAH 0x5404
#define R_RDBAL 0xC000
#define R_RDBAH 0xC004
#define R_RDLEN 0xC008
#define R_SRRCTL 0xC00C
#define R_RDH 0xC010
#define R_RDT 0xC018
#define R_RXDCTL 0xC028
#define R_TDBAL 0xE000
#define R_TDBAH 0xE004
#define R_TDLEN 0xE008
#define R_TDH 0xE010
#define R_TDT 0xE018
#define R_TXDCTL 0xE028
struct arx { volatile u64 a, b; };
struct atx { volatile u64 a; volatile u32 cmd, sta; };
static volatile u32* M; static struct arx* rx; static struct atx* tx; static u8* rxb; static u8* txb; static u8 busy[NTX]; static int rxi, txi, txc, txn, skip, fam;
static void w(u32 r,u32 v){ M[r>>2]=v; }
static u32 r(u32 reg){ return M[reg>>2]; }
static int ig_wait(u32 reg,u32 mask,u32 want,int us){ for(int t=0;t<us;t+=10){ if((r(reg)&mask)==want)return 1; kapi_delay_us(10); } return (r(reg)&mask)==want; }
static int ig_mdic(int write,int reg,u16 val,u16* out){
    w(R_MDIC,((write?1u:2u)<<26)|(1u<<21)|((u32)(reg&31)<<16)|val);
    for(int t=0;t<2000;t++){ u32 v=r(R_MDIC); if(v&(1u<<28)){ if(v&(1u<<30))return 0; if(out)*out=(u16)v; return 1; } kapi_delay_us(5); }
    return 0;
}
static void ig_reap(void){ while(txn>0&&(tx[txc].sta&1)){ busy[txc]=0; txc=(txc+1)%NTX; txn--; } }
static void ig_pm_d0(int b,int d,int f){
    if(!(kapi_pci_rd32(b,d,f,4)&(1u<<20)))return;
    int c=(int)(kapi_pci_rd32(b,d,f,0x34)&0xFC);
    for(int g=0;g<48&&c>=0x40;g++){ u32 h=kapi_pci_rd32(b,d,f,c); if((h&0xFF)==1){ u32 pm=kapi_pci_rd32(b,d,f,c+4); if(pm&3){ kapi_pci_wr32(b,d,f,c+4,pm&~3u); kapi_delay_us(10000); } return; } c=(int)((h>>8)&0xFC); }
}
static int ig_send(const u8* d,int len){
    if(len<=0||len>BUF)return -1;
    if(!(r(R_STATUS)&2))return -1;
    ig_reap();
    if(txn>=NTX-1){ u64 t0=kapi_ns(); while(txn>=NTX-1&&kapi_ns()-t0<100000000ULL){ kapi_delay_us(5); ig_reap(); } if(txn>=NTX-1)return -1; }
    struct atx* t=&tx[txi];
    u8* b=txb+txi*BUF; for(int i=0;i<len;i++)b[i]=d[i]; for(int i=len;i<60;i++)b[i]=0; if(len<60)len=60;
    t->a=kapi_phys(b); t->sta=(u32)len<<14; t->cmd=(u32)len|(3u<<20)|(1u<<24)|(1u<<25)|(1u<<27)|(1u<<29); busy[txi]=1; txn++;
    txi=(txi+1)%NTX;
    kapi_mb(); w(R_TDT,(u32)txi); kapi_mb();
    return len;
}
static int ig_recv(u8* out,int max){
    for(int guard=0;guard<NRX;guard++){
        struct arx* x=&rx[rxi]; u64 wb=x->b; u32 st=(u32)wb;
        if(!(st&1))return 0;
        int n=0, eop=(st&2)!=0;
        if(!skip&&eop&&!(st&0x80000000u)){ n=(int)((wb>>32)&0xFFFF); if(n>max)n=max; const u8* b=rxb+rxi*BUF; for(int i=0;i<n;i++)out[i]=b[i]; }
        skip=!eop;
        x->a=kapi_phys(rxb+rxi*BUF); x->b=0;
        kapi_mb(); w(R_RDT,(u32)rxi);
        rxi=(rxi+1)%NRX;
        if(n>0)return n;
    }
    return 0;
}
static int ig_link(void){ return (r(R_STATUS)&2)!=0; }
static const u16 igb_ids[]={0x10C9,0x10E6,0x10E7,0x10E8,0x1526,0x150A,0x1518,0x150D,0x150E,0x150F,0x1510,0x1511,0x1516,0x1527,0x1521,0x1522,0x1523,0x1524,0x1F40,0x1F41,0x1F45,0x1533,0x1536,0x1537,0x1538,0x157B,0x157C,0x1539,0};
static const u16 igc_ids[]={0x15F2,0x15F3,0x15F7,0x15F8,0x15FD,0x0D9F,0x125B,0x125C,0x125D,0};
int igb_probe(struct nvx_nic* out){
    int b,d,f; fam=0;
    for(int k=0;!fam&&kapi_pci_find(0x8086,igb_ids,k,&b,&d,&f);k++) if((kapi_pci_rd32(b,d,f,8)>>16)==0x0200)fam=1;
    for(int k=0;!fam&&kapi_pci_find(0x8086,igc_ids,k,&b,&d,&f);k++) if((kapi_pci_rd32(b,d,f,8)>>16)==0x0200)fam=2;
    if(!fam)return 0;
    u32 lo=kapi_pci_rd32(b,d,f,0x10); if(lo&1){ kapi_log("igb: BAR0 is not memory"); return 0; }
    u64 bar=lo&0xFFFFFFF0u; if(((lo>>1)&3)==2)bar|=(u64)kapi_pci_rd32(b,d,f,0x14)<<32;
    if(!bar){ kapi_log("igb: BAR0 not assigned"); return 0; }
    M=(volatile u32*)kapi_mmio(bar,0x20000); if(!M){ kapi_log("igb: BAR0 cannot be mapped"); return 0; }
    ig_pm_d0(b,d,f);
    kapi_pci_enable(b,d,f);
    if(r(R_STATUS)==0xFFFFFFFFu){ kapi_log("igb: device does not answer"); return 0; }
    w(R_IMC,0xFFFFFFFFu); w(R_EIMC,0xFFFFFFFFu); w(R_RCTL,0); w(R_TCTL,1u<<3); (void)r(R_STATUS);
    kapi_delay_us(10000);
    w(R_CTRL,r(R_CTRL)|(1u<<2)); ig_wait(R_STATUS,1u<<19,0,10000);
    w(R_CTRL,r(R_CTRL)|(1u<<26));
    kapi_delay_us(20000);
    if(!ig_wait(R_CTRL,1u<<26,0,200000)){ kapi_log("igb: reset did not finish"); return 0; }
    ig_wait(R_EECD,1u<<9,1u<<9,100000);
    w(R_IMC,0xFFFFFFFFu); w(R_EIMC,0xFFFFFFFFu); (void)r(R_ICR);
    w(R_CTRLEXT,r(R_CTRLEXT)|(1u<<28));
    w(R_CTRL,(r(R_CTRL)|(1u<<6))&~(1u<<3));
    { u16 c=0; if(ig_mdic(0,0,0,&c)&&c!=0xFFFF)ig_mdic(1,0,(u16)((c&~(1u<<11))|(1u<<9)|(1u<<12)),0); }
    u32 ral=r(R_RAL), rah=r(R_RAH);
    if((rah&0x80000000u)&&((ral|(rah&0xFFFF))!=0)){ for(int i=0;i<4;i++)out->mac[i]=(u8)(ral>>(8*i)); out->mac[4]=(u8)rah; out->mac[5]=(u8)(rah>>8); }
    else {
        int good=1; u16 wd[3];
        u32 ee=fam==2?0x12014u:R_EERD;
        for(int k=0;k<3&&good;k++){ w(ee,((u32)k<<2)|1u); good=ig_wait(ee,2,2,10000); wd[k]=(u16)(r(ee)>>16); }
        if(good&&((wd[0]|wd[1]|wd[2])!=0)&&((wd[0]&wd[1]&wd[2])!=0xFFFF)){ for(int k=0;k<3;k++){ out->mac[k*2]=(u8)wd[k]; out->mac[k*2+1]=(u8)(wd[k]>>8); } }
        else { u64 s=kapi_ns()*0x9E3779B97F4A7C15ULL; for(int i=0;i<6;i++)out->mac[i]=(u8)(s>>(8*i+3)); out->mac[0]=(u8)((out->mac[0]&0xFE)|0x02); kapi_log("igb: no MAC address stored, using a random local one"); }
    }
    w(R_RAL,(u32)out->mac[0]|((u32)out->mac[1]<<8)|((u32)out->mac[2]<<16)|((u32)out->mac[3]<<24));
    w(R_RAH,(u32)out->mac[4]|((u32)out->mac[5]<<8)|0x80000000u);
    for(int i=0;i<128;i++)w(R_MTA+(u32)i*4,0);
    if(!rx){ rx=(struct arx*)kapi_dma_pages(1); tx=(struct atx*)kapi_dma_pages(1); rxb=(u8*)kapi_dma_pages(NRX*BUF/4096); txb=(u8*)kapi_dma_pages(NTX*BUF/4096); }
    if(!rx||!tx||!rxb||!txb){ kapi_log("igb: no memory for the rings"); return 0; }
    for(int i=0;i<NRX;i++){ rx[i].a=kapi_phys(rxb+i*BUF); rx[i].b=0; }
    for(int i=0;i<NTX;i++){ tx[i].a=0; tx[i].cmd=0; tx[i].sta=0; busy[i]=0; }
    rxi=0; txi=0; txc=0; txn=0; skip=0;
    kapi_mb();
    w(R_RXDCTL,0); ig_wait(R_RXDCTL,1u<<25,0,10000);
    w(R_RDBAL,(u32)kapi_phys(rx)); w(R_RDBAH,(u32)(kapi_phys(rx)>>32)); w(R_RDLEN,NRX*16);
    w(R_SRRCTL,2u|(1u<<25)|(1u<<31)); w(R_RDH,0); w(R_RDT,0);
    w(R_RXDCTL,(1u<<25)|8u|(8u<<8)|(1u<<16));
    if(!ig_wait(R_RXDCTL,1u<<25,1u<<25,10000)){ kapi_log("igb: receive queue did not start"); return 0; }
    w(R_RDT,NRX-1);
    w(R_RCTL,(1u<<1)|(1u<<3)|(1u<<4)|(1u<<15)|(1u<<26));
    w(R_TXDCTL,0); ig_wait(R_TXDCTL,1u<<25,0,10000);
    w(R_TDBAL,(u32)kapi_phys(tx)); w(R_TDBAH,(u32)(kapi_phys(tx)>>32)); w(R_TDLEN,NTX*16); w(R_TDH,0); w(R_TDT,0);
    w(R_TXDCTL,(1u<<25)|8u|(1u<<8));
    if(!ig_wait(R_TXDCTL,1u<<25,1u<<25,10000)){ kapi_log("igb: transmit queue did not start"); return 0; }
    w(R_TCTL,(1u<<1)|(1u<<3)|(0x0Fu<<4)|(0x3Fu<<12)|(1u<<24));
    out->name=fam==2?"Intel I225/I226 2.5G (igc)":"Intel I210/I211/82576/I350 (igb)"; out->send=ig_send; out->recv=ig_recv; out->link=ig_link;
    kapi_log(fam==2?"igc: up":"igb: up");
    return 1;
}
